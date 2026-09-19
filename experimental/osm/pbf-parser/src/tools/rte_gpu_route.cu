#include "rtegpu_common.h"

#include <cuda_runtime.h>
#include <chrono>
#include <ctime>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define RTEGPU_MAX_ROUNDS 6u
#define RTEGPU_ACCESS_WALK_M 800u
#define RTEGPU_EGRESS_WALK_M 1500u
#define RTEGPU_EGRESS_WALK_SCORE_WEIGHT 2u
#define RTEGPU_BOARD_SLACK_SEC 120u
#define RTEGPU_INF_STATE ((((unsigned long long)RTEGPU_INF_TIME) << 8u) | 255ull)
#define RTEGPU_NO_INDEX 0xffffffffu
#define RTEGPU_STATE_NONE 0u
#define RTEGPU_STATE_ORIGIN_WALK 1u
#define RTEGPU_STATE_VEHICLE 2u
#define RTEGPU_STATE_TRANSFER_WALK 3u

#define OSMRTE_HEADER_SIZE 256u
#define OSMRTE_SECTION_RECORD_SIZE 64u
#define OSMRTE_TILE_RECORD_SIZE 128u
#define OSMRTE_ADDRESS_HEADER_SIZE 64u
#define OSMRTE_ADDRESS_RECORD_SIZE 80u
#define OSMRTE_ADDRESS_SCAN_BATCH_RECORDS 4096u
#define OSMRTE_SECTION_ADDRESS_DICTIONARIES 0x0400u

#define RTEGPU_ADDRESS_INDEX_HEADER_SIZE 128u
#define RTEGPU_ADDRESS_INDEX_ENTRY_SIZE 32u
#define RTEGPU_ADDRESS_INDEX_MAGIC "RTEAIDX1"
#define RTEGPU_MAX_ADDRESS_CANDIDATES 64u

static void cuda_check(cudaError_t err, const char *what) {
    if (err != cudaSuccess) {
        std::fprintf(stderr, "rte-gpu-route: CUDA %s failed: %s\n", what, cudaGetErrorString(err));
        std::exit(1);
    }
}

__host__ __device__ static unsigned long long rtegpu_pack_state(uint32_t arrival, uint8_t rides) {
    return (((unsigned long long)arrival) << 8u) | (unsigned long long)rides;
}

__host__ __device__ static uint32_t rtegpu_state_arrival(unsigned long long state) {
    return (uint32_t)(state >> 8u);
}

__host__ __device__ static uint8_t rtegpu_state_rides(unsigned long long state) {
    return (uint8_t)(state & 255ull);
}

__host__ __device__ static int rtegpu_can_board(uint32_t arrival, uint32_t departure) {
    return arrival != RTEGPU_INF_TIME && arrival <= departure && departure - arrival >= RTEGPU_BOARD_SLACK_SEC;
}

__global__ void rtegpu_trip_scan_kernel(
    uint32_t trip_count,
    const uint32_t *trip_service,
    const uint32_t *trip_route,
    const uint32_t *trip_mode,
    const uint32_t *trip_event_offset,
    const uint32_t *trip_event_count,
    const uint8_t *service_active,
    const uint32_t *event_stop,
    const uint32_t *event_arrival,
    const uint32_t *event_departure,
    const unsigned long long *base_state,
    unsigned long long *state,
    uint32_t *pred_kind,
    uint32_t *pred_previous,
    uint32_t *pred_trip,
    uint32_t *pred_route,
    uint32_t *pred_mode,
    uint32_t *pred_board,
    uint32_t *pred_alight,
    uint32_t *pred_departure,
    uint32_t *pred_arrival,
    uint32_t *pred_walk_m,
    uint32_t round
) {
    uint32_t trip_index = blockIdx.x * blockDim.x + threadIdx.x;
    uint32_t stride = blockDim.x * gridDim.x;
    for (; trip_index < trip_count; trip_index += stride) {
        uint32_t service_index = trip_service[trip_index];
        uint32_t offset = trip_event_offset[trip_index];
        uint32_t count = trip_event_count[trip_index];
        uint32_t board_stop_index = RTEGPU_NO_INDEX;
        uint32_t board_departure = 0;
        uint8_t board_rides = 0;
        int have_board = 0;
        if (!service_active[service_index]) continue;
        for (uint32_t j = 0; j < count; ++j) {
            uint32_t event_index = offset + j;
            uint32_t stop_index = event_stop[event_index];
            uint32_t event_arr = event_arrival[event_index];
            uint32_t event_dep = event_departure[event_index];
            if (have_board && event_arr >= board_departure) {
                unsigned long long candidate_state = rtegpu_pack_state(event_arr, board_rides);
                unsigned long long old = atomicMin(state + stop_index, candidate_state);
                if (pred_kind != nullptr && candidate_state < old && state[stop_index] == candidate_state) {
                    pred_kind[stop_index] = RTEGPU_STATE_VEHICLE;
                    pred_previous[stop_index] = board_stop_index;
                    pred_trip[stop_index] = trip_index;
                    pred_route[stop_index] = trip_route[trip_index];
                    pred_mode[stop_index] = trip_mode[trip_index];
                    pred_board[stop_index] = board_stop_index;
                    pred_alight[stop_index] = stop_index;
                    pred_departure[stop_index] = board_departure;
                    pred_arrival[stop_index] = event_arr;
                    pred_walk_m[stop_index] = 0;
                }
            }
            {
                unsigned long long base = base_state[stop_index];
                uint32_t base_time = rtegpu_state_arrival(base);
                uint8_t base_ride_count = rtegpu_state_rides(base);
                if (rtegpu_can_board(base_time, event_dep) && base_ride_count < RTEGPU_MAX_ROUNDS) {
                    uint8_t candidate_rides = (uint8_t)(base_ride_count + 1u);
                    if (candidate_rides == round + 1u && (!have_board || event_dep < board_departure)) {
                        have_board = 1;
                        board_stop_index = stop_index;
                        board_departure = event_dep;
                        board_rides = candidate_rides;
                    }
                }
            }
        }
    }
}

__global__ void rtegpu_transfer_kernel(
    uint32_t stop_count,
    const uint32_t *transfer_offset,
    const uint32_t *transfer_to,
    const uint32_t *transfer_walk_sec,
    const unsigned long long *base_state,
    unsigned long long *state,
    uint32_t *pred_kind,
    uint32_t *pred_previous,
    uint32_t *pred_trip,
    uint32_t *pred_route,
    uint32_t *pred_mode,
    uint32_t *pred_board,
    uint32_t *pred_alight,
    uint32_t *pred_departure,
    uint32_t *pred_arrival,
    uint32_t *pred_walk_m
) {
    uint32_t from_stop = blockIdx.x * blockDim.x + threadIdx.x;
    uint32_t stride = blockDim.x * gridDim.x;
    for (; from_stop < stop_count; from_stop += stride) {
        unsigned long long from_state = base_state[from_stop];
        uint32_t from_arrival = rtegpu_state_arrival(from_state);
        uint8_t from_rides = rtegpu_state_rides(from_state);
        if (from_arrival == RTEGPU_INF_TIME || from_rides >= RTEGPU_MAX_ROUNDS) continue;
        uint32_t begin = transfer_offset[from_stop];
        uint32_t end = transfer_offset[from_stop + 1u];
        for (uint32_t edge = begin; edge < end; ++edge) {
            uint32_t walk_sec = transfer_walk_sec[edge];
            uint32_t candidate = from_arrival + walk_sec;
            uint32_t to_stop = transfer_to[edge];
            if (candidate < from_arrival) continue;
            unsigned long long candidate_state = rtegpu_pack_state(candidate, from_rides);
            unsigned long long old = atomicMin(state + to_stop, candidate_state);
            if (pred_kind != nullptr && candidate_state < old && state[to_stop] == candidate_state) {
                pred_kind[to_stop] = RTEGPU_STATE_TRANSFER_WALK;
                pred_previous[to_stop] = from_stop;
                pred_trip[to_stop] = RTEGPU_NO_INDEX;
                pred_route[to_stop] = RTEGPU_NO_INDEX;
                pred_mode[to_stop] = 0;
                pred_board[to_stop] = from_stop;
                pred_alight[to_stop] = to_stop;
                pred_departure[to_stop] = from_arrival;
                pred_arrival[to_stop] = candidate;
                pred_walk_m[to_stop] = (uint32_t)(((uint64_t)walk_sec * 4800ull) / 3600ull);
            }
        }
    }
}

struct QueryOptions {
    const char *pack_path = nullptr;
    const char *rte_path = nullptr;
    const char *address_index_path = nullptr;
    const char *from_text = nullptr;
    const char *to_text = nullptr;
    uint32_t from_stop = UINT32_MAX;
    uint32_t to_stop = UINT32_MAX;
    int32_t from_lat = 0;
    int32_t from_lon = 0;
    int32_t to_lat = 0;
    int32_t to_lon = 0;
    bool have_from_coord = false;
    bool have_to_coord = false;
    uint32_t depart_date = 20260603u;
    uint32_t depart_seconds = 8u * 3600u;
    bool depart_explicit = false;
    uint32_t iterations = 3;
    uint32_t address_threads = 0;
    bool verify = false;
    bool show_plan = false;
    bool json = false;
    bool use_color = true;
    bool interactive = false;
    bool tui = false;
    bool api = false;
    const char *api_host = "127.0.0.1";
    uint32_t api_port = 8765;
    bool build_address_index = false;
    bool address_index_used = false;
    bool address_index_checked = false;
    bool address_index_only = false;
    std::string address_index_status = "not_used";
    std::vector<int32_t> from_candidate_lat;
    std::vector<int32_t> from_candidate_lon;
    std::vector<int32_t> to_candidate_lat;
    std::vector<int32_t> to_candidate_lon;
    uint64_t address_index_entries = 0;
    uint32_t from_match_count = 0;
    uint32_t to_match_count = 0;
    bool resolved_addresses = false;
};

struct RteGpuAddressIndexHeader {
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    uint64_t address_count = 0;
    uint64_t entry_count = 0;
    uint64_t build_unix_time = 0;
};

struct RteGpuAddressIndexEntry {
    uint64_t hash1 = 0;
    uint64_t hash2 = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    uint32_t flags = 0;
    uint32_t count = 0;
};

struct RteGpuPredHost {
    std::vector<uint32_t> kind;
    std::vector<uint32_t> previous;
    std::vector<uint32_t> trip;
    std::vector<uint32_t> route;
    std::vector<uint32_t> mode;
    std::vector<uint32_t> board;
    std::vector<uint32_t> alight;
    std::vector<uint32_t> departure;
    std::vector<uint32_t> arrival;
    std::vector<uint32_t> walk_m;
};

struct RteGpuPlanLeg {
    uint32_t kind = RTEGPU_STATE_NONE;
    uint32_t mode = 0;
    uint32_t trip = RTEGPU_NO_INDEX;
    uint32_t route = RTEGPU_NO_INDEX;
    uint32_t board = RTEGPU_NO_INDEX;
    uint32_t alight = RTEGPU_NO_INDEX;
    uint32_t departure = 0;
    uint32_t arrival = 0;
    uint32_t walk_m = 0;
};

static void compact_plan_legs(std::vector<RteGpuPlanLeg> &legs) {
    std::vector<RteGpuPlanLeg> compact;
    compact.reserve(legs.size());
    for (const RteGpuPlanLeg &leg : legs) {
        if (!compact.empty()) {
            RteGpuPlanLeg &previous = compact.back();
            if (previous.kind == RTEGPU_STATE_VEHICLE && leg.kind == RTEGPU_STATE_VEHICLE && previous.trip == leg.trip && previous.trip != RTEGPU_NO_INDEX && previous.alight == leg.board) {
                previous.alight = leg.alight;
                previous.arrival = leg.arrival;
                continue;
            }
            if (previous.kind == RTEGPU_STATE_TRANSFER_WALK && leg.kind == RTEGPU_STATE_TRANSFER_WALK && previous.alight == leg.board) {
                previous.alight = leg.alight;
                previous.arrival = leg.arrival;
                previous.walk_m += leg.walk_m;
                continue;
            }
            if (previous.kind == RTEGPU_STATE_ORIGIN_WALK && leg.kind == RTEGPU_STATE_TRANSFER_WALK && previous.alight == leg.board) {
                previous.alight = leg.alight;
                previous.arrival = leg.arrival;
                previous.walk_m += leg.walk_m;
                continue;
            }
        }
        compact.push_back(leg);
    }
    legs.swap(compact);
}

struct RteGpuDeviceContext {
    uint32_t *trip_service = nullptr;
    uint32_t *trip_route = nullptr;
    uint32_t *trip_mode = nullptr;
    uint32_t *trip_event_offset = nullptr;
    uint32_t *trip_event_count = nullptr;
    uint32_t *event_stop = nullptr;
    uint32_t *event_arrival = nullptr;
    uint32_t *event_departure = nullptr;
    uint32_t *transfer_offset = nullptr;
    uint32_t *transfer_to = nullptr;
    uint32_t *transfer_walk_sec = nullptr;
    unsigned long long *state = nullptr;
    unsigned long long *base_state = nullptr;
    uint8_t *active_services = nullptr;
    uint32_t *pred_kind = nullptr;
    uint32_t *pred_previous = nullptr;
    uint32_t *pred_trip = nullptr;
    uint32_t *pred_route = nullptr;
    uint32_t *pred_mode = nullptr;
    uint32_t *pred_board = nullptr;
    uint32_t *pred_alight = nullptr;
    uint32_t *pred_departure = nullptr;
    uint32_t *pred_arrival = nullptr;
    uint32_t *pred_walk_m = nullptr;
    size_t stop_bytes_u64 = 0;
    size_t stop_bytes_u32 = 0;
    size_t trip_bytes = 0;
    size_t event_bytes = 0;
    size_t transfer_offset_bytes = 0;
    size_t transfer_edge_bytes = 0;
    bool with_plan = false;
};

struct RteGpuQueryTiming {
    double address_ms = 0.0;
    double load_ms = 0.0;
    double copy_ms = 0.0;
    double gpu_ms = 0.0;
    double cpu_ms = 0.0;
};

struct RteGpuRouteResult {
    uint32_t candidate_origin_stops = 0;
    uint32_t candidate_destination_stops = 0;
    uint32_t gpu_best_stop = RTEGPU_NO_INDEX;
    uint32_t gpu_best_arrival = RTEGPU_INF_TIME;
    uint32_t gpu_walk_m = 0;
    bool plan_found = false;
    std::vector<RteGpuPlanLeg> plan_legs;
    RteGpuQueryTiming timing;
};

struct OsmrteAddressSection {
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t record_count = 0;
    uint32_t record_size = 0;
    bool present = false;
};

struct OsmrteAddressRecord {
    uint32_t entity_type = 0;
    uint32_t flags = 0;
    int64_t id = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    uint64_t tile_id = 0;
    uint32_t city_offset = 0;
    uint32_t city_size = 0;
    uint32_t suburb_offset = 0;
    uint32_t suburb_size = 0;
    uint32_t street_offset = 0;
    uint32_t street_size = 0;
    uint32_t housenumber_offset = 0;
    uint32_t housenumber_size = 0;
    uint32_t postcode_offset = 0;
    uint32_t postcode_size = 0;
};

struct OsmrteResolvedAddress {
    OsmrteAddressRecord record;
    std::vector<OsmrteAddressRecord> candidates;
    uint32_t match_count = 0;
    uint32_t match_score = 0;
    bool found = false;
};

struct OsmrteAddressNeedle {
    std::string main_norm;
    std::string place_norm;
    std::string preferred_place_norm;
    std::string main_name_norm;
    std::string house_norm;
    OsmrteResolvedAddress resolved;
    bool active = false;
    bool main_parsed = false;
};

static void usage(const char *program) {
    std::fprintf(stderr,
    "Usage: %s FILE.rtegpu ((--from-stop N | --from-latlon LAT,LON) (--to-stop N | --to-latlon LAT,LON) | --rte FILE.rte --from TEXT --to TEXT) [--depart YYYY-MM-DDTHH:MM[:SS]] [--iterations N] [--address-index FILE] [--address-threads N] [--verify] [--plan] [--color] [--no-color] [--json]\n"
    "       %s FILE.rtegpu --rte FILE.rte --tui [--address-index FILE] [--from TEXT] [--to TEXT] [--depart YYYY-MM-DDTHH:MM[:SS]]\n"
    "       %s FILE.rtegpu --rte FILE.rte --interactive [--address-index FILE] [--plan] [--json]    # stdin: FROM<TAB>TO\n"
    "       %s FILE.rtegpu --rte FILE.rte --api [--api-host HOST] [--api-port PORT] [--address-index FILE]\n"
    "       %s FILE.rtegpu --rte FILE.rte --build-address-index [--address-index FILE]\n",
        program,
        program,
        program,
        program,
        program);
}

static void set_depart_now(QueryOptions &options) {
    std::time_t now = std::time(nullptr);
    std::tm local;
    if (localtime_r(&now, &local) != nullptr) {
        options.depart_date = (uint32_t)(local.tm_year + 1900) * 10000u + (uint32_t)(local.tm_mon + 1) * 100u + (uint32_t)local.tm_mday;
        options.depart_seconds = (uint32_t)local.tm_hour * 3600u + (uint32_t)local.tm_min * 60u + (uint32_t)local.tm_sec;
    }
}

static QueryOptions parse_args(int argc, char **argv) {
    QueryOptions options;
    if (argc < 2) {
        usage(argv[0]);
        std::exit(1);
    }
    options.pack_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--from-stop") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_u32_arg(argv[++i], &options.from_stop)) { usage(argv[0]); std::exit(1); }
        } else if (std::strcmp(argv[i], "--rte") == 0 && i + 1 < argc) {
            options.rte_path = argv[++i];
        } else if (std::strcmp(argv[i], "--from") == 0 && i + 1 < argc) {
            options.from_text = argv[++i];
        } else if (std::strcmp(argv[i], "--to") == 0 && i + 1 < argc) {
            options.to_text = argv[++i];
        } else if (std::strcmp(argv[i], "--to-stop") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_u32_arg(argv[++i], &options.to_stop)) { usage(argv[0]); std::exit(1); }
        } else if (std::strcmp(argv[i], "--from-latlon") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_latlon(argv[++i], &options.from_lat, &options.from_lon)) { usage(argv[0]); std::exit(1); }
            options.have_from_coord = true;
        } else if (std::strcmp(argv[i], "--to-latlon") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_latlon(argv[++i], &options.to_lat, &options.to_lon)) { usage(argv[0]); std::exit(1); }
            options.have_to_coord = true;
        } else if (std::strcmp(argv[i], "--depart") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_depart(argv[++i], &options.depart_date, &options.depart_seconds)) { usage(argv[0]); std::exit(1); }
            options.depart_explicit = true;
        } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_u32_arg(argv[++i], &options.iterations) || options.iterations == 0) { usage(argv[0]); std::exit(1); }
        } else if (std::strcmp(argv[i], "--address-threads") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_u32_arg(argv[++i], &options.address_threads)) { usage(argv[0]); std::exit(1); }
        } else if (std::strcmp(argv[i], "--address-index") == 0 && i + 1 < argc) {
            options.address_index_path = argv[++i];
        } else if (std::strcmp(argv[i], "--build-address-index") == 0) {
            options.build_address_index = true;
        } else if (std::strcmp(argv[i], "--interactive") == 0) {
            options.interactive = true;
        } else if (std::strcmp(argv[i], "--tui") == 0) {
            options.tui = true;
        } else if (std::strcmp(argv[i], "--api") == 0) {
            options.api = true;
        } else if (std::strcmp(argv[i], "--api-host") == 0 && i + 1 < argc) {
            options.api_host = argv[++i];
        } else if (std::strcmp(argv[i], "--api-port") == 0 && i + 1 < argc) {
            if (!rtegpu_parse_u32_arg(argv[++i], &options.api_port) || options.api_port == 0 || options.api_port > 65535u) { usage(argv[0]); std::exit(1); }
        } else if (std::strcmp(argv[i], "--verify") == 0) {
            options.verify = true;
        } else if (std::strcmp(argv[i], "--plan") == 0) {
            options.show_plan = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            options.json = true;
        } else if (std::strcmp(argv[i], "--color") == 0) {
            options.use_color = true;
        } else if (std::strcmp(argv[i], "--no-color") == 0) {
            options.use_color = false;
        } else {
            usage(argv[0]);
            std::exit(1);
        }
    }
    if ((options.build_address_index || options.interactive || options.tui || options.api) && options.rte_path == nullptr) { usage(argv[0]); std::exit(1); }
    if (options.tui && (options.json || options.verify || options.from_stop != UINT32_MAX || options.to_stop != UINT32_MAX || options.have_from_coord || options.have_to_coord)) { usage(argv[0]); std::exit(1); }
    if (options.interactive && (options.from_text != nullptr || options.to_text != nullptr || options.from_stop != UINT32_MAX || options.to_stop != UINT32_MAX || options.have_from_coord || options.have_to_coord)) { usage(argv[0]); std::exit(1); }
    if (options.api && (options.from_text != nullptr || options.to_text != nullptr || options.from_stop != UINT32_MAX || options.to_stop != UINT32_MAX || options.have_from_coord || options.have_to_coord || options.verify)) { usage(argv[0]); std::exit(1); }
    bool has_address_query = options.rte_path != nullptr || options.from_text != nullptr || options.to_text != nullptr;
    if (has_address_query && !options.build_address_index &&
        !options.interactive && !options.tui && !options.api && !(options.rte_path != nullptr && options.from_text != nullptr && options.to_text != nullptr)) { usage(argv[0]); std::exit(1); }
    if (!options.tui && has_address_query && options.rte_path != nullptr && (options.from_text != nullptr || options.to_text != nullptr) &&
        !(options.from_text != nullptr && options.to_text != nullptr)) { usage(argv[0]); std::exit(1); }
    if (options.rte_path == nullptr && !options.build_address_index && !options.interactive && !options.tui && !options.api) {
        if (options.from_stop == UINT32_MAX && !options.have_from_coord) { usage(argv[0]); std::exit(1); }
        if (options.to_stop == UINT32_MAX && !options.have_to_coord) { usage(argv[0]); std::exit(1); }
    }
    if (options.json) {
        options.show_plan = true;
        options.use_color = false;
    }
    if (options.tui) {
        options.show_plan = true;
    }
    if (options.api) {
        options.show_plan = true;
        options.json = true;
        options.use_color = false;
    }
    if (!options.depart_explicit) set_depart_now(options);
    return options;
}

static void osmrte_read_at(std::ifstream &file, uint64_t offset, void *data, size_t size) {
    file.seekg((std::streamoff)offset, std::ios::beg);
    if (!file.good()) throw std::runtime_error("seek failed while reading .rte address data");
    file.read((char *)data, (std::streamsize)size);
    if ((size_t)file.gcount() != size) throw std::runtime_error("short read while reading .rte address data");
}

static bool rte_source_stat(const char *path, uint64_t *size_out, uint64_t *mtime_out) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *size_out = (uint64_t)st.st_size;
    *mtime_out = (uint64_t)st.st_mtime;
    return true;
}

static std::string default_address_index_path(const QueryOptions &options) {
    if (options.address_index_path != nullptr) return std::string(options.address_index_path);
    if (options.rte_path == nullptr) return std::string();
    return std::string(options.rte_path) + ".addridx";
}

static OsmrteAddressRecord parse_osmrte_address_record(const unsigned char bytes[OSMRTE_ADDRESS_RECORD_SIZE]) {
    OsmrteAddressRecord record;
    record.entity_type = rtegpu_read_u32(bytes + 0);
    record.flags = rtegpu_read_u32(bytes + 4);
    record.id = (int64_t)rtegpu_read_u64(bytes + 8);
    record.lat_e7 = rtegpu_read_i32(bytes + 16);
    record.lon_e7 = rtegpu_read_i32(bytes + 20);
    record.tile_id = rtegpu_read_u64(bytes + 24);
    record.city_offset = rtegpu_read_u32(bytes + 40);
    record.city_size = rtegpu_read_u32(bytes + 44);
    record.suburb_offset = rtegpu_read_u32(bytes + 48);
    record.suburb_size = rtegpu_read_u32(bytes + 52);
    record.street_offset = rtegpu_read_u32(bytes + 56);
    record.street_size = rtegpu_read_u32(bytes + 60);
    record.housenumber_offset = rtegpu_read_u32(bytes + 64);
    record.housenumber_size = rtegpu_read_u32(bytes + 68);
    record.postcode_offset = rtegpu_read_u32(bytes + 72);
    record.postcode_size = rtegpu_read_u32(bytes + 76);
    return record;
}

static OsmrteAddressSection read_osmrte_address_section(std::ifstream &file) {
    unsigned char header[OSMRTE_HEADER_SIZE];
    osmrte_read_at(file, 0, header, sizeof(header));
    if (std::memcmp(header, "OSMRTE01", 8) != 0) throw std::runtime_error("not an OSMRTE01 file");
    if (rtegpu_read_u32(header + 12) != OSMRTE_HEADER_SIZE) throw std::runtime_error("unsupported .rte header size");
    uint64_t section_directory_offset = rtegpu_read_u64(header + 64);
    uint32_t section_count = rtegpu_read_u32(header + 72);
    uint32_t section_record_size = rtegpu_read_u32(header + 76);
    if (section_record_size != OSMRTE_SECTION_RECORD_SIZE) throw std::runtime_error("unsupported .rte section record size");
    OsmrteAddressSection section;
    for (uint32_t i = 0; i < section_count; ++i) {
        unsigned char bytes[OSMRTE_SECTION_RECORD_SIZE];
        osmrte_read_at(file, section_directory_offset + (uint64_t)i * OSMRTE_SECTION_RECORD_SIZE, bytes, sizeof(bytes));
        if (rtegpu_read_u32(bytes + 0) != OSMRTE_SECTION_ADDRESS_DICTIONARIES) continue;
        section.present = true;
        section.offset = rtegpu_read_u64(bytes + 8);
        section.size = rtegpu_read_u64(bytes + 16);
        section.record_count = rtegpu_read_u64(bytes + 32);
        section.record_size = rtegpu_read_u32(bytes + 40);
    }
    if (!section.present || section.record_size != OSMRTE_ADDRESS_RECORD_SIZE) throw std::runtime_error(".rte has no compatible address section");
    return section;
}

static std::vector<char> read_osmrte_address_strings(std::ifstream &file, const OsmrteAddressSection &section) {
    unsigned char header[OSMRTE_ADDRESS_HEADER_SIZE];
    osmrte_read_at(file, section.offset, header, sizeof(header));
    if (std::memcmp(header, "ADDRIDX1", 8) != 0) throw std::runtime_error("unsupported address section magic");
    if (rtegpu_read_u32(header + 24) != OSMRTE_ADDRESS_RECORD_SIZE) throw std::runtime_error("unsupported address record size");
    uint32_t strings_size = rtegpu_read_u32(header + 28);
    uint64_t strings_offset = rtegpu_read_u64(header + 32);
    if (strings_offset > section.size || (uint64_t)strings_size > section.size - strings_offset) throw std::runtime_error("address string table outside section");
    std::vector<char> strings((size_t)strings_size + 1u, 0);
    if (strings_size != 0) osmrte_read_at(file, section.offset + strings_offset, strings.data(), strings_size);
    return strings;
}

static std::string normalize_text_copy_cpp(const char *data, size_t size) {
    std::string out;
    out.reserve(size * 2u);
    for (size_t index = 0; index < size;) {
        unsigned char ch = (unsigned char)data[index];
        if (ch >= 'A' && ch <= 'Z') { out.push_back((char)(ch + ('a' - 'A'))); index += 1; }
        else if (ch == 0xc3u && index + 1u < size && (unsigned char)data[index + 1u] == 0x9fu) { out.push_back('s'); out.push_back('s'); index += 2; }
        else { out.push_back((char)ch); index += 1; }
    }
    return out;
}

static void append_unique_string_cpp(std::vector<std::string> &values, const std::string &value) {
    if (value.empty()) return;
    for (const std::string &existing : values) if (existing == value) return;
    values.push_back(value);
}

static std::string fold_german_umlauts_cpp(const std::string &text) {
    std::string out;
    out.reserve(text.size() + 4u);
    for (size_t index = 0; index < text.size();) {
        unsigned char ch = (unsigned char)text[index];
        if (ch == 0xc3u && index + 1u < text.size()) {
            unsigned char next = (unsigned char)text[index + 1u];
            if (next == 0xa4u || next == 0x84u) { out.append("ae"); index += 2u; continue; }
            if (next == 0xb6u || next == 0x96u) { out.append("oe"); index += 2u; continue; }
            if (next == 0xbcu || next == 0x9cu) { out.append("ue"); index += 2u; continue; }
            if (next == 0x9fu) { out.append("ss"); index += 2u; continue; }
        }
        out.push_back((char)ch);
        index += 1u;
    }
    return out;
}

static void collect_ascii_umlaut_variants_cpp(const std::string &text, size_t offset, std::string current, std::vector<std::string> &values) {
    if (values.size() >= 16u) return;
    size_t pos = std::string::npos;
    const char *umlaut = nullptr;
    size_t token_size = 0;
    for (size_t i = offset; i + 1u < text.size(); ++i) {
        if (text.compare(i, 2u, "ae") == 0) { pos = i; umlaut = "\xc3\xa4"; token_size = 2u; break; }
        if (text.compare(i, 2u, "oe") == 0) { pos = i; umlaut = "\xc3\xb6"; token_size = 2u; break; }
        if (text.compare(i, 2u, "ue") == 0) { pos = i; umlaut = "\xc3\xbc"; token_size = 2u; break; }
    }
    if (pos == std::string::npos) return;
    current.replace(pos, token_size, umlaut);
    append_unique_string_cpp(values, current);
    collect_ascii_umlaut_variants_cpp(current, pos + std::strlen(umlaut), current, values);
    collect_ascii_umlaut_variants_cpp(text, pos + token_size, text, values);
}

static bool normalized_is_alnum_cpp(char ch);
static std::string trim_normalized_span_cpp(const char *text, size_t size);

static std::vector<std::string> normalized_aliases_cpp(const std::string &text) {
    std::vector<std::string> values;
    append_unique_string_cpp(values, text);
    std::string folded = fold_german_umlauts_cpp(text);
    append_unique_string_cpp(values, folded);
    collect_ascii_umlaut_variants_cpp(text, 0, text, values);
    if (folded != text) collect_ascii_umlaut_variants_cpp(folded, 0, folded, values);
    return values;
}

static std::vector<std::string> normalized_place_aliases_cpp(const std::string &text) {
    std::vector<std::string> values = normalized_aliases_cpp(text);
    size_t in_pos = text.find(" in ");
    if (in_pos != std::string::npos) {
        std::string prefix = trim_normalized_span_cpp(text.data(), in_pos);
        for (const std::string &alias : normalized_aliases_cpp(prefix)) append_unique_string_cpp(values, alias);
    }
    size_t paren_pos = text.find("(");
    if (paren_pos != std::string::npos) {
        std::string prefix = trim_normalized_span_cpp(text.data(), paren_pos);
        for (const std::string &alias : normalized_aliases_cpp(prefix)) append_unique_string_cpp(values, alias);
    }
    return values;
}

static bool normalized_place_strings_match_tolerant_cpp(const std::string &field, const std::string &query) {
    if (field.empty() || query.empty()) return false;
    std::vector<std::string> field_values = normalized_place_aliases_cpp(field);
    std::vector<std::string> query_values = normalized_place_aliases_cpp(query);
    for (const std::string &field_value : field_values) {
        for (const std::string &query_value : query_values) {
            if (field_value == query_value) return true;
            if (field_value.size() > query_value.size() && field_value.compare(0, query_value.size(), query_value) == 0 && !normalized_is_alnum_cpp(field_value[query_value.size()])) return true;
            if (query_value.size() > field_value.size() && query_value.compare(0, field_value.size(), field_value) == 0 && !normalized_is_alnum_cpp(query_value[field_value.size()])) return true;
        }
    }
    return false;
}

static bool normalized_strings_match_tolerant_cpp(const std::string &left, const std::string &right) {
    if (left.empty() || right.empty()) return false;
    if (left == right) return true;
    std::vector<std::string> left_values = normalized_aliases_cpp(left);
    std::vector<std::string> right_values = normalized_aliases_cpp(right);
    for (const std::string &left_value : left_values) for (const std::string &right_value : right_values) if (left_value == right_value) return true;
    return false;
}

static unsigned int edit_distance_bounded_cpp(const std::string &left, const std::string &right, unsigned int limit) {
    size_t left_size = left.size();
    size_t right_size = right.size();
    if (left_size > right_size + limit || right_size > left_size + limit) return limit + 1u;
    std::vector<unsigned int> previous(right_size + 1u);
    std::vector<unsigned int> current(right_size + 1u);
    for (size_t j = 0; j <= right_size; ++j) previous[j] = (unsigned int)j;
    for (size_t i = 1; i <= left_size; ++i) {
        current[0] = (unsigned int)i;
        unsigned int row_min = current[0];
        for (size_t j = 1; j <= right_size; ++j) {
            unsigned int cost = left[i - 1u] == right[j - 1u] ? 0u : 1u;
            unsigned int value = std::min(std::min(previous[j] + 1u, current[j - 1u] + 1u), previous[j - 1u] + cost);
            if (i > 1u && j > 1u && left[i - 1u] == right[j - 2u] && left[i - 2u] == right[j - 1u]) value = std::min(value, previous[j - 2u] + 1u);
            current[j] = value;
            row_min = std::min(row_min, value);
        }
        if (row_min > limit) return limit + 1u;
        previous.swap(current);
    }
    return previous[right_size];
}

static bool normalized_strings_match_fuzzy_cpp(const std::string &left, const std::string &right) {
    if (normalized_strings_match_tolerant_cpp(left, right)) return true;
    std::vector<std::string> left_values = normalized_aliases_cpp(left);
    std::vector<std::string> right_values = normalized_aliases_cpp(right);
    for (const std::string &left_value : left_values) {
        for (const std::string &right_value : right_values) {
            size_t min_size = std::min(left_value.size(), right_value.size());
            if (min_size < 8u) continue;
            if (!left_value.empty() && !right_value.empty() && left_value[0] != right_value[0]) continue;
            if (edit_distance_bounded_cpp(left_value, right_value, 2u) <= 2u) return true;
        }
    }
    return false;
}

static std::vector<std::string> normalized_house_aliases_cpp(const std::string &house_norm) {
    std::vector<std::string> values;
    append_unique_string_cpp(values, house_norm);
    size_t token_begin = 0;
    for (size_t index = 0; index <= house_norm.size(); ++index) {
        bool at_end = index == house_norm.size();
        char ch = at_end ? ',' : house_norm[index];
        if (ch == ',' || ch == ';' || ch == '/') {
            size_t token_end = index;
            while (token_begin < token_end && (house_norm[token_begin] == ' ' || house_norm[token_begin] == '\t')) token_begin += 1u;
            while (token_end > token_begin && (house_norm[token_end - 1u] == ' ' || house_norm[token_end - 1u] == '\t')) token_end -= 1u;
            std::string token(house_norm.data() + token_begin, token_end - token_begin);
            append_unique_string_cpp(values, token);
            token_begin = index + 1u;
        }
    }
    return values;
}

static bool normalized_house_matches_cpp(const std::string &record_house_norm, const std::string &query_house_norm) {
    if (record_house_norm.empty() || query_house_norm.empty()) return false;
    std::vector<std::string> record_values = normalized_house_aliases_cpp(record_house_norm);
    std::vector<std::string> query_values = normalized_house_aliases_cpp(query_house_norm);
    for (const std::string &record_value : record_values) for (const std::string &query_value : query_values) if (record_value == query_value) return true;
    return false;
}

static bool normalized_char_is_trim_cpp(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',';
}

static bool normalized_is_alnum_cpp(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

static std::string trim_normalized_span_cpp(const char *text, size_t size) {
    size_t start = 0;
    size_t end = size;
    while (start < end && normalized_char_is_trim_cpp(text[start])) start += 1;
    while (end > start && normalized_char_is_trim_cpp(text[end - 1])) end -= 1;
    return std::string(text + start, text + end);
}

static bool address_field_valid_cpp(uint32_t offset, uint32_t size, uint32_t strings_size) {
    return offset <= strings_size && size <= strings_size - offset;
}

static std::string normalized_field_cpp(const std::vector<char> &strings, uint32_t offset, uint32_t size) {
    uint32_t strings_size = (uint32_t)(strings.empty() ? 0 : strings.size() - 1u);
    if (!address_field_valid_cpp(offset, size, strings_size)) return std::string();
    return normalize_text_copy_cpp(strings.data() + offset, size);
}

static bool parse_normalized_main_address_cpp(const std::string &main_norm, std::string &street_norm, std::string &house_norm) {
    std::string trimmed = trim_normalized_span_cpp(main_norm.data(), main_norm.size());
    size_t end = trimmed.size();
    while (end > 0 && !normalized_is_alnum_cpp(trimmed[end - 1u])) end -= 1u;
    if (end == 0) return false;
    size_t start = end;
    while (start > 0 && normalized_is_alnum_cpp(trimmed[start - 1u])) start -= 1u;
    if (start == 0) return false;
    house_norm.assign(trimmed.data() + start, end - start);
    street_norm = trim_normalized_span_cpp(trimmed.data(), start);
    return !street_norm.empty() && !house_norm.empty();
}

static uint64_t address_hash_bytes(uint64_t hash, const char *data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static void address_key_hash(const std::string &street_norm, const std::string &house_norm, const std::string &place_norm, uint64_t *hash1, uint64_t *hash2) {
    uint64_t h1 = 1469598103934665603ull;
    uint64_t h2 = 1099511628211ull ^ 0x9e3779b97f4a7c15ull;
    h1 = address_hash_bytes(h1, "A", 1);
    h1 = address_hash_bytes(h1, street_norm.data(), street_norm.size());
    h1 = address_hash_bytes(h1, "\x1f", 1);
    h1 = address_hash_bytes(h1, house_norm.data(), house_norm.size());
    h1 = address_hash_bytes(h1, "\x1e", 1);
    h1 = address_hash_bytes(h1, place_norm.data(), place_norm.size());
    h2 = address_hash_bytes(h2, "B", 1);
    h2 = address_hash_bytes(h2, place_norm.data(), place_norm.size());
    h2 = address_hash_bytes(h2, "\x1e", 1);
    h2 = address_hash_bytes(h2, house_norm.data(), house_norm.size());
    h2 = address_hash_bytes(h2, "\x1f", 1);
    h2 = address_hash_bytes(h2, street_norm.data(), street_norm.size());
    *hash1 = h1;
    *hash2 = h2;
}

static int address_index_hash_compare(uint64_t left1, uint64_t left2, uint64_t right1, uint64_t right2) {
    if (left1 < right1) return -1;
    if (left1 > right1) return 1;
    if (left2 < right2) return -1;
    if (left2 > right2) return 1;
    return 0;
}

static void collect_record_place_norms_cpp(const std::vector<char> &strings, const OsmrteAddressRecord &record, std::vector<std::string> &places) {
    append_unique_string_cpp(places, normalized_field_cpp(strings, record.city_offset, record.city_size));
    append_unique_string_cpp(places, normalized_field_cpp(strings, record.suburb_offset, record.suburb_size));
    append_unique_string_cpp(places, normalized_field_cpp(strings, record.postcode_offset, record.postcode_size));
}

static void append_address_index_key_variants(std::vector<RteGpuAddressIndexEntry> &entries, const OsmrteAddressRecord &record, const std::string &main_norm, const std::string &house_norm, const std::string &place_norm) {
    if (main_norm.empty() || house_norm.empty()) return;
    std::vector<std::string> main_aliases = normalized_aliases_cpp(main_norm);
    std::vector<std::string> house_aliases = normalized_house_aliases_cpp(house_norm);
    std::vector<std::string> place_aliases;
    if (place_norm.empty()) place_aliases.push_back(std::string());
    else place_aliases = normalized_place_aliases_cpp(place_norm);
    for (const std::string &main_alias : main_aliases) {
        for (const std::string &house_alias : house_aliases) {
            for (const std::string &place_alias : place_aliases) {
                RteGpuAddressIndexEntry entry;
                address_key_hash(main_alias, house_alias, place_alias, &entry.hash1, &entry.hash2);
                entry.lat_e7 = record.lat_e7;
                entry.lon_e7 = record.lon_e7;
                entry.flags = record.flags;
                entry.count = 1;
                entries.push_back(entry);
            }
        }
    }
}

static void write_address_index_header_bytes(const RteGpuAddressIndexHeader &header, unsigned char out[RTEGPU_ADDRESS_INDEX_HEADER_SIZE]) {
    std::memset(out, 0, RTEGPU_ADDRESS_INDEX_HEADER_SIZE);
    std::memcpy(out, RTEGPU_ADDRESS_INDEX_MAGIC, 8);
    rtegpu_write_u32(out + 8, 1);
    rtegpu_write_u32(out + 12, RTEGPU_ADDRESS_INDEX_HEADER_SIZE);
    rtegpu_write_u32(out + 16, RTEGPU_ADDRESS_INDEX_ENTRY_SIZE);
    rtegpu_write_u64(out + 24, header.source_size);
    rtegpu_write_u64(out + 32, header.source_mtime);
    rtegpu_write_u64(out + 40, header.address_count);
    rtegpu_write_u64(out + 48, header.entry_count);
    rtegpu_write_u64(out + 56, header.build_unix_time);
}

static RteGpuAddressIndexHeader parse_address_index_header_bytes(const unsigned char in[RTEGPU_ADDRESS_INDEX_HEADER_SIZE]) {
    if (std::memcmp(in, RTEGPU_ADDRESS_INDEX_MAGIC, 8) != 0) throw std::runtime_error("not an RTE address index");
    if (rtegpu_read_u32(in + 8) != 1 || rtegpu_read_u32(in + 12) != RTEGPU_ADDRESS_INDEX_HEADER_SIZE || rtegpu_read_u32(in + 16) != RTEGPU_ADDRESS_INDEX_ENTRY_SIZE) throw std::runtime_error("unsupported RTE address index");
    RteGpuAddressIndexHeader header;
    header.source_size = rtegpu_read_u64(in + 24);
    header.source_mtime = rtegpu_read_u64(in + 32);
    header.address_count = rtegpu_read_u64(in + 40);
    header.entry_count = rtegpu_read_u64(in + 48);
    header.build_unix_time = rtegpu_read_u64(in + 56);
    return header;
}

static void write_address_index_entry_bytes(const RteGpuAddressIndexEntry &entry, unsigned char out[RTEGPU_ADDRESS_INDEX_ENTRY_SIZE]) {
    rtegpu_write_u64(out + 0, entry.hash1);
    rtegpu_write_u64(out + 8, entry.hash2);
    rtegpu_write_u32(out + 16, (uint32_t)entry.lat_e7);
    rtegpu_write_u32(out + 20, (uint32_t)entry.lon_e7);
    rtegpu_write_u32(out + 24, entry.flags);
    rtegpu_write_u32(out + 28, entry.count);
}

static RteGpuAddressIndexEntry parse_address_index_entry_bytes(const unsigned char in[RTEGPU_ADDRESS_INDEX_ENTRY_SIZE]) {
    RteGpuAddressIndexEntry entry;
    entry.hash1 = rtegpu_read_u64(in + 0);
    entry.hash2 = rtegpu_read_u64(in + 8);
    entry.lat_e7 = rtegpu_read_i32(in + 16);
    entry.lon_e7 = rtegpu_read_i32(in + 20);
    entry.flags = rtegpu_read_u32(in + 24);
    entry.count = rtegpu_read_u32(in + 28);
    return entry;
}

static bool address_index_entry_less(const RteGpuAddressIndexEntry &left, const RteGpuAddressIndexEntry &right) {
    int cmp = address_index_hash_compare(left.hash1, left.hash2, right.hash1, right.hash2);
    return cmp < 0;
}

static void address_index_merge_entry(RteGpuAddressIndexEntry &dst, const RteGpuAddressIndexEntry &src) {
    if (dst.count > UINT32_MAX - src.count) throw std::runtime_error("too many duplicate address index entries");
    dst.count += src.count;
    if ((src.flags & 1u) != 0u && (dst.flags & 1u) == 0u) {
        dst.lat_e7 = src.lat_e7;
        dst.lon_e7 = src.lon_e7;
        dst.flags = src.flags;
    }
}

static uint64_t build_address_index_file(const char *rte_path, const char *index_path) {
    std::ifstream rte(rte_path, std::ios::binary);
    if (!rte) throw std::runtime_error("failed to open .rte file for address index build");
    OsmrteAddressSection section = read_osmrte_address_section(rte);
    std::vector<char> strings = read_osmrte_address_strings(rte, section);
    std::vector<RteGpuAddressIndexEntry> entries;
    if (section.record_count > 0) entries.reserve((size_t)std::min<uint64_t>(section.record_count, 10000000ull));
    std::vector<unsigned char> records((size_t)OSMRTE_ADDRESS_SCAN_BATCH_RECORDS * OSMRTE_ADDRESS_RECORD_SIZE);
    for (uint64_t index = 0; index < section.record_count;) {
        uint64_t remaining = section.record_count - index;
        uint32_t batch_count = remaining > OSMRTE_ADDRESS_SCAN_BATCH_RECORDS ? OSMRTE_ADDRESS_SCAN_BATCH_RECORDS : (uint32_t)remaining;
        size_t batch_size = (size_t)batch_count * OSMRTE_ADDRESS_RECORD_SIZE;
        osmrte_read_at(rte, section.offset + OSMRTE_ADDRESS_HEADER_SIZE + index * OSMRTE_ADDRESS_RECORD_SIZE, records.data(), batch_size);
        for (uint32_t batch_index = 0; batch_index < batch_count; ++batch_index) {
            OsmrteAddressRecord record = parse_osmrte_address_record(records.data() + (size_t)batch_index * OSMRTE_ADDRESS_RECORD_SIZE);
            std::string street_norm = normalized_field_cpp(strings, record.street_offset, record.street_size);
            std::string house_norm = normalized_field_cpp(strings, record.housenumber_offset, record.housenumber_size);
            if (house_norm.empty()) continue;
            std::vector<std::string> places;
            collect_record_place_norms_cpp(strings, record, places);
            append_address_index_key_variants(entries, record, street_norm, house_norm, std::string());
            for (const std::string &place_norm : places) append_address_index_key_variants(entries, record, street_norm, house_norm, place_norm);
            for (const std::string &main_place_norm : places) {
                append_address_index_key_variants(entries, record, main_place_norm, house_norm, std::string());
                for (const std::string &place_norm : places) {
                    if (main_place_norm != place_norm) append_address_index_key_variants(entries, record, main_place_norm, house_norm, place_norm);
                }
            }
        }
        index += batch_count;
        if (index % 1000000ull == 0 || index == section.record_count) std::fprintf(stderr, "rte-gpu-route: address_index_build %llu/%llu\n", (unsigned long long)index, (unsigned long long)section.record_count);
    }
    std::sort(entries.begin(), entries.end(), address_index_entry_less);
    size_t write_index = 0;
    for (size_t read_index = 0; read_index < entries.size(); ++read_index) {
        if (write_index != 0 && address_index_hash_compare(entries[write_index - 1u].hash1, entries[write_index - 1u].hash2, entries[read_index].hash1, entries[read_index].hash2) == 0) {
            address_index_merge_entry(entries[write_index - 1u], entries[read_index]);
        } else {
            if (write_index != read_index) entries[write_index] = entries[read_index];
            write_index += 1u;
        }
    }
    entries.resize(write_index);
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    if (!rte_source_stat(rte_path, &source_size, &source_mtime)) throw std::runtime_error("failed to stat .rte file for address index build");
    RteGpuAddressIndexHeader header;
    header.source_size = source_size;
    header.source_mtime = source_mtime;
    header.address_count = section.record_count;
    header.entry_count = entries.size();
    header.build_unix_time = (uint64_t)std::time(nullptr);
    std::ofstream out(index_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("failed to open address index output");
    unsigned char header_bytes[RTEGPU_ADDRESS_INDEX_HEADER_SIZE];
    write_address_index_header_bytes(header, header_bytes);
    out.write((const char *)header_bytes, sizeof(header_bytes));
    unsigned char entry_bytes[RTEGPU_ADDRESS_INDEX_ENTRY_SIZE];
    for (const RteGpuAddressIndexEntry &entry : entries) {
        write_address_index_entry_bytes(entry, entry_bytes);
        out.write((const char *)entry_bytes, sizeof(entry_bytes));
    }
    out.close();
    if (!out) throw std::runtime_error("failed while writing address index");
    return header.entry_count;
}

static RteGpuAddressIndexEntry read_address_index_entry(std::ifstream &file, uint64_t entry_index) {
    unsigned char bytes[RTEGPU_ADDRESS_INDEX_ENTRY_SIZE];
    osmrte_read_at(file, RTEGPU_ADDRESS_INDEX_HEADER_SIZE + entry_index * RTEGPU_ADDRESS_INDEX_ENTRY_SIZE, bytes, sizeof(bytes));
    return parse_address_index_entry_bytes(bytes);
}

static bool lookup_address_index_key(std::ifstream &index, uint64_t entry_count, const std::string &main_norm, const std::string &house_norm, const std::string &place_norm, OsmrteResolvedAddress &resolved) {
    uint64_t hash1 = 0;
    uint64_t hash2 = 0;
    address_key_hash(main_norm, house_norm, place_norm, &hash1, &hash2);
    uint64_t left = 0;
    uint64_t right = entry_count;
    while (left < right) {
        uint64_t mid = left + (right - left) / 2u;
        RteGpuAddressIndexEntry entry = read_address_index_entry(index, mid);
        if (address_index_hash_compare(entry.hash1, entry.hash2, hash1, hash2) < 0) left = mid + 1u;
        else right = mid;
    }
    if (left >= entry_count) return false;
    RteGpuAddressIndexEntry entry = read_address_index_entry(index, left);
    if (address_index_hash_compare(entry.hash1, entry.hash2, hash1, hash2) != 0 || entry.count == 0) return false;
    resolved.match_count = entry.count;
    resolved.record.lat_e7 = entry.lat_e7;
    resolved.record.lon_e7 = entry.lon_e7;
    resolved.record.flags = entry.flags;
    resolved.found = true;
    return true;
}

static bool lookup_address_index_one(const char *rte_path, const char *index_path, const OsmrteAddressNeedle &needle, OsmrteResolvedAddress &resolved, uint64_t *entry_count_out) {
    if (!needle.active || !needle.main_parsed) return false;
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    if (!rte_source_stat(rte_path, &source_size, &source_mtime)) return false;
    std::ifstream index(index_path, std::ios::binary);
    if (!index) return false;
    unsigned char header_bytes[RTEGPU_ADDRESS_INDEX_HEADER_SIZE];
    try {
        osmrte_read_at(index, 0, header_bytes, sizeof(header_bytes));
        RteGpuAddressIndexHeader header = parse_address_index_header_bytes(header_bytes);
        if (entry_count_out != nullptr) *entry_count_out = header.entry_count;
        if (header.source_size != source_size || header.source_mtime != source_mtime || header.entry_count == 0) return false;
        std::vector<std::string> main_aliases = normalized_aliases_cpp(needle.main_name_norm);
        std::vector<std::string> place_aliases;
        if (needle.place_norm.empty()) place_aliases.push_back(std::string());
        else place_aliases = normalized_place_aliases_cpp(needle.place_norm);
        for (const std::string &main_alias : main_aliases) {
            for (const std::string &place_alias : place_aliases) {
                if (lookup_address_index_key(index, header.entry_count, main_alias, needle.house_norm, place_alias, resolved)) return true;
            }
        }
        if (!needle.place_norm.empty()) {
            for (const std::string &main_alias : main_aliases) {
                if (lookup_address_index_key(index, header.entry_count, main_alias, needle.house_norm, std::string(), resolved)) return true;
            }
        }
        return false;
    } catch (const std::exception &) {
        return false;
    }
}

static bool resolve_address_pair_with_index(QueryOptions &options, const OsmrteAddressNeedle &from_needle, const OsmrteAddressNeedle &to_needle, OsmrteResolvedAddress &from, OsmrteResolvedAddress &to) {
    std::string index_path = default_address_index_path(options);
    if (index_path.empty()) return false;
    options.address_index_checked = true;
    uint64_t entry_count = 0;
    bool from_ok = !from_needle.active || lookup_address_index_one(options.rte_path, index_path.c_str(), from_needle, from, &entry_count);
    if (entry_count != 0) options.address_index_entries = entry_count;
    bool to_ok = !to_needle.active || lookup_address_index_one(options.rte_path, index_path.c_str(), to_needle, to, &entry_count);
    if (entry_count != 0) options.address_index_entries = entry_count;
    if (from_ok && to_ok) {
        options.address_index_used = true;
        options.address_index_status = "hit";
        return true;
    }
    options.address_index_status = "fallback_scan";
    return false;
}

static bool normalized_place_matches_cpp(const std::vector<char> &strings, uint32_t offset, uint32_t size, const std::string &place_norm) {
    if (place_norm.empty()) return true;
    std::string field_norm = normalized_field_cpp(strings, offset, size);
    return normalized_place_strings_match_tolerant_cpp(field_norm, place_norm);
}

static bool normalized_address_place_matches_cpp(const std::vector<char> &strings, const OsmrteAddressRecord &record, const std::string &place_norm) {
    if (place_norm.empty()) return true;
    if (normalized_place_matches_cpp(strings, record.city_offset, record.city_size, place_norm)) return true;
    if (normalized_place_matches_cpp(strings, record.suburb_offset, record.suburb_size, place_norm)) return true;
    if (normalized_place_matches_cpp(strings, record.postcode_offset, record.postcode_size, place_norm)) return true;
    return false;
}

static bool normalized_record_has_place_cpp(const std::vector<char> &strings, const OsmrteAddressRecord &record, const std::string &place_norm) {
    if (place_norm.empty()) return false;
    if (normalized_place_matches_cpp(strings, record.city_offset, record.city_size, place_norm)) return true;
    if (normalized_place_matches_cpp(strings, record.suburb_offset, record.suburb_size, place_norm)) return true;
    if (normalized_place_matches_cpp(strings, record.postcode_offset, record.postcode_size, place_norm)) return true;
    return false;
}

static uint32_t normalized_street_house_match_score_cpp(const std::vector<char> &strings, const OsmrteAddressRecord &record, const OsmrteAddressNeedle &needle) {
    if (!needle.main_parsed) return 0;
    std::string street_norm = normalized_field_cpp(strings, record.street_offset, record.street_size);
    std::string house_norm = normalized_field_cpp(strings, record.housenumber_offset, record.housenumber_size);
    if (!normalized_house_matches_cpp(house_norm, needle.house_norm)) return 0;
    if (!street_norm.empty() && normalized_strings_match_tolerant_cpp(street_norm, needle.main_name_norm)) return 120;
    if (!street_norm.empty() && normalized_strings_match_fuzzy_cpp(street_norm, needle.main_name_norm)) return 110;
    if (normalized_record_has_place_cpp(strings, record, needle.main_name_norm)) return 60;
    return 0;
}

static uint32_t normalized_record_place_affinity_score_cpp(const std::vector<char> &strings, const OsmrteAddressRecord &record, const std::string &place_norm) {
    if (place_norm.empty()) return 0;
    return normalized_record_has_place_cpp(strings, record, place_norm) ? 30u : 0u;
}

static void append_address_candidate(OsmrteResolvedAddress &resolved, const OsmrteAddressRecord &record) {
    for (const OsmrteAddressRecord &candidate : resolved.candidates) {
        if (candidate.lat_e7 == record.lat_e7 && candidate.lon_e7 == record.lon_e7) return;
    }
    if (resolved.candidates.size() < RTEGPU_MAX_ADDRESS_CANDIDATES) resolved.candidates.push_back(record);
}

static void update_resolved_address_candidate(OsmrteResolvedAddress &resolved, const OsmrteAddressRecord &record, uint32_t match_score) {
    if (!resolved.found || match_score > resolved.match_score) {
        resolved.record = record;
        resolved.match_score = match_score;
        resolved.found = true;
        resolved.candidates.clear();
        append_address_candidate(resolved, record);
        return;
    }
    if (match_score == resolved.match_score) {
        if ((record.flags & 1u) != 0u && (resolved.record.flags & 1u) == 0u) resolved.record = record;
        append_address_candidate(resolved, record);
    }
}

static void split_normalized_address_query_cpp(const std::string &query_norm, std::string &main_norm, std::string &place_norm) {
    size_t comma = query_norm.find(',');
    if (comma == std::string::npos) {
        main_norm = trim_normalized_span_cpp(query_norm.data(), query_norm.size());
        place_norm.clear();
    } else {
        main_norm = trim_normalized_span_cpp(query_norm.data(), comma);
        place_norm = trim_normalized_span_cpp(query_norm.data() + comma + 1u, query_norm.size() - comma - 1u);
    }
}

static void init_osmrte_address_needle(OsmrteAddressNeedle &needle, const char *query) {
    std::string query_norm = normalize_text_copy_cpp(query, std::strlen(query));
    split_normalized_address_query_cpp(query_norm, needle.main_norm, needle.place_norm);
    needle.preferred_place_norm = needle.place_norm;
    needle.main_parsed = parse_normalized_main_address_cpp(needle.main_norm, needle.main_name_norm, needle.house_norm);
    needle.active = true;
}

static void update_osmrte_address_match(const std::vector<char> &strings, const OsmrteAddressRecord &record, OsmrteAddressNeedle &needle) {
    if (!needle.active) return;
    uint32_t street_score = normalized_street_house_match_score_cpp(strings, record, needle);
    if (street_score != 0 && normalized_address_place_matches_cpp(strings, record, needle.place_norm)) {
        uint32_t match_score = street_score + normalized_record_place_affinity_score_cpp(strings, record, needle.preferred_place_norm);
        needle.resolved.match_count += 1;
        update_resolved_address_candidate(needle.resolved, record, match_score);
    }
}

static void resolve_osmrte_address_pair(std::ifstream &file, const OsmrteAddressSection &section, const std::vector<char> &strings, OsmrteAddressNeedle &from, OsmrteAddressNeedle &to) {
    std::vector<unsigned char> records((size_t)OSMRTE_ADDRESS_SCAN_BATCH_RECORDS * OSMRTE_ADDRESS_RECORD_SIZE);
    for (uint64_t index = 0; index < section.record_count;) {
        uint64_t remaining = section.record_count - index;
        uint32_t batch_count = remaining > OSMRTE_ADDRESS_SCAN_BATCH_RECORDS ? OSMRTE_ADDRESS_SCAN_BATCH_RECORDS : (uint32_t)remaining;
        size_t batch_size = (size_t)batch_count * OSMRTE_ADDRESS_RECORD_SIZE;
        osmrte_read_at(file, section.offset + OSMRTE_ADDRESS_HEADER_SIZE + index * OSMRTE_ADDRESS_RECORD_SIZE, records.data(), batch_size);
        for (uint32_t batch_index = 0; batch_index < batch_count; ++batch_index) {
            OsmrteAddressRecord record = parse_osmrte_address_record(records.data() + (size_t)batch_index * OSMRTE_ADDRESS_RECORD_SIZE);
            update_osmrte_address_match(strings, record, from);
            update_osmrte_address_match(strings, record, to);
        }
        index += batch_count;
    }
}

static uint32_t choose_osmrte_address_threads(uint64_t record_count, uint32_t requested_threads) {
    uint32_t hardware_threads = std::thread::hardware_concurrency();
    uint32_t thread_count = requested_threads != 0 ? requested_threads : hardware_threads;
    if (thread_count == 0) thread_count = 1;
    if (thread_count > 16u) thread_count = 16u;
    if (record_count != 0 && thread_count > record_count) thread_count = (uint32_t)record_count;
    return thread_count;
}

static void merge_osmrte_address_needle(OsmrteAddressNeedle &dst, const OsmrteAddressNeedle &src) {
    dst.resolved.match_count += src.resolved.match_count;
    if (src.resolved.found && (!dst.resolved.found || src.resolved.match_score > dst.resolved.match_score)) {
        dst.resolved.record = src.resolved.record;
        dst.resolved.match_score = src.resolved.match_score;
        dst.resolved.found = true;
        dst.resolved.candidates = src.resolved.candidates;
    } else if (src.resolved.found && src.resolved.match_score == dst.resolved.match_score) {
        if ((src.resolved.record.flags & 1u) != 0u && (dst.resolved.record.flags & 1u) == 0u) dst.resolved.record = src.resolved.record;
        for (const OsmrteAddressRecord &candidate : src.resolved.candidates) append_address_candidate(dst.resolved, candidate);
    }
}

static void store_query_address_candidates(const OsmrteResolvedAddress &resolved, std::vector<int32_t> &latitudes, std::vector<int32_t> &longitudes) {
    latitudes.clear();
    longitudes.clear();
    if (!resolved.found) return;
    if (resolved.candidates.empty()) {
        latitudes.push_back(resolved.record.lat_e7);
        longitudes.push_back(resolved.record.lon_e7);
        return;
    }
    for (const OsmrteAddressRecord &candidate : resolved.candidates) {
        latitudes.push_back(candidate.lat_e7);
        longitudes.push_back(candidate.lon_e7);
    }
}

static void resolve_osmrte_address_pair_range(const char *path, const OsmrteAddressSection &section, const std::vector<char> &strings, const OsmrteAddressNeedle &from_template, const OsmrteAddressNeedle &to_template, uint64_t begin, uint64_t end, OsmrteAddressNeedle *from_out, OsmrteAddressNeedle *to_out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("failed to open .rte file in address worker");
    OsmrteAddressNeedle from = from_template;
    OsmrteAddressNeedle to = to_template;
    std::vector<unsigned char> records((size_t)OSMRTE_ADDRESS_SCAN_BATCH_RECORDS * OSMRTE_ADDRESS_RECORD_SIZE);
    for (uint64_t index = begin; index < end;) {
        uint64_t remaining = end - index;
        uint32_t batch_count = remaining > OSMRTE_ADDRESS_SCAN_BATCH_RECORDS ? OSMRTE_ADDRESS_SCAN_BATCH_RECORDS : (uint32_t)remaining;
        size_t batch_size = (size_t)batch_count * OSMRTE_ADDRESS_RECORD_SIZE;
        osmrte_read_at(file, section.offset + OSMRTE_ADDRESS_HEADER_SIZE + index * OSMRTE_ADDRESS_RECORD_SIZE, records.data(), batch_size);
        for (uint32_t batch_index = 0; batch_index < batch_count; ++batch_index) {
            OsmrteAddressRecord record = parse_osmrte_address_record(records.data() + (size_t)batch_index * OSMRTE_ADDRESS_RECORD_SIZE);
            update_osmrte_address_match(strings, record, from);
            update_osmrte_address_match(strings, record, to);
        }
        index += batch_count;
    }
    *from_out = from;
    *to_out = to;
}

static void resolve_osmrte_address_pair_parallel(const char *path, const OsmrteAddressSection &section, const std::vector<char> &strings, OsmrteAddressNeedle &from, OsmrteAddressNeedle &to, uint32_t requested_threads) {
    uint32_t thread_count = choose_osmrte_address_threads(section.record_count, requested_threads);
    if (thread_count <= 1u) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("failed to open .rte file for address lookup");
        resolve_osmrte_address_pair(file, section, strings, from, to);
        return;
    }
    std::vector<OsmrteAddressNeedle> from_results(thread_count);
    std::vector<OsmrteAddressNeedle> to_results(thread_count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    uint64_t chunk = (section.record_count + thread_count - 1u) / thread_count;
    for (uint32_t i = 0; i < thread_count; ++i) {
        uint64_t begin = (uint64_t)i * chunk;
        uint64_t end = std::min<uint64_t>(section.record_count, begin + chunk);
        threads.emplace_back(resolve_osmrte_address_pair_range, path, std::cref(section), std::cref(strings), std::cref(from), std::cref(to), begin, end, &from_results[i], &to_results[i]);
    }
    for (std::thread &thread : threads) thread.join();
    from.resolved = OsmrteResolvedAddress();
    to.resolved = OsmrteResolvedAddress();
    for (uint32_t i = 0; i < thread_count; ++i) {
        merge_osmrte_address_needle(from, from_results[i]);
        merge_osmrte_address_needle(to, to_results[i]);
    }
}

static bool relax_address_needle_place(OsmrteAddressNeedle &needle) {
    if (!needle.active || needle.resolved.found || needle.place_norm.empty()) return false;
    needle.place_norm.clear();
    needle.resolved = OsmrteResolvedAddress();
    return true;
}

static void resolve_address_queries(QueryOptions &options) {
    if (options.rte_path == nullptr) return;
    int32_t lat = 0;
    int32_t lon = 0;
    if (options.from_text != nullptr && rtegpu_parse_latlon(options.from_text, &lat, &lon)) {
        options.from_lat = lat;
        options.from_lon = lon;
        options.have_from_coord = true;
        options.from_match_count = 1;
    }
    if (options.to_text != nullptr && rtegpu_parse_latlon(options.to_text, &lat, &lon)) {
        options.to_lat = lat;
        options.to_lon = lon;
        options.have_to_coord = true;
        options.to_match_count = 1;
    }
    if (options.have_from_coord && options.have_to_coord) {
        options.resolved_addresses = true;
        return;
    }
    OsmrteAddressNeedle from_needle;
    OsmrteAddressNeedle to_needle;
    if (!options.have_from_coord) init_osmrte_address_needle(from_needle, options.from_text);
    if (!options.have_to_coord) init_osmrte_address_needle(to_needle, options.to_text);
    OsmrteResolvedAddress from_index;
    OsmrteResolvedAddress to_index;
    if (resolve_address_pair_with_index(options, from_needle, to_needle, from_index, to_index)) {
        if (!options.have_from_coord) {
            options.from_match_count = from_index.match_count;
            options.from_lat = from_index.record.lat_e7;
            options.from_lon = from_index.record.lon_e7;
            store_query_address_candidates(from_index, options.from_candidate_lat, options.from_candidate_lon);
            options.have_from_coord = true;
        }
        if (!options.have_to_coord) {
            options.to_match_count = to_index.match_count;
            options.to_lat = to_index.record.lat_e7;
            options.to_lon = to_index.record.lon_e7;
            store_query_address_candidates(to_index, options.to_candidate_lat, options.to_candidate_lon);
            options.have_to_coord = true;
        }
        options.resolved_addresses = true;
        return;
    }
    if (options.address_index_only) throw std::runtime_error("address not found in current address index");
    std::ifstream rte(options.rte_path, std::ios::binary);
    if (!rte) throw std::runtime_error("failed to open .rte file for address lookup");
    OsmrteAddressSection section = read_osmrte_address_section(rte);
    std::vector<char> strings = read_osmrte_address_strings(rte, section);
    rte.close();
    options.address_threads = choose_osmrte_address_threads(section.record_count, options.address_threads);
    resolve_osmrte_address_pair_parallel(options.rte_path, section, strings, from_needle, to_needle, options.address_threads);
    bool relaxed_from_place = relax_address_needle_place(from_needle);
    bool relaxed_to_place = relax_address_needle_place(to_needle);
    if (relaxed_from_place || relaxed_to_place) {
        OsmrteResolvedAddress saved_from = from_needle.resolved;
        OsmrteResolvedAddress saved_to = to_needle.resolved;
        if (!relaxed_from_place) from_needle.active = false;
        if (!relaxed_to_place) to_needle.active = false;
        resolve_osmrte_address_pair_parallel(options.rte_path, section, strings, from_needle, to_needle, options.address_threads);
        if (!relaxed_from_place) {
            from_needle.active = true;
            from_needle.resolved = saved_from;
        }
        if (!relaxed_to_place) {
            to_needle.active = true;
            to_needle.resolved = saved_to;
        }
        options.address_index_status = "fallback_scan_relaxed_place";
    }
    if (!options.have_from_coord) {
        OsmrteResolvedAddress from = from_needle.resolved;
        options.from_match_count = from.match_count;
        if (!from.found) throw std::runtime_error("from address not found in .rte address section");
        options.from_lat = from.record.lat_e7;
        options.from_lon = from.record.lon_e7;
        store_query_address_candidates(from, options.from_candidate_lat, options.from_candidate_lon);
        options.have_from_coord = true;
    }
    if (!options.have_to_coord) {
        OsmrteResolvedAddress to = to_needle.resolved;
        options.to_match_count = to.match_count;
        if (!to.found) throw std::runtime_error("to address not found in .rte address section");
        options.to_lat = to.record.lat_e7;
        options.to_lon = to.record.lon_e7;
        store_query_address_candidates(to, options.to_candidate_lat, options.to_candidate_lon);
        options.have_to_coord = true;
    }
    options.resolved_addresses = true;
}

static void initialize_origin(const RteGpuPack &pack, const QueryOptions &options, std::vector<uint32_t> &arrival, std::vector<uint8_t> &rides, uint32_t *candidate_origin_stops) {
    uint32_t stop_count = (uint32_t)pack.header.stop_count;
    arrival.assign(stop_count, RTEGPU_INF_TIME);
    rides.assign(stop_count, 0);
    *candidate_origin_stops = 0;
    if (options.from_stop != UINT32_MAX) {
        if (options.from_stop >= stop_count) throw std::runtime_error("from stop index out of range");
        arrival[options.from_stop] = options.depart_seconds;
        *candidate_origin_stops = 1;
        return;
    }
    const std::vector<int32_t> *candidate_lat = options.from_candidate_lat.empty() ? nullptr : &options.from_candidate_lat;
    const std::vector<int32_t> *candidate_lon = options.from_candidate_lon.empty() ? nullptr : &options.from_candidate_lon;
    uint32_t candidate_count = candidate_lat != nullptr && candidate_lon != nullptr && candidate_lat->size() == candidate_lon->size() ? (uint32_t)candidate_lat->size() : 1u;
    for (uint32_t i = 0; i < stop_count; ++i) {
        for (uint32_t candidate = 0; candidate < candidate_count; ++candidate) {
            int32_t from_lat = candidate_lat != nullptr ? (*candidate_lat)[candidate] : options.from_lat;
            int32_t from_lon = candidate_lon != nullptr ? (*candidate_lon)[candidate] : options.from_lon;
            uint32_t meters = rtegpu_direct_distance_m(from_lat, from_lon, pack.stop_lat[i], pack.stop_lon[i]);
            if (meters > RTEGPU_ACCESS_WALK_M) continue;
            uint32_t walk_sec = rtegpu_walking_seconds(meters);
            uint32_t candidate_arrival = options.depart_seconds + walk_sec;
            if (candidate_arrival < arrival[i]) {
                if (arrival[i] == RTEGPU_INF_TIME) *candidate_origin_stops += 1;
                arrival[i] = candidate_arrival;
            }
        }
    }
}

static uint32_t finish_score(uint32_t arrival, uint32_t egress_walk_sec) {
    uint64_t score = (uint64_t)arrival + (uint64_t)egress_walk_sec * RTEGPU_EGRESS_WALK_SCORE_WEIGHT;
    return score > UINT32_MAX ? UINT32_MAX : (uint32_t)score;
}

static uint32_t select_destination(const RteGpuPack &pack, const QueryOptions &options, const std::vector<uint32_t> &arrival, uint32_t *best_stop, uint32_t *candidate_destination_stops, uint32_t *best_walk_m) {
    uint32_t stop_count = (uint32_t)pack.header.stop_count;
    uint32_t best_arrival = RTEGPU_INF_TIME;
    uint32_t best_score = RTEGPU_INF_TIME;
    *best_stop = UINT32_MAX;
    *best_walk_m = 0;
    *candidate_destination_stops = 0;
    if (options.to_stop != UINT32_MAX) {
        if (options.to_stop >= stop_count) throw std::runtime_error("to stop index out of range");
        *best_stop = options.to_stop;
        return arrival[options.to_stop];
    }
    const std::vector<int32_t> *candidate_lat = options.to_candidate_lat.empty() ? nullptr : &options.to_candidate_lat;
    const std::vector<int32_t> *candidate_lon = options.to_candidate_lon.empty() ? nullptr : &options.to_candidate_lon;
    uint32_t candidate_count = candidate_lat != nullptr && candidate_lon != nullptr && candidate_lat->size() == candidate_lon->size() ? (uint32_t)candidate_lat->size() : 1u;
    for (uint32_t i = 0; i < stop_count; ++i) {
        for (uint32_t candidate = 0; candidate < candidate_count; ++candidate) {
            int32_t to_lat = candidate_lat != nullptr ? (*candidate_lat)[candidate] : options.to_lat;
            int32_t to_lon = candidate_lon != nullptr ? (*candidate_lon)[candidate] : options.to_lon;
            uint32_t meters = rtegpu_direct_distance_m(to_lat, to_lon, pack.stop_lat[i], pack.stop_lon[i]);
            if (meters <= RTEGPU_EGRESS_WALK_M) *candidate_destination_stops += 1;
            if (arrival[i] == RTEGPU_INF_TIME || meters > RTEGPU_EGRESS_WALK_M) continue;
            uint32_t walk_sec = rtegpu_walking_seconds(meters);
            uint32_t total_arrival = arrival[i] + walk_sec;
            uint32_t score = finish_score(arrival[i], walk_sec);
            if (score < best_score || (score == best_score && total_arrival < best_arrival)) {
                best_score = score;
                best_arrival = total_arrival;
                *best_stop = i;
                *best_walk_m = meters;
            }
        }
    }
    return best_arrival;
}

static void pred_host_init(RteGpuPredHost &pred, uint32_t stop_count) {
    pred.kind.assign(stop_count, RTEGPU_STATE_NONE);
    pred.previous.assign(stop_count, RTEGPU_NO_INDEX);
    pred.trip.assign(stop_count, RTEGPU_NO_INDEX);
    pred.route.assign(stop_count, RTEGPU_NO_INDEX);
    pred.mode.assign(stop_count, 0);
    pred.board.assign(stop_count, RTEGPU_NO_INDEX);
    pred.alight.assign(stop_count, RTEGPU_NO_INDEX);
    pred.departure.assign(stop_count, 0);
    pred.arrival.assign(stop_count, 0);
    pred.walk_m.assign(stop_count, 0);
}

static void initialize_origin_predecessors(const RteGpuPack &pack, const QueryOptions &options, const std::vector<uint32_t> &initial_arrival, RteGpuPredHost &pred) {
    uint32_t stop_count = (uint32_t)pack.header.stop_count;
    pred_host_init(pred, stop_count);
    const std::vector<int32_t> *candidate_lat = options.from_candidate_lat.empty() ? nullptr : &options.from_candidate_lat;
    const std::vector<int32_t> *candidate_lon = options.from_candidate_lon.empty() ? nullptr : &options.from_candidate_lon;
    uint32_t candidate_count = candidate_lat != nullptr && candidate_lon != nullptr && candidate_lat->size() == candidate_lon->size() ? (uint32_t)candidate_lat->size() : 1u;
    for (uint32_t i = 0; i < stop_count; ++i) {
        if (initial_arrival[i] == RTEGPU_INF_TIME) continue;
        uint32_t walk_m = 0;
        if (options.from_stop == UINT32_MAX) {
            walk_m = UINT32_MAX;
            for (uint32_t candidate = 0; candidate < candidate_count; ++candidate) {
                int32_t from_lat = candidate_lat != nullptr ? (*candidate_lat)[candidate] : options.from_lat;
                int32_t from_lon = candidate_lon != nullptr ? (*candidate_lon)[candidate] : options.from_lon;
                uint32_t meters = rtegpu_direct_distance_m(from_lat, from_lon, pack.stop_lat[i], pack.stop_lon[i]);
                uint32_t walk_sec = rtegpu_walking_seconds(meters);
                if (meters <= RTEGPU_ACCESS_WALK_M && options.depart_seconds + walk_sec == initial_arrival[i] && meters < walk_m) walk_m = meters;
            }
            if (walk_m == UINT32_MAX) walk_m = rtegpu_direct_distance_m(options.from_lat, options.from_lon, pack.stop_lat[i], pack.stop_lon[i]);
        }
        pred.kind[i] = RTEGPU_STATE_ORIGIN_WALK;
        pred.previous[i] = RTEGPU_NO_INDEX;
        pred.trip[i] = RTEGPU_NO_INDEX;
        pred.route[i] = RTEGPU_NO_INDEX;
        pred.mode[i] = 0;
        pred.board[i] = i;
        pred.alight[i] = i;
        pred.departure[i] = options.depart_seconds;
        pred.arrival[i] = initial_arrival[i];
        pred.walk_m[i] = walk_m;
    }
}

static const char *mode_name(uint32_t mode) {
    if (mode == 1u) return "tram";
    if (mode == 2u) return "subway";
    if (mode == 3u) return "rail";
    if (mode == 4u) return "bus";
    if (mode == 5u) return "transit";
    return "transit";
}

static void color_start(const QueryOptions &options, const char *code) {
    if (options.use_color) std::printf("%s", code);
}

static void color_end(const QueryOptions &options) {
    if (options.use_color) std::printf("\033[0m");
}

static void print_colored_cstr(const QueryOptions &options, const char *code, const char *text) {
    color_start(options, code);
    std::printf("%s", text);
    color_end(options);
}

static void print_pack_string(const RteGpuPack &pack, uint32_t offset, uint32_t size) {
    if (offset <= pack.strings.size() && size <= pack.strings.size() - offset && size != 0) std::fwrite(pack.strings.data() + offset, 1, size, stdout);
    else std::printf("unknown");
}

static void json_write_escaped(const char *data, size_t size) {
    std::putchar('"');
    for (size_t i = 0; i < size; ++i) {
        unsigned char ch = (unsigned char)data[i];
        if (ch == '"' || ch == '\\') { std::putchar('\\'); std::putchar(ch); }
        else if (ch == '\n') std::printf("\\n");
        else if (ch == '\r') std::printf("\\r");
        else if (ch == '\t') std::printf("\\t");
        else if (ch < 0x20u) std::printf("\\u%04x", (unsigned)ch);
        else std::putchar(ch);
    }
    std::putchar('"');
}

static void json_write_cstr(const char *text) {
    json_write_escaped(text, std::strlen(text));
}

static void json_write_pack_string_or_null(const RteGpuPack &pack, uint32_t offset, uint32_t size) {
    if (offset <= pack.strings.size() && size <= pack.strings.size() - offset && size != 0) json_write_escaped(pack.strings.data() + offset, size);
    else std::printf("null");
}

static void print_stop_name(const RteGpuPack &pack, uint32_t stop) {
    if (stop < pack.stop_name_offset.size() && stop < pack.stop_name_size.size()) print_pack_string(pack, pack.stop_name_offset[stop], pack.stop_name_size[stop]);
    else std::printf("stop %u", stop);
}

static void print_stop_name_colored(const RteGpuPack &pack, const QueryOptions &options, uint32_t stop) {
    color_start(options, "\033[1;36m");
    print_stop_name(pack, stop);
    color_end(options);
}

static void json_write_stop_name_or_null(const RteGpuPack &pack, uint32_t stop) {
    if (stop < pack.stop_name_offset.size() && stop < pack.stop_name_size.size()) json_write_pack_string_or_null(pack, pack.stop_name_offset[stop], pack.stop_name_size[stop]);
    else std::printf("null");
}

static void print_route_name(const RteGpuPack &pack, uint32_t route) {
    if (route < pack.route_short_offset.size() && route < pack.route_short_size.size() && pack.route_short_size[route] != 0) {
        print_pack_string(pack, pack.route_short_offset[route], pack.route_short_size[route]);
    } else if (route < pack.route_long_offset.size() && route < pack.route_long_size.size() && pack.route_long_size[route] != 0) {
        print_pack_string(pack, pack.route_long_offset[route], pack.route_long_size[route]);
    } else {
        std::printf("route %u", route);
    }
}

static void print_route_name_colored(const RteGpuPack &pack, const QueryOptions &options, uint32_t route) {
    color_start(options, "\033[1;33m");
    print_route_name(pack, route);
    color_end(options);
}

static void json_write_route_short_or_null(const RteGpuPack &pack, uint32_t route) {
    if (route < pack.route_short_offset.size() && route < pack.route_short_size.size()) json_write_pack_string_or_null(pack, pack.route_short_offset[route], pack.route_short_size[route]);
    else std::printf("null");
}

static void json_write_route_long_or_null(const RteGpuPack &pack, uint32_t route) {
    if (route < pack.route_long_offset.size() && route < pack.route_long_size.size()) json_write_pack_string_or_null(pack, pack.route_long_offset[route], pack.route_long_size[route]);
    else std::printf("null");
}

static void json_write_coord_fields(const char *prefix, int32_t lat_e7, int32_t lon_e7) {
    std::printf(",\"%s_lat_e7\":%d,\"%s_lon_e7\":%d,\"%s_lat\":%.7f,\"%s_lon\":%.7f", prefix, lat_e7, prefix, lon_e7, prefix, (double)lat_e7 / 10000000.0, prefix, (double)lon_e7 / 10000000.0);
}

static void json_write_stop_coord_fields(const RteGpuPack &pack, const char *prefix, uint32_t stop) {
    if (stop < pack.stop_lat.size() && stop < pack.stop_lon.size()) json_write_coord_fields(prefix, pack.stop_lat[stop], pack.stop_lon[stop]);
}

static bool build_plan_legs(const RteGpuPack &pack, const RteGpuPredHost &pred, uint32_t best_stop, std::vector<RteGpuPlanLeg> &legs) {
    std::vector<RteGpuPlanLeg> reverse;
    uint32_t current = best_stop;
    uint32_t guard = 0;
    while (current != RTEGPU_NO_INDEX && current < pack.header.stop_count && guard < pack.header.stop_count && reverse.size() < 64u) {
        guard += 1;
        uint32_t kind = pred.kind[current];
        if (kind == RTEGPU_STATE_ORIGIN_WALK) {
            RteGpuPlanLeg leg;
            leg.kind = kind;
            leg.board = current;
            leg.alight = current;
            leg.departure = pred.departure[current];
            leg.arrival = pred.arrival[current];
            leg.walk_m = pred.walk_m[current];
            reverse.push_back(leg);
            break;
        }
        if (kind != RTEGPU_STATE_VEHICLE && kind != RTEGPU_STATE_TRANSFER_WALK) return false;
        RteGpuPlanLeg leg;
        leg.kind = kind;
        leg.mode = pred.mode[current];
        leg.trip = pred.trip[current];
        leg.route = pred.route[current];
        leg.board = pred.board[current];
        leg.alight = pred.alight[current];
        leg.departure = pred.departure[current];
        leg.arrival = pred.arrival[current];
        leg.walk_m = pred.walk_m[current];
        reverse.push_back(leg);
        current = pred.previous[current];
    }
    if (reverse.empty() || reverse.back().kind != RTEGPU_STATE_ORIGIN_WALK) return false;
    legs.assign(reverse.rbegin(), reverse.rend());
    compact_plan_legs(legs);
    return true;
}

static void print_plan_text(const RteGpuPack &pack, const QueryOptions &options, const std::vector<RteGpuPlanLeg> &legs, bool plan_found, uint32_t egress_walk_m, uint32_t best_arrival) {
    if (!plan_found) {
        std::printf("plan_status: unavailable\n");
        return;
    }
    bool combine_final_walk = egress_walk_m != 0 && !legs.empty() && legs.back().kind == RTEGPU_STATE_TRANSFER_WALK;
    uint32_t display_leg_count = (uint32_t)legs.size() + ((egress_walk_m != 0 || options.to_stop == RTEGPU_NO_INDEX) ? 1u : 0u) - (combine_final_walk ? 1u : 0u);
    std::printf("plan_status: found\n");
    std::printf("plan_leg_count: %u\n", display_leg_count);
    print_colored_cstr(options, "\033[1;35m", "GPU transit plan");
    std::printf("\n");
    uint32_t visible_legs = (uint32_t)legs.size() - (combine_final_walk ? 1u : 0u);
    for (uint32_t i = 0; i < visible_legs; ++i) {
        const RteGpuPlanLeg &leg = legs[i];
        char dep[16];
        char arr[16];
        rtegpu_format_time(leg.departure, dep);
        rtegpu_format_time(leg.arrival, arr);
        if (leg.kind == RTEGPU_STATE_ORIGIN_WALK) {
            std::printf("plan_%u: walk %u m to ", i + 1u, leg.walk_m);
            print_stop_name_colored(pack, options, leg.alight);
            std::printf(" arrive %s\n", arr);
        } else if (leg.kind == RTEGPU_STATE_TRANSFER_WALK) {
            std::printf("plan_%u: walk %u m from ", i + 1u, leg.walk_m);
            print_stop_name_colored(pack, options, leg.board);
            std::printf(" at %s to ", dep);
            print_stop_name_colored(pack, options, leg.alight);
            std::printf(" arrive %s\n", arr);
        } else if (leg.kind == RTEGPU_STATE_VEHICLE) {
            std::printf("plan_%u: take ", i + 1u);
            print_colored_cstr(options, "\033[1;33m", mode_name(leg.mode));
            std::printf(" ");
            print_route_name_colored(pack, options, leg.route);
            std::printf(" from ");
            print_stop_name_colored(pack, options, leg.board);
            std::printf(" at %s to ", dep);
            print_stop_name_colored(pack, options, leg.alight);
            std::printf(" arrive %s\n", arr);
        }
    }
    if (combine_final_walk) {
        const RteGpuPlanLeg &leg = legs.back();
        char arr[16];
        char dep[16];
        rtegpu_format_time(best_arrival, arr);
        rtegpu_format_time(leg.departure, dep);
        std::printf("plan_%u: walk %u m from ", visible_legs + 1u, leg.walk_m + egress_walk_m);
        print_stop_name_colored(pack, options, leg.board);
        std::printf(" at %s to destination arrive %s\n", dep, arr);
        return;
    }
    if (egress_walk_m != 0 || options.to_stop == RTEGPU_NO_INDEX) {
        char arr[16];
        rtegpu_format_time(best_arrival, arr);
        std::printf("plan_%u: walk %u m to destination arrive %s\n", (uint32_t)legs.size() + 1u, egress_walk_m, arr);
    }
}
static void json_write_plan_leg(const RteGpuPack &pack, const QueryOptions &options, const RteGpuPlanLeg &leg) {
    if (leg.kind == RTEGPU_STATE_ORIGIN_WALK) {
        std::printf("{\"kind\":\"access_walk\",\"to_stop_index\":%u,\"to_stop\":", leg.alight);
        json_write_stop_name_or_null(pack, leg.alight);
        if (options.have_from_coord) json_write_coord_fields("from", options.from_lat, options.from_lon);
        json_write_stop_coord_fields(pack, "to_stop", leg.alight);
        std::printf(",\"walk_m\":%u,\"departure_sec\":%u,\"arrival_sec\":%u}", leg.walk_m, leg.departure, leg.arrival);
    } else if (leg.kind == RTEGPU_STATE_TRANSFER_WALK) {
        std::printf("{\"kind\":\"transfer_walk\",\"from_stop_index\":%u,\"from_stop\":", leg.board);
        json_write_stop_name_or_null(pack, leg.board);
        std::printf(",\"to_stop_index\":%u,\"to_stop\":", leg.alight);
        json_write_stop_name_or_null(pack, leg.alight);
        json_write_stop_coord_fields(pack, "from_stop", leg.board);
        json_write_stop_coord_fields(pack, "to_stop", leg.alight);
        std::printf(",\"walk_m\":%u,\"departure_sec\":%u,\"arrival_sec\":%u}", leg.walk_m, leg.departure, leg.arrival);
    } else if (leg.kind == RTEGPU_STATE_VEHICLE) {
        std::printf("{\"kind\":\"ride\",\"mode\":");
        json_write_cstr(mode_name(leg.mode));
        std::printf(",\"route_index\":%u,\"route_short_name\":", leg.route);
        json_write_route_short_or_null(pack, leg.route);
        std::printf(",\"route_long_name\":");
        json_write_route_long_or_null(pack, leg.route);
        std::printf(",\"board_stop_index\":%u,\"board_stop\":", leg.board);
        json_write_stop_name_or_null(pack, leg.board);
        std::printf(",\"alight_stop_index\":%u,\"alight_stop\":", leg.alight);
        json_write_stop_name_or_null(pack, leg.alight);
        json_write_stop_coord_fields(pack, "board_stop", leg.board);
        json_write_stop_coord_fields(pack, "alight_stop", leg.alight);
        std::printf(",\"departure_sec\":%u,\"arrival_sec\":%u}", leg.departure, leg.arrival);
    } else {
        std::printf("{\"kind\":\"unknown\"}");
    }
}

static void print_json_report(
    const RteGpuPack &pack,
    const QueryOptions &options,
    const std::vector<RteGpuPlanLeg> &legs,
    bool plan_found,
    uint32_t candidate_origin_stops,
    uint32_t candidate_destination_stops,
    uint32_t gpu_best_stop,
    uint32_t gpu_best_arrival,
    uint32_t gpu_walk_m,
    bool verify_best_match,
    uint64_t mismatch_count,
    uint32_t cpu_best_stop,
    uint32_t cpu_best_arrival,
    double address_ms,
    double load_ms,
    double copy_ms,
    double gpu_ms,
    double cpu_ms
) {
    std::printf("{\"format\":\"RTEGPU01\",\"query_kind\":\"transit_trip_scan_with_walk_transfers\"");
    std::printf(",\"resident_mode\":%s", options.interactive ? "true" : "false");
    std::printf(",\"query\":{\"depart_date\":%u,\"depart_seconds\":%u", options.depart_date, options.depart_seconds);
    if (options.from_text != nullptr) { std::printf(",\"from\":"); json_write_cstr(options.from_text); }
    if (options.to_text != nullptr) { std::printf(",\"to\":"); json_write_cstr(options.to_text); }
    if (options.from_stop != RTEGPU_NO_INDEX) std::printf(",\"from_stop_index\":%u", options.from_stop);
    if (options.to_stop != RTEGPU_NO_INDEX) std::printf(",\"to_stop_index\":%u", options.to_stop);
    if (options.have_from_coord) std::printf(",\"from_lat_e7\":%d,\"from_lon_e7\":%d", options.from_lat, options.from_lon);
    if (options.have_to_coord) std::printf(",\"to_lat_e7\":%d,\"to_lon_e7\":%d", options.to_lat, options.to_lon);
    std::printf("}");
    std::printf(",\"address_mode\":%s", options.resolved_addresses ? "true" : "false");
    if (options.resolved_addresses) std::printf(",\"from_address_matches\":%u,\"to_address_matches\":%u,\"address_threads\":%u", options.from_match_count, options.to_match_count, options.address_threads);
    std::printf(",\"address_index\":{\"status\":");
    json_write_cstr(options.address_index_status.c_str());
    std::printf(",\"used\":%s,\"entries\":%llu}", options.address_index_used ? "true" : "false", (unsigned long long)options.address_index_entries);
    std::printf(",\"counts\":{\"stops\":%llu,\"trips\":%llu,\"events\":%llu,\"transfer_edges\":%llu}", (unsigned long long)pack.header.stop_count, (unsigned long long)pack.header.trip_count, (unsigned long long)pack.header.event_count, (unsigned long long)pack.header.transfer_edge_count);
    std::printf(",\"result\":{\"status\":");
    json_write_cstr(gpu_best_arrival == RTEGPU_INF_TIME ? "unreachable" : "found");
    std::printf(",\"candidate_origin_stops\":%u,\"candidate_destination_stops\":%u,\"best_stop_index\":%u,\"best_arrival_sec\":%u,\"walk_from_stop_m\":%u}", candidate_origin_stops, candidate_destination_stops, gpu_best_stop, gpu_best_arrival, gpu_walk_m);
    std::printf(",\"verification\":{\"enabled\":%s", options.verify ? "true" : "false");
    if (options.verify) std::printf(",\"arrival_mismatches\":%llu,\"best_match\":%s,\"cpu_best_stop_index\":%u,\"cpu_best_arrival_sec\":%u", (unsigned long long)mismatch_count, verify_best_match ? "true" : "false", cpu_best_stop, cpu_best_arrival);
    std::printf("}");
    std::printf(",\"plan\":{\"status\":");
    json_write_cstr(plan_found ? "found" : "unavailable");
    std::printf(",\"legs\":[");
    if (plan_found) {
        for (uint32_t i = 0; i < legs.size(); ++i) {
            if (i != 0) std::putchar(',');
            json_write_plan_leg(pack, options, legs[i]);
        }
        if (gpu_walk_m != 0 || options.to_stop == RTEGPU_NO_INDEX) {
            if (!legs.empty()) std::putchar(',');
            std::printf("{\"kind\":\"egress_walk\",\"from_stop_index\":%u,\"from_stop\":", gpu_best_stop);
            json_write_stop_name_or_null(pack, gpu_best_stop);
            json_write_stop_coord_fields(pack, "from_stop", gpu_best_stop);
            if (options.have_to_coord) json_write_coord_fields("to", options.to_lat, options.to_lon);
            std::printf(",\"walk_m\":%u,\"arrival_sec\":%u}", gpu_walk_m, gpu_best_arrival);
        }
    }
    std::printf("]}");
    std::printf(",\"timing_ms\":{\"address_resolve\":%.3f,\"load\":%.3f,\"host_to_device\":%.3f,\"gpu_kernel_avg\":%.3f,\"cpu_scan\":%.3f}}\n", address_ms, load_ms, copy_ms, gpu_ms, cpu_ms);
}

static void cpu_scan(const RteGpuPack &pack, const std::vector<uint8_t> &active_services, const std::vector<uint32_t> &initial_arrival, const std::vector<uint8_t> &initial_rides, std::vector<uint32_t> &arrival, std::vector<uint8_t> &rides) {
    uint32_t trip_count = (uint32_t)pack.header.trip_count;
    uint32_t stop_count = (uint32_t)pack.header.stop_count;
    arrival = initial_arrival;
    rides = initial_rides;
    std::vector<uint32_t> base_arrival(stop_count);
    std::vector<uint8_t> base_rides(stop_count);
    for (uint32_t round = 0; round < RTEGPU_MAX_ROUNDS; ++round) {
        base_arrival = arrival;
        base_rides = rides;
        for (uint32_t trip_index = 0; trip_index < trip_count; ++trip_index) {
            if (!active_services[pack.trip_service[trip_index]]) continue;
            uint32_t offset = pack.trip_event_offset[trip_index];
            uint32_t count = pack.trip_event_count[trip_index];
            uint32_t board_departure = 0;
            uint8_t board_rides = 0;
            bool have_board = false;
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t event_index = offset + j;
                uint32_t stop_index = pack.event_stop[event_index];
                uint32_t event_arr = pack.event_arrival[event_index];
                uint32_t event_dep = pack.event_departure[event_index];
                if (have_board && event_arr >= board_departure && event_arr < arrival[stop_index]) {
                    arrival[stop_index] = event_arr;
                    rides[stop_index] = board_rides;
                }
                if (rtegpu_can_board(base_arrival[stop_index], event_dep) && base_rides[stop_index] < RTEGPU_MAX_ROUNDS) {
                    uint8_t candidate = (uint8_t)(base_rides[stop_index] + 1u);
                    if (candidate == round + 1u && (!have_board || event_dep < board_departure)) {
                        have_board = true;
                        board_departure = event_dep;
                        board_rides = candidate;
                    }
                }
            }
        }
        base_arrival = arrival;
        base_rides = rides;
        for (uint32_t from_stop = 0; from_stop < stop_count; ++from_stop) {
            uint32_t from_arrival = base_arrival[from_stop];
            uint8_t from_rides = base_rides[from_stop];
            if (from_arrival == RTEGPU_INF_TIME || from_rides >= RTEGPU_MAX_ROUNDS) continue;
            uint32_t begin = pack.transfer_offset[from_stop];
            uint32_t end = pack.transfer_offset[from_stop + 1u];
            for (uint32_t edge = begin; edge < end; ++edge) {
                uint32_t candidate = from_arrival + pack.transfer_walk_sec[edge];
                uint32_t to_stop = pack.transfer_to[edge];
                if (candidate >= from_arrival && candidate < arrival[to_stop]) {
                    arrival[to_stop] = candidate;
                    rides[to_stop] = from_rides;
                }
            }
        }
    }
}

static void rtegpu_device_free(RteGpuDeviceContext &device) {
    cudaFree(device.trip_service);
    cudaFree(device.trip_route);
    cudaFree(device.trip_mode);
    cudaFree(device.trip_event_offset);
    cudaFree(device.trip_event_count);
    cudaFree(device.event_stop);
    cudaFree(device.event_arrival);
    cudaFree(device.event_departure);
    cudaFree(device.transfer_offset);
    cudaFree(device.transfer_to);
    cudaFree(device.transfer_walk_sec);
    cudaFree(device.state);
    cudaFree(device.base_state);
    cudaFree(device.active_services);
    cudaFree(device.pred_kind);
    cudaFree(device.pred_previous);
    cudaFree(device.pred_trip);
    cudaFree(device.pred_route);
    cudaFree(device.pred_mode);
    cudaFree(device.pred_board);
    cudaFree(device.pred_alight);
    cudaFree(device.pred_departure);
    cudaFree(device.pred_arrival);
    cudaFree(device.pred_walk_m);
    device = RteGpuDeviceContext();
}

static double rtegpu_device_init(const RteGpuPack &pack, bool with_plan, RteGpuDeviceContext &device) {
    device = RteGpuDeviceContext();
    device.with_plan = with_plan;
    device.stop_bytes_u64 = (size_t)pack.header.stop_count * sizeof(unsigned long long);
    device.stop_bytes_u32 = (size_t)pack.header.stop_count * sizeof(uint32_t);
    device.trip_bytes = (size_t)pack.header.trip_count * sizeof(uint32_t);
    device.event_bytes = (size_t)pack.header.event_count * sizeof(uint32_t);
    device.transfer_offset_bytes = pack.transfer_offset.size() * sizeof(uint32_t);
    device.transfer_edge_bytes = pack.transfer_to.size() * sizeof(uint32_t);

    cuda_check(cudaMalloc(&device.trip_service, device.trip_bytes), "malloc trip_service");
    cuda_check(cudaMalloc(&device.trip_route, device.trip_bytes), "malloc trip_route");
    cuda_check(cudaMalloc(&device.trip_mode, device.trip_bytes), "malloc trip_mode");
    cuda_check(cudaMalloc(&device.trip_event_offset, device.trip_bytes), "malloc trip_event_offset");
    cuda_check(cudaMalloc(&device.trip_event_count, device.trip_bytes), "malloc trip_event_count");
    cuda_check(cudaMalloc(&device.event_stop, device.event_bytes), "malloc event_stop");
    cuda_check(cudaMalloc(&device.event_arrival, device.event_bytes), "malloc event_arrival");
    cuda_check(cudaMalloc(&device.event_departure, device.event_bytes), "malloc event_departure");
    cuda_check(cudaMalloc(&device.transfer_offset, device.transfer_offset_bytes), "malloc transfer_offset");
    cuda_check(cudaMalloc(&device.transfer_to, device.transfer_edge_bytes), "malloc transfer_to");
    cuda_check(cudaMalloc(&device.transfer_walk_sec, device.transfer_edge_bytes), "malloc transfer_walk_sec");
    cuda_check(cudaMalloc(&device.state, device.stop_bytes_u64), "malloc state");
    cuda_check(cudaMalloc(&device.base_state, device.stop_bytes_u64), "malloc base_state");
    cuda_check(cudaMalloc(&device.active_services, pack.header.service_count), "malloc active_services");
    if (with_plan) {
        cuda_check(cudaMalloc(&device.pred_kind, device.stop_bytes_u32), "malloc pred_kind");
        cuda_check(cudaMalloc(&device.pred_previous, device.stop_bytes_u32), "malloc pred_previous");
        cuda_check(cudaMalloc(&device.pred_trip, device.stop_bytes_u32), "malloc pred_trip");
        cuda_check(cudaMalloc(&device.pred_route, device.stop_bytes_u32), "malloc pred_route");
        cuda_check(cudaMalloc(&device.pred_mode, device.stop_bytes_u32), "malloc pred_mode");
        cuda_check(cudaMalloc(&device.pred_board, device.stop_bytes_u32), "malloc pred_board");
        cuda_check(cudaMalloc(&device.pred_alight, device.stop_bytes_u32), "malloc pred_alight");
        cuda_check(cudaMalloc(&device.pred_departure, device.stop_bytes_u32), "malloc pred_departure");
        cuda_check(cudaMalloc(&device.pred_arrival, device.stop_bytes_u32), "malloc pred_arrival");
        cuda_check(cudaMalloc(&device.pred_walk_m, device.stop_bytes_u32), "malloc pred_walk_m");
    }

    auto copy_start = std::chrono::steady_clock::now();
    cuda_check(cudaMemcpy(device.trip_service, pack.trip_service.data(), device.trip_bytes, cudaMemcpyHostToDevice), "copy trip_service");
    cuda_check(cudaMemcpy(device.trip_route, pack.trip_route.data(), device.trip_bytes, cudaMemcpyHostToDevice), "copy trip_route");
    cuda_check(cudaMemcpy(device.trip_mode, pack.trip_mode.data(), device.trip_bytes, cudaMemcpyHostToDevice), "copy trip_mode");
    cuda_check(cudaMemcpy(device.trip_event_offset, pack.trip_event_offset.data(), device.trip_bytes, cudaMemcpyHostToDevice), "copy trip_event_offset");
    cuda_check(cudaMemcpy(device.trip_event_count, pack.trip_event_count.data(), device.trip_bytes, cudaMemcpyHostToDevice), "copy trip_event_count");
    cuda_check(cudaMemcpy(device.event_stop, pack.event_stop.data(), device.event_bytes, cudaMemcpyHostToDevice), "copy event_stop");
    cuda_check(cudaMemcpy(device.event_arrival, pack.event_arrival.data(), device.event_bytes, cudaMemcpyHostToDevice), "copy event_arrival");
    cuda_check(cudaMemcpy(device.event_departure, pack.event_departure.data(), device.event_bytes, cudaMemcpyHostToDevice), "copy event_departure");
    cuda_check(cudaMemcpy(device.transfer_offset, pack.transfer_offset.data(), device.transfer_offset_bytes, cudaMemcpyHostToDevice), "copy transfer_offset");
    cuda_check(cudaMemcpy(device.transfer_to, pack.transfer_to.data(), device.transfer_edge_bytes, cudaMemcpyHostToDevice), "copy transfer_to");
    cuda_check(cudaMemcpy(device.transfer_walk_sec, pack.transfer_walk_sec.data(), device.transfer_edge_bytes, cudaMemcpyHostToDevice), "copy transfer_walk_sec");
    cuda_check(cudaDeviceSynchronize(), "static copy sync");
    auto copy_end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(copy_end - copy_start).count();
}

static void compute_route_query_silent(const RteGpuPack &pack, RteGpuDeviceContext &device, QueryOptions &options, const RteGpuQueryTiming &setup_timing, RteGpuRouteResult &result) {
    uint32_t service_day_index = 0;
    if (pack.header.service_date_count == 0) throw std::runtime_error("pack has no service calendar");
    {
        int requested = rtegpu_date_to_day(options.depart_date);
        int first = rtegpu_date_to_day(pack.header.first_service_date);
        if (requested < first || requested >= first + (int)pack.header.service_date_count) throw std::runtime_error("departure date outside service calendar");
        service_day_index = (uint32_t)(requested - first);
    }
    const uint8_t *active_services = pack.service_active.data() + (size_t)service_day_index * (size_t)pack.header.service_count;

    std::vector<uint32_t> initial_arrival;
    std::vector<uint8_t> initial_rides;
    initialize_origin(pack, options, initial_arrival, initial_rides, &result.candidate_origin_stops);
    RteGpuPredHost initial_pred;
    RteGpuPredHost gpu_pred;
    initialize_origin_predecessors(pack, options, initial_arrival, initial_pred);

    std::vector<unsigned long long> initial_state((size_t)pack.header.stop_count);
    for (uint32_t i = 0; i < pack.header.stop_count; ++i) initial_state[i] = rtegpu_pack_state(initial_arrival[i], initial_rides[i]);
    std::vector<unsigned long long> gpu_state((size_t)pack.header.stop_count);
    std::vector<uint32_t> gpu_arrival((size_t)pack.header.stop_count);

    cuda_check(cudaMemcpy(device.active_services, active_services, pack.header.service_count, cudaMemcpyHostToDevice), "copy active_services");
    cuda_check(cudaMemcpy(device.state, initial_state.data(), device.stop_bytes_u64, cudaMemcpyHostToDevice), "reset state");
    cuda_check(cudaMemcpy(device.pred_kind, initial_pred.kind.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_kind");
    cuda_check(cudaMemcpy(device.pred_previous, initial_pred.previous.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_previous");
    cuda_check(cudaMemcpy(device.pred_trip, initial_pred.trip.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_trip");
    cuda_check(cudaMemcpy(device.pred_route, initial_pred.route.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_route");
    cuda_check(cudaMemcpy(device.pred_mode, initial_pred.mode.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_mode");
    cuda_check(cudaMemcpy(device.pred_board, initial_pred.board.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_board");
    cuda_check(cudaMemcpy(device.pred_alight, initial_pred.alight.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_alight");
    cuda_check(cudaMemcpy(device.pred_departure, initial_pred.departure.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_departure");
    cuda_check(cudaMemcpy(device.pred_arrival, initial_pred.arrival.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_arrival");
    cuda_check(cudaMemcpy(device.pred_walk_m, initial_pred.walk_m.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_walk_m");

    cudaEvent_t start_event, stop_event;
    cuda_check(cudaEventCreate(&start_event), "event create start");
    cuda_check(cudaEventCreate(&stop_event), "event create stop");
    cuda_check(cudaEventRecord(start_event), "event record start");
    for (uint32_t round = 0; round < RTEGPU_MAX_ROUNDS; ++round) {
        cuda_check(cudaMemcpy(device.base_state, device.state, device.stop_bytes_u64, cudaMemcpyDeviceToDevice), "copy base state");
        int threads = 256;
        int blocks = (int)std::min<uint64_t>(65535ull, (pack.header.trip_count + threads - 1u) / threads);
        rtegpu_trip_scan_kernel<<<blocks, threads>>>((uint32_t)pack.header.trip_count, device.trip_service, device.trip_route, device.trip_mode, device.trip_event_offset, device.trip_event_count, device.active_services, device.event_stop, device.event_arrival, device.event_departure, device.base_state, device.state, device.pred_kind, device.pred_previous, device.pred_trip, device.pred_route, device.pred_mode, device.pred_board, device.pred_alight, device.pred_departure, device.pred_arrival, device.pred_walk_m, round);
        cuda_check(cudaGetLastError(), "trip scan launch");
        cuda_check(cudaMemcpy(device.base_state, device.state, device.stop_bytes_u64, cudaMemcpyDeviceToDevice), "copy transfer base state");
        int transfer_blocks = (int)std::min<uint64_t>(65535ull, (pack.header.stop_count + threads - 1u) / threads);
        rtegpu_transfer_kernel<<<transfer_blocks, threads>>>((uint32_t)pack.header.stop_count, device.transfer_offset, device.transfer_to, device.transfer_walk_sec, device.base_state, device.state, device.pred_kind, device.pred_previous, device.pred_trip, device.pred_route, device.pred_mode, device.pred_board, device.pred_alight, device.pred_departure, device.pred_arrival, device.pred_walk_m);
        cuda_check(cudaGetLastError(), "transfer launch");
    }
    cuda_check(cudaEventRecord(stop_event), "event record stop");
    cuda_check(cudaEventSynchronize(stop_event), "event sync stop");
    float kernel_ms = 0.0f;
    cuda_check(cudaEventElapsedTime(&kernel_ms, start_event, stop_event), "event elapsed");
    cuda_check(cudaEventDestroy(start_event), "event destroy start");
    cuda_check(cudaEventDestroy(stop_event), "event destroy stop");

    cuda_check(cudaMemcpy(gpu_state.data(), device.state, device.stop_bytes_u64, cudaMemcpyDeviceToHost), "copy result state");
    for (uint32_t i = 0; i < pack.header.stop_count; ++i) gpu_arrival[i] = rtegpu_state_arrival(gpu_state[i]);
    pred_host_init(gpu_pred, (uint32_t)pack.header.stop_count);
    cuda_check(cudaMemcpy(gpu_pred.kind.data(), device.pred_kind, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_kind");
    cuda_check(cudaMemcpy(gpu_pred.previous.data(), device.pred_previous, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_previous");
    cuda_check(cudaMemcpy(gpu_pred.trip.data(), device.pred_trip, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_trip");
    cuda_check(cudaMemcpy(gpu_pred.route.data(), device.pred_route, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_route");
    cuda_check(cudaMemcpy(gpu_pred.mode.data(), device.pred_mode, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_mode");
    cuda_check(cudaMemcpy(gpu_pred.board.data(), device.pred_board, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_board");
    cuda_check(cudaMemcpy(gpu_pred.alight.data(), device.pred_alight, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_alight");
    cuda_check(cudaMemcpy(gpu_pred.departure.data(), device.pred_departure, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_departure");
    cuda_check(cudaMemcpy(gpu_pred.arrival.data(), device.pred_arrival, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_arrival");
    cuda_check(cudaMemcpy(gpu_pred.walk_m.data(), device.pred_walk_m, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_walk_m");

    result.gpu_best_arrival = select_destination(pack, options, gpu_arrival, &result.gpu_best_stop, &result.candidate_destination_stops, &result.gpu_walk_m);
    result.plan_found = false;
    result.plan_legs.clear();
    if (result.gpu_best_stop != RTEGPU_NO_INDEX && result.gpu_best_arrival != RTEGPU_INF_TIME) result.plan_found = build_plan_legs(pack, gpu_pred, result.gpu_best_stop, result.plan_legs);
    result.timing = setup_timing;
    result.timing.gpu_ms = (double)kernel_ms;
}

static int run_route_query(const RteGpuPack &pack, RteGpuDeviceContext &device, QueryOptions &options, const RteGpuQueryTiming &setup_timing) {
    uint32_t service_day_index = 0;
    if (pack.header.service_date_count == 0) throw std::runtime_error("pack has no service calendar");
    {
        int requested = rtegpu_date_to_day(options.depart_date);
        int first = rtegpu_date_to_day(pack.header.first_service_date);
        if (requested < first || requested >= first + (int)pack.header.service_date_count) throw std::runtime_error("departure date outside service calendar");
        service_day_index = (uint32_t)(requested - first);
    }
    const uint8_t *active_services = pack.service_active.data() + (size_t)service_day_index * (size_t)pack.header.service_count;
    std::vector<uint8_t> active_services_host;
    if (options.verify) active_services_host.assign(active_services, active_services + pack.header.service_count);

    std::vector<uint32_t> initial_arrival;
    std::vector<uint8_t> initial_rides;
    uint32_t candidate_origin_stops = 0;
    initialize_origin(pack, options, initial_arrival, initial_rides, &candidate_origin_stops);
    RteGpuPredHost initial_pred;
    RteGpuPredHost gpu_pred;
    if (options.show_plan) initialize_origin_predecessors(pack, options, initial_arrival, initial_pred);

    std::vector<unsigned long long> initial_state((size_t)pack.header.stop_count);
    for (uint32_t i = 0; i < pack.header.stop_count; ++i) initial_state[i] = rtegpu_pack_state(initial_arrival[i], initial_rides[i]);
    std::vector<unsigned long long> gpu_state((size_t)pack.header.stop_count);
    std::vector<uint32_t> gpu_arrival((size_t)pack.header.stop_count);
    std::vector<uint8_t> gpu_rides((size_t)pack.header.stop_count);

    cuda_check(cudaMemcpy(device.active_services, active_services, pack.header.service_count, cudaMemcpyHostToDevice), "copy active_services");
    float total_kernel_ms = 0.0f;
    for (uint32_t iteration = 0; iteration < options.iterations; ++iteration) {
        cuda_check(cudaMemcpy(device.state, initial_state.data(), device.stop_bytes_u64, cudaMemcpyHostToDevice), "reset state");
        if (options.show_plan) {
            cuda_check(cudaMemcpy(device.pred_kind, initial_pred.kind.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_kind");
            cuda_check(cudaMemcpy(device.pred_previous, initial_pred.previous.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_previous");
            cuda_check(cudaMemcpy(device.pred_trip, initial_pred.trip.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_trip");
            cuda_check(cudaMemcpy(device.pred_route, initial_pred.route.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_route");
            cuda_check(cudaMemcpy(device.pred_mode, initial_pred.mode.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_mode");
            cuda_check(cudaMemcpy(device.pred_board, initial_pred.board.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_board");
            cuda_check(cudaMemcpy(device.pred_alight, initial_pred.alight.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_alight");
            cuda_check(cudaMemcpy(device.pred_departure, initial_pred.departure.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_departure");
            cuda_check(cudaMemcpy(device.pred_arrival, initial_pred.arrival.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_arrival");
            cuda_check(cudaMemcpy(device.pred_walk_m, initial_pred.walk_m.data(), device.stop_bytes_u32, cudaMemcpyHostToDevice), "reset pred_walk_m");
        }
        cudaEvent_t start_event, stop_event;
        cuda_check(cudaEventCreate(&start_event), "event create start");
        cuda_check(cudaEventCreate(&stop_event), "event create stop");
        cuda_check(cudaEventRecord(start_event), "event record start");
        for (uint32_t round = 0; round < RTEGPU_MAX_ROUNDS; ++round) {
            cuda_check(cudaMemcpy(device.base_state, device.state, device.stop_bytes_u64, cudaMemcpyDeviceToDevice), "copy base state");
            int threads = 256;
            int blocks = (int)std::min<uint64_t>(65535ull, (pack.header.trip_count + threads - 1u) / threads);
            rtegpu_trip_scan_kernel<<<blocks, threads>>>((uint32_t)pack.header.trip_count, device.trip_service, device.trip_route, device.trip_mode, device.trip_event_offset, device.trip_event_count, device.active_services, device.event_stop, device.event_arrival, device.event_departure, device.base_state, device.state, device.pred_kind, device.pred_previous, device.pred_trip, device.pred_route, device.pred_mode, device.pred_board, device.pred_alight, device.pred_departure, device.pred_arrival, device.pred_walk_m, round);
            cuda_check(cudaGetLastError(), "trip scan launch");
            cuda_check(cudaMemcpy(device.base_state, device.state, device.stop_bytes_u64, cudaMemcpyDeviceToDevice), "copy transfer base state");
            int transfer_blocks = (int)std::min<uint64_t>(65535ull, (pack.header.stop_count + threads - 1u) / threads);
            rtegpu_transfer_kernel<<<transfer_blocks, threads>>>((uint32_t)pack.header.stop_count, device.transfer_offset, device.transfer_to, device.transfer_walk_sec, device.base_state, device.state, device.pred_kind, device.pred_previous, device.pred_trip, device.pred_route, device.pred_mode, device.pred_board, device.pred_alight, device.pred_departure, device.pred_arrival, device.pred_walk_m);
            cuda_check(cudaGetLastError(), "transfer launch");
        }
        cuda_check(cudaEventRecord(stop_event), "event record stop");
        cuda_check(cudaEventSynchronize(stop_event), "event sync stop");
        float ms = 0.0f;
        cuda_check(cudaEventElapsedTime(&ms, start_event, stop_event), "event elapsed");
        total_kernel_ms += ms;
        cuda_check(cudaEventDestroy(start_event), "event destroy start");
        cuda_check(cudaEventDestroy(stop_event), "event destroy stop");
    }
    cuda_check(cudaMemcpy(gpu_state.data(), device.state, device.stop_bytes_u64, cudaMemcpyDeviceToHost), "copy result state");
    for (uint32_t i = 0; i < pack.header.stop_count; ++i) {
        gpu_arrival[i] = rtegpu_state_arrival(gpu_state[i]);
        gpu_rides[i] = rtegpu_state_rides(gpu_state[i]);
    }
    if (options.show_plan) {
        pred_host_init(gpu_pred, (uint32_t)pack.header.stop_count);
        cuda_check(cudaMemcpy(gpu_pred.kind.data(), device.pred_kind, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_kind");
        cuda_check(cudaMemcpy(gpu_pred.previous.data(), device.pred_previous, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_previous");
        cuda_check(cudaMemcpy(gpu_pred.trip.data(), device.pred_trip, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_trip");
        cuda_check(cudaMemcpy(gpu_pred.route.data(), device.pred_route, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_route");
        cuda_check(cudaMemcpy(gpu_pred.mode.data(), device.pred_mode, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_mode");
        cuda_check(cudaMemcpy(gpu_pred.board.data(), device.pred_board, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_board");
        cuda_check(cudaMemcpy(gpu_pred.alight.data(), device.pred_alight, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_alight");
        cuda_check(cudaMemcpy(gpu_pred.departure.data(), device.pred_departure, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_departure");
        cuda_check(cudaMemcpy(gpu_pred.arrival.data(), device.pred_arrival, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_arrival");
        cuda_check(cudaMemcpy(gpu_pred.walk_m.data(), device.pred_walk_m, device.stop_bytes_u32, cudaMemcpyDeviceToHost), "copy pred_walk_m");
    }

    std::vector<uint32_t> cpu_arrival;
    std::vector<uint8_t> cpu_rides;
    auto cpu_start = std::chrono::steady_clock::now();
    if (options.verify) cpu_scan(pack, active_services_host, initial_arrival, initial_rides, cpu_arrival, cpu_rides);
    auto cpu_end = std::chrono::steady_clock::now();

    uint32_t gpu_best_stop, gpu_candidate_dest, gpu_walk_m;
    uint32_t cpu_best_stop = RTEGPU_NO_INDEX, cpu_candidate_dest = 0, cpu_walk_m = 0;
    uint32_t gpu_best_arrival = select_destination(pack, options, gpu_arrival, &gpu_best_stop, &gpu_candidate_dest, &gpu_walk_m);
    uint32_t cpu_best_arrival = RTEGPU_INF_TIME;
    if (options.verify) cpu_best_arrival = select_destination(pack, options, cpu_arrival, &cpu_best_stop, &cpu_candidate_dest, &cpu_walk_m);
    (void)cpu_candidate_dest;
    (void)cpu_walk_m;
    uint64_t mismatch_count = 0;
    if (options.verify) for (uint32_t i = 0; i < pack.header.stop_count; ++i) if (gpu_arrival[i] != cpu_arrival[i]) mismatch_count += 1;
    bool verify_best_match = options.verify && gpu_best_arrival == cpu_best_arrival && gpu_best_stop == cpu_best_stop;
    std::vector<RteGpuPlanLeg> plan_legs;
    bool plan_found = false;
    if (options.show_plan && gpu_best_stop != RTEGPU_NO_INDEX && gpu_best_arrival != RTEGPU_INF_TIME) plan_found = build_plan_legs(pack, gpu_pred, gpu_best_stop, plan_legs);

    char depart_text[16];
    char gpu_arrival_text[16];
    char cpu_arrival_text[16];
    rtegpu_format_time(options.depart_seconds, depart_text);
    rtegpu_format_time(gpu_best_arrival == RTEGPU_INF_TIME ? 0 : gpu_best_arrival, gpu_arrival_text);
    rtegpu_format_time(cpu_best_arrival == RTEGPU_INF_TIME ? 0 : cpu_best_arrival, cpu_arrival_text);
    RteGpuQueryTiming timing = setup_timing;
    timing.gpu_ms = total_kernel_ms / (float)options.iterations;
    timing.cpu_ms = std::chrono::duration<double, std::milli>(cpu_end - cpu_start).count();

    if (options.json) {
        print_json_report(pack, options, plan_legs, plan_found, candidate_origin_stops, gpu_candidate_dest, gpu_best_stop, gpu_best_arrival, gpu_walk_m, verify_best_match, mismatch_count, cpu_best_stop, cpu_best_arrival, timing.address_ms, timing.load_ms, timing.copy_ms, timing.gpu_ms, timing.cpu_ms);
    } else {
        std::printf("format: RTEGPU01\n");
        std::printf("query_kind: transit_trip_scan_with_walk_transfers\n");
        std::printf("resident_mode: %s\n", options.interactive ? "yes" : "no");
        std::printf("address_mode: %s\n", options.resolved_addresses ? "yes" : "no");
        if (options.resolved_addresses) {
            std::printf("from_address_matches: %u\n", options.from_match_count);
            std::printf("to_address_matches: %u\n", options.to_match_count);
            std::printf("address_threads: %u\n", options.address_threads);
            std::printf("address_index_status: %s\n", options.address_index_status.c_str());
            std::printf("address_index_used: %s\n", options.address_index_used ? "yes" : "no");
            std::printf("address_index_entries: %llu\n", (unsigned long long)options.address_index_entries);
        }
        std::printf("depart_date: %u\n", options.depart_date);
        std::printf("depart_time: %s\n", depart_text);
        std::printf("stops: %llu\n", (unsigned long long)pack.header.stop_count);
        std::printf("trips: %llu\n", (unsigned long long)pack.header.trip_count);
        std::printf("events: %llu\n", (unsigned long long)pack.header.event_count);
        std::printf("transfer_edges: %llu\n", (unsigned long long)pack.header.transfer_edge_count);
        std::printf("candidate_origin_stops: %u\n", candidate_origin_stops);
        std::printf("candidate_destination_stops: %u\n", gpu_candidate_dest);
        std::printf("gpu_best_stop: %u\n", gpu_best_stop);
        std::printf("gpu_best_arrival: %s\n", gpu_best_arrival == RTEGPU_INF_TIME ? "unreachable" : gpu_arrival_text);
        std::printf("gpu_walk_from_stop_m: %u\n", gpu_walk_m);
        if (options.verify) {
            std::printf("cpu_best_stop: %u\n", cpu_best_stop);
            std::printf("cpu_best_arrival: %s\n", cpu_best_arrival == RTEGPU_INF_TIME ? "unreachable" : cpu_arrival_text);
            std::printf("verify_arrival_mismatches: %llu\n", (unsigned long long)mismatch_count);
            std::printf("verify_best_match: %s\n", verify_best_match ? "yes" : "no");
        } else {
            std::printf("verify_status: skipped\n");
        }
        if (options.show_plan) print_plan_text(pack, options, plan_legs, plan_found, gpu_walk_m, gpu_best_arrival);
        std::printf("address_resolve_ms: %.3f\n", timing.address_ms);
        std::printf("load_ms: %.3f\n", timing.load_ms);
        std::printf("host_to_device_ms: %.3f\n", timing.copy_ms);
        std::printf("gpu_kernel_avg_ms: %.3f\n", timing.gpu_ms);
        std::printf("cpu_scan_ms: %.3f\n", timing.cpu_ms);
    }
    std::fflush(stdout);
    return !options.verify || (mismatch_count == 0 && verify_best_match) ? 0 : 2;
}

static bool split_interactive_query_line(const std::string &line, std::string &from, std::string &to) {
    size_t tab = line.find('\t');
    if (tab == std::string::npos) return false;
    from = trim_normalized_span_cpp(line.data(), tab);
    to = trim_normalized_span_cpp(line.data() + tab + 1u, line.size() - tab - 1u);
    return !from.empty() && !to.empty();
}

struct RteGpuApiJsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<RteGpuApiJsonValue> array;
    std::map<std::string, RteGpuApiJsonValue> object;
};

static void api_append_utf8(std::string &out, uint32_t codepoint) {
    if (codepoint <= 0x7fu) out.push_back((char)codepoint);
    else if (codepoint <= 0x7ffu) {
        out.push_back((char)(0xc0u | (codepoint >> 6u)));
        out.push_back((char)(0x80u | (codepoint & 0x3fu)));
    } else {
        out.push_back((char)(0xe0u | (codepoint >> 12u)));
        out.push_back((char)(0x80u | ((codepoint >> 6u) & 0x3fu)));
        out.push_back((char)(0x80u | (codepoint & 0x3fu)));
    }
}

struct RteGpuApiJsonParser {
    const std::string &input;
    size_t pos = 0;
    std::string error;

    explicit RteGpuApiJsonParser(const std::string &text) : input(text) {}

    void skip_ws() {
        while (pos < input.size() && std::isspace((unsigned char)input[pos])) pos += 1u;
    }

    bool parse_hex4(uint32_t &value) {
        if (pos + 4u > input.size()) return false;
        value = 0;
        for (uint32_t i = 0; i < 4u; ++i) {
            unsigned char ch = (unsigned char)input[pos++];
            value <<= 4u;
            if (ch >= '0' && ch <= '9') value |= (uint32_t)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= (uint32_t)(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= (uint32_t)(ch - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parse_string(std::string &out) {
        if (pos >= input.size() || input[pos] != '"') { error = "expected JSON string"; return false; }
        pos += 1u;
        out.clear();
        while (pos < input.size()) {
            unsigned char ch = (unsigned char)input[pos++];
            if (ch == '"') return true;
            if (ch == '\\') {
                if (pos >= input.size()) { error = "truncated JSON escape"; return false; }
                unsigned char esc = (unsigned char)input[pos++];
                if (esc == '"' || esc == '\\' || esc == '/') out.push_back((char)esc);
                else if (esc == 'b') out.push_back('\b');
                else if (esc == 'f') out.push_back('\f');
                else if (esc == 'n') out.push_back('\n');
                else if (esc == 'r') out.push_back('\r');
                else if (esc == 't') out.push_back('\t');
                else if (esc == 'u') {
                    uint32_t codepoint = 0;
                    if (!parse_hex4(codepoint)) { error = "invalid JSON unicode escape"; return false; }
                    api_append_utf8(out, codepoint);
                } else {
                    error = "invalid JSON escape";
                    return false;
                }
            } else {
                if (ch < 0x20u) { error = "control character in JSON string"; return false; }
                out.push_back((char)ch);
            }
        }
        error = "unterminated JSON string";
        return false;
    }

    bool parse_value(RteGpuApiJsonValue &value) {
        skip_ws();
        if (pos >= input.size()) { error = "expected JSON value"; return false; }
        char ch = input[pos];
        if (ch == '"') {
            value.type = RteGpuApiJsonValue::String;
            return parse_string(value.text);
        }
        if (ch == '{') {
            value.type = RteGpuApiJsonValue::Object;
            pos += 1u;
            skip_ws();
            if (pos < input.size() && input[pos] == '}') { pos += 1u; return true; }
            for (;;) {
                std::string key;
                if (!parse_string(key)) return false;
                skip_ws();
                if (pos >= input.size() || input[pos] != ':') { error = "expected ':' in JSON object"; return false; }
                pos += 1u;
                RteGpuApiJsonValue child;
                if (!parse_value(child)) return false;
                value.object[key] = child;
                skip_ws();
                if (pos < input.size() && input[pos] == ',') { pos += 1u; skip_ws(); continue; }
                if (pos < input.size() && input[pos] == '}') { pos += 1u; return true; }
                error = "expected ',' or '}' in JSON object";
                return false;
            }
        }
        if (ch == '[') {
            value.type = RteGpuApiJsonValue::Array;
            pos += 1u;
            skip_ws();
            if (pos < input.size() && input[pos] == ']') { pos += 1u; return true; }
            for (;;) {
                RteGpuApiJsonValue child;
                if (!parse_value(child)) return false;
                value.array.push_back(child);
                skip_ws();
                if (pos < input.size() && input[pos] == ',') { pos += 1u; continue; }
                if (pos < input.size() && input[pos] == ']') { pos += 1u; return true; }
                error = "expected ',' or ']' in JSON array";
                return false;
            }
        }
        if (input.compare(pos, 4u, "true") == 0) { value.type = RteGpuApiJsonValue::Bool; value.boolean = true; pos += 4u; return true; }
        if (input.compare(pos, 5u, "false") == 0) { value.type = RteGpuApiJsonValue::Bool; value.boolean = false; pos += 5u; return true; }
        if (input.compare(pos, 4u, "null") == 0) { value.type = RteGpuApiJsonValue::Null; pos += 4u; return true; }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            const char *begin = input.c_str() + pos;
            char *end = nullptr;
            errno = 0;
            double number = std::strtod(begin, &end);
            if (end == begin || errno == ERANGE) { error = "invalid JSON number"; return false; }
            pos = (size_t)(end - input.c_str());
            value.type = RteGpuApiJsonValue::Number;
            value.number = number;
            return true;
        }
        error = "unexpected character in JSON value";
        return false;
    }

    bool parse_root(RteGpuApiJsonValue &value) {
        if (!parse_value(value)) return false;
        skip_ws();
        if (pos != input.size()) { error = "extra data after JSON value"; return false; }
        return true;
    }
};

static const RteGpuApiJsonValue *api_json_field(const RteGpuApiJsonValue &object, const char *name) {
    if (object.type != RteGpuApiJsonValue::Object) return nullptr;
    auto found = object.object.find(name);
    return found == object.object.end() ? nullptr : &found->second;
}

static bool api_json_get_string(const RteGpuApiJsonValue &object, const char *name, std::string &out) {
    const RteGpuApiJsonValue *value = api_json_field(object, name);
    if (value == nullptr) return false;
    if (value->type != RteGpuApiJsonValue::String) throw std::runtime_error(std::string("JSON field must be a string: ") + name);
    out = value->text;
    return true;
}

static bool api_json_get_bool(const RteGpuApiJsonValue &object, const char *name, bool &out) {
    const RteGpuApiJsonValue *value = api_json_field(object, name);
    if (value == nullptr) return false;
    if (value->type != RteGpuApiJsonValue::Bool) throw std::runtime_error(std::string("JSON field must be a boolean: ") + name);
    out = value->boolean;
    return true;
}

static bool api_json_get_u32(const RteGpuApiJsonValue &object, const char *name, uint32_t &out) {
    const RteGpuApiJsonValue *value = api_json_field(object, name);
    if (value == nullptr) return false;
    if (value->type != RteGpuApiJsonValue::Number || value->number < 0.0 || value->number > 4294967295.0 || std::floor(value->number) != value->number) throw std::runtime_error(std::string("JSON field must be an unsigned integer: ") + name);
    out = (uint32_t)value->number;
    return true;
}

static bool api_json_get_i32(const RteGpuApiJsonValue &object, const char *name, int32_t &out) {
    const RteGpuApiJsonValue *value = api_json_field(object, name);
    if (value == nullptr) return false;
    if (value->type != RteGpuApiJsonValue::Number || value->number < -2147483648.0 || value->number > 2147483647.0 || std::floor(value->number) != value->number) throw std::runtime_error(std::string("JSON field must be a signed integer: ") + name);
    out = (int32_t)value->number;
    return true;
}

static bool api_json_get_double(const RteGpuApiJsonValue &object, const char *name, double &out) {
    const RteGpuApiJsonValue *value = api_json_field(object, name);
    if (value == nullptr) return false;
    if (value->type != RteGpuApiJsonValue::Number) throw std::runtime_error(std::string("JSON field must be a number: ") + name);
    out = value->number;
    return true;
}

struct RteGpuApiParsedQuery {
    QueryOptions options;
    std::string from;
    std::string to;
};

static int32_t api_coord_to_e7(double coord) {
    double scaled = coord * 10000000.0;
    if (scaled < -2147483648.0 || scaled > 2147483647.0) throw std::runtime_error("coordinate outside int32 e7 range");
    return (int32_t)std::llround(scaled);
}

static void api_prepare_query(const QueryOptions &base, const RteGpuApiJsonValue &root, RteGpuApiParsedQuery &parsed) {
    if (root.type != RteGpuApiJsonValue::Object) throw std::runtime_error("route request must be a JSON object");
    parsed = RteGpuApiParsedQuery();
    parsed.options = base;
    parsed.options.interactive = true;
    parsed.options.api = true;
    parsed.options.tui = false;
    parsed.options.build_address_index = false;
    parsed.options.verify = false;
    parsed.options.json = true;
    parsed.options.show_plan = true;
    parsed.options.use_color = false;
    parsed.options.from_text = nullptr;
    parsed.options.to_text = nullptr;
    parsed.options.from_stop = RTEGPU_NO_INDEX;
    parsed.options.to_stop = RTEGPU_NO_INDEX;
    parsed.options.have_from_coord = false;
    parsed.options.have_to_coord = false;
    parsed.options.from_candidate_lat.clear();
    parsed.options.from_candidate_lon.clear();
    parsed.options.to_candidate_lat.clear();
    parsed.options.to_candidate_lon.clear();
    parsed.options.resolved_addresses = false;
    parsed.options.from_match_count = 0;
    parsed.options.to_match_count = 0;
    parsed.options.address_index_used = false;
    parsed.options.address_index_checked = false;
    parsed.options.address_index_only = false;
    parsed.options.address_index_status = "not_used";
    if (!base.depart_explicit) set_depart_now(parsed.options);

    std::string depart;
    if (api_json_get_string(root, "depart", depart)) {
        if (!rtegpu_parse_depart(depart.c_str(), &parsed.options.depart_date, &parsed.options.depart_seconds)) throw std::runtime_error("depart must look like YYYY-MM-DDTHH:MM[:SS]");
        parsed.options.depart_explicit = true;
    } else {
        uint32_t depart_date = 0;
        uint32_t depart_seconds = 0;
        bool have_date = api_json_get_u32(root, "depart_date", depart_date);
        bool have_seconds = api_json_get_u32(root, "depart_seconds", depart_seconds);
        if (have_date != have_seconds) throw std::runtime_error("depart_date and depart_seconds must be supplied together");
        if (have_date) {
            parsed.options.depart_date = depart_date;
            parsed.options.depart_seconds = depart_seconds;
            parsed.options.depart_explicit = true;
        }
    }
    if (parsed.options.depart_seconds >= 48u * 3600u) throw std::runtime_error("depart_seconds is out of range");

    bool address_index_only = false;
    if (api_json_get_bool(root, "address_index_only", address_index_only)) parsed.options.address_index_only = address_index_only;

    bool have_from_text = api_json_get_string(root, "from", parsed.from);
    bool have_to_text = api_json_get_string(root, "to", parsed.to);
    if (have_from_text || have_to_text) {
        if (!have_from_text || !have_to_text) throw std::runtime_error("from and to address strings must be supplied together");
        if (parsed.from.empty() || parsed.to.empty()) throw std::runtime_error("from and to must not be empty");
        parsed.options.from_text = parsed.from.c_str();
        parsed.options.to_text = parsed.to.c_str();
        return;
    }

    uint32_t stop = 0;
    if (api_json_get_u32(root, "from_stop", stop) || api_json_get_u32(root, "from_stop_index", stop)) parsed.options.from_stop = stop;
    if (api_json_get_u32(root, "to_stop", stop) || api_json_get_u32(root, "to_stop_index", stop)) parsed.options.to_stop = stop;

    int32_t coord = 0;
    if (api_json_get_i32(root, "from_lat_e7", coord)) { parsed.options.from_lat = coord; parsed.options.have_from_coord = true; }
    if (api_json_get_i32(root, "from_lon_e7", coord)) { parsed.options.from_lon = coord; parsed.options.have_from_coord = true; }
    if (api_json_get_i32(root, "to_lat_e7", coord)) { parsed.options.to_lat = coord; parsed.options.have_to_coord = true; }
    if (api_json_get_i32(root, "to_lon_e7", coord)) { parsed.options.to_lon = coord; parsed.options.have_to_coord = true; }

    double coord_double = 0.0;
    if (api_json_get_double(root, "from_lat", coord_double)) { parsed.options.from_lat = api_coord_to_e7(coord_double); parsed.options.have_from_coord = true; }
    if (api_json_get_double(root, "from_lon", coord_double)) { parsed.options.from_lon = api_coord_to_e7(coord_double); parsed.options.have_from_coord = true; }
    if (api_json_get_double(root, "to_lat", coord_double)) { parsed.options.to_lat = api_coord_to_e7(coord_double); parsed.options.have_to_coord = true; }
    if (api_json_get_double(root, "to_lon", coord_double)) { parsed.options.to_lon = api_coord_to_e7(coord_double); parsed.options.have_to_coord = true; }

    bool have_from = parsed.options.from_stop != RTEGPU_NO_INDEX || parsed.options.have_from_coord;
    bool have_to = parsed.options.to_stop != RTEGPU_NO_INDEX || parsed.options.have_to_coord;
    if (!have_from || !have_to) throw std::runtime_error("request must supply either from/to address strings, stop indexes, or coordinates");
    if (parsed.options.have_from_coord && (parsed.options.from_lat < -900000000 || parsed.options.from_lat > 900000000 || parsed.options.from_lon < -1800000000 || parsed.options.from_lon > 1800000000)) throw std::runtime_error("from coordinate out of WGS84 range");
    if (parsed.options.have_to_coord && (parsed.options.to_lat < -900000000 || parsed.options.to_lat > 900000000 || parsed.options.to_lon < -1800000000 || parsed.options.to_lon > 1800000000)) throw std::runtime_error("to coordinate out of WGS84 range");
}

static void api_append_json_string(std::string &out, const char *data, size_t size) {
    out.push_back('"');
    for (size_t i = 0; i < size; ++i) {
        unsigned char ch = (unsigned char)data[i];
        if (ch == '"' || ch == '\\') { out.push_back('\\'); out.push_back((char)ch); }
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else if (ch < 0x20u) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)ch);
            out += buf;
        } else out.push_back((char)ch);
    }
    out.push_back('"');
}

static void api_append_json_string(std::string &out, const std::string &text) {
    api_append_json_string(out, text.data(), text.size());
}

static void api_append_cstr(std::string &out, const char *text) {
    api_append_json_string(out, text, std::strlen(text));
}

static void api_append_pack_string_or_null(std::string &out, const RteGpuPack &pack, uint32_t offset, uint32_t size) {
    if (offset <= pack.strings.size() && size <= pack.strings.size() - offset && size != 0) api_append_json_string(out, pack.strings.data() + offset, size);
    else out += "null";
}

static void api_append_stop_name_or_null(std::string &out, const RteGpuPack &pack, uint32_t stop) {
    if (stop < pack.stop_name_offset.size() && stop < pack.stop_name_size.size()) api_append_pack_string_or_null(out, pack, pack.stop_name_offset[stop], pack.stop_name_size[stop]);
    else out += "null";
}

static void api_append_route_short_or_null(std::string &out, const RteGpuPack &pack, uint32_t route) {
    if (route < pack.route_short_offset.size() && route < pack.route_short_size.size()) api_append_pack_string_or_null(out, pack, pack.route_short_offset[route], pack.route_short_size[route]);
    else out += "null";
}

static void api_append_route_long_or_null(std::string &out, const RteGpuPack &pack, uint32_t route) {
    if (route < pack.route_long_offset.size() && route < pack.route_long_size.size()) api_append_pack_string_or_null(out, pack, pack.route_long_offset[route], pack.route_long_size[route]);
    else out += "null";
}

static void api_append_coord_fields(std::string &out, const char *prefix, int32_t lat_e7, int32_t lon_e7) {
    char buf[128];
    out += ",\"";
    out += prefix;
    out += "_lat_e7\":";
    out += std::to_string(lat_e7);
    out += ",\"";
    out += prefix;
    out += "_lon_e7\":";
    out += std::to_string(lon_e7);
    std::snprintf(buf, sizeof(buf), ",\"%s_lat\":%.7f,\"%s_lon\":%.7f", prefix, (double)lat_e7 / 10000000.0, prefix, (double)lon_e7 / 10000000.0);
    out += buf;
}

static void api_append_stop_coord_fields(std::string &out, const RteGpuPack &pack, const char *prefix, uint32_t stop) {
    if (stop < pack.stop_lat.size() && stop < pack.stop_lon.size()) api_append_coord_fields(out, prefix, pack.stop_lat[stop], pack.stop_lon[stop]);
}

static void api_append_double3(std::string &out, double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", value);
    out += buf;
}

static void api_append_plan_leg(std::string &out, const RteGpuPack &pack, const QueryOptions &options, const RteGpuPlanLeg &leg) {
    if (leg.kind == RTEGPU_STATE_ORIGIN_WALK) {
        out += "{\"kind\":\"access_walk\",\"to_stop_index\":" + std::to_string(leg.alight) + ",\"to_stop\":";
        api_append_stop_name_or_null(out, pack, leg.alight);
        if (options.have_from_coord) api_append_coord_fields(out, "from", options.from_lat, options.from_lon);
        api_append_stop_coord_fields(out, pack, "to_stop", leg.alight);
        out += ",\"walk_m\":" + std::to_string(leg.walk_m) + ",\"departure_sec\":" + std::to_string(leg.departure) + ",\"arrival_sec\":" + std::to_string(leg.arrival) + "}";
    } else if (leg.kind == RTEGPU_STATE_TRANSFER_WALK) {
        out += "{\"kind\":\"transfer_walk\",\"from_stop_index\":" + std::to_string(leg.board) + ",\"from_stop\":";
        api_append_stop_name_or_null(out, pack, leg.board);
        out += ",\"to_stop_index\":" + std::to_string(leg.alight) + ",\"to_stop\":";
        api_append_stop_name_or_null(out, pack, leg.alight);
        api_append_stop_coord_fields(out, pack, "from_stop", leg.board);
        api_append_stop_coord_fields(out, pack, "to_stop", leg.alight);
        out += ",\"walk_m\":" + std::to_string(leg.walk_m) + ",\"departure_sec\":" + std::to_string(leg.departure) + ",\"arrival_sec\":" + std::to_string(leg.arrival) + "}";
    } else if (leg.kind == RTEGPU_STATE_VEHICLE) {
        out += "{\"kind\":\"ride\",\"mode\":";
        api_append_cstr(out, mode_name(leg.mode));
        out += ",\"route_index\":" + std::to_string(leg.route) + ",\"route_short_name\":";
        api_append_route_short_or_null(out, pack, leg.route);
        out += ",\"route_long_name\":";
        api_append_route_long_or_null(out, pack, leg.route);
        out += ",\"board_stop_index\":" + std::to_string(leg.board) + ",\"board_stop\":";
        api_append_stop_name_or_null(out, pack, leg.board);
        out += ",\"alight_stop_index\":" + std::to_string(leg.alight) + ",\"alight_stop\":";
        api_append_stop_name_or_null(out, pack, leg.alight);
        api_append_stop_coord_fields(out, pack, "board_stop", leg.board);
        api_append_stop_coord_fields(out, pack, "alight_stop", leg.alight);
        out += ",\"departure_sec\":" + std::to_string(leg.departure) + ",\"arrival_sec\":" + std::to_string(leg.arrival) + "}";
    } else {
        out += "{\"kind\":\"unknown\"}";
    }
}

static std::string api_route_response_json(const RteGpuPack &pack, const QueryOptions &options, const RteGpuRouteResult &result) {
    std::string out;
    out.reserve(4096u + result.plan_legs.size() * 512u);
    out += "{\"format\":\"RTEGPU01\",\"query_kind\":\"transit_trip_scan_with_walk_transfers\",\"resident_mode\":true,\"api_mode\":true";
    out += ",\"query\":{\"depart_date\":" + std::to_string(options.depart_date) + ",\"depart_seconds\":" + std::to_string(options.depart_seconds);
    if (options.from_text != nullptr) { out += ",\"from\":"; api_append_cstr(out, options.from_text); }
    if (options.to_text != nullptr) { out += ",\"to\":"; api_append_cstr(out, options.to_text); }
    if (options.from_stop != RTEGPU_NO_INDEX) out += ",\"from_stop_index\":" + std::to_string(options.from_stop);
    if (options.to_stop != RTEGPU_NO_INDEX) out += ",\"to_stop_index\":" + std::to_string(options.to_stop);
    if (options.have_from_coord) out += ",\"from_lat_e7\":" + std::to_string(options.from_lat) + ",\"from_lon_e7\":" + std::to_string(options.from_lon);
    if (options.have_to_coord) out += ",\"to_lat_e7\":" + std::to_string(options.to_lat) + ",\"to_lon_e7\":" + std::to_string(options.to_lon);
    out += "}";
    out += ",\"address_mode\":";
    out += options.resolved_addresses ? "true" : "false";
    if (options.resolved_addresses) out += ",\"from_address_matches\":" + std::to_string(options.from_match_count) + ",\"to_address_matches\":" + std::to_string(options.to_match_count) + ",\"address_threads\":" + std::to_string(options.address_threads);
    out += ",\"address_index\":{\"status\":";
    api_append_json_string(out, options.address_index_status);
    out += ",\"used\":";
    out += options.address_index_used ? "true" : "false";
    out += ",\"entries\":" + std::to_string(options.address_index_entries) + "}";
    out += ",\"counts\":{\"stops\":" + std::to_string(pack.header.stop_count) + ",\"trips\":" + std::to_string(pack.header.trip_count) + ",\"events\":" + std::to_string(pack.header.event_count) + ",\"transfer_edges\":" + std::to_string(pack.header.transfer_edge_count) + "}";
    out += ",\"result\":{\"status\":";
    api_append_cstr(out, result.gpu_best_arrival == RTEGPU_INF_TIME ? "unreachable" : "found");
    out += ",\"candidate_origin_stops\":" + std::to_string(result.candidate_origin_stops) + ",\"candidate_destination_stops\":" + std::to_string(result.candidate_destination_stops) + ",\"best_stop_index\":" + std::to_string(result.gpu_best_stop) + ",\"best_arrival_sec\":" + std::to_string(result.gpu_best_arrival) + ",\"walk_from_stop_m\":" + std::to_string(result.gpu_walk_m) + "}";
    out += ",\"verification\":{\"enabled\":false}";
    out += ",\"plan\":{\"status\":";
    api_append_cstr(out, result.plan_found ? "found" : "unavailable");
    out += ",\"legs\":[";
    if (result.plan_found) {
        bool need_comma = false;
        for (uint32_t i = 0; i < result.plan_legs.size(); ++i) {
            if (need_comma) out.push_back(',');
            api_append_plan_leg(out, pack, options, result.plan_legs[i]);
            need_comma = true;
        }
        if (result.gpu_walk_m != 0 || options.to_stop == RTEGPU_NO_INDEX) {
            if (need_comma) out.push_back(',');
            out += "{\"kind\":\"egress_walk\",\"from_stop_index\":" + std::to_string(result.gpu_best_stop) + ",\"from_stop\":";
            api_append_stop_name_or_null(out, pack, result.gpu_best_stop);
            api_append_stop_coord_fields(out, pack, "from_stop", result.gpu_best_stop);
            if (options.have_to_coord) api_append_coord_fields(out, "to", options.to_lat, options.to_lon);
            out += ",\"walk_m\":" + std::to_string(result.gpu_walk_m) + ",\"arrival_sec\":" + std::to_string(result.gpu_best_arrival) + "}";
        }
    }
    out += "]}";
    out += ",\"timing_ms\":{\"address_resolve\":";
    api_append_double3(out, result.timing.address_ms);
    out += ",\"load\":";
    api_append_double3(out, result.timing.load_ms);
    out += ",\"host_to_device\":";
    api_append_double3(out, result.timing.copy_ms);
    out += ",\"gpu_kernel_avg\":";
    api_append_double3(out, result.timing.gpu_ms);
    out += ",\"cpu_scan\":";
    api_append_double3(out, result.timing.cpu_ms);
    out += "}}\n";
    return out;
}

static std::string api_error_json(const std::string &message) {
    std::string out = "{\"error\":{\"message\":";
    api_append_json_string(out, message);
    out += "}}\n";
    return out;
}

struct RteGpuHttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;
};

static std::string api_lower_ascii(std::string text) {
    for (char &ch : text) ch = (char)std::tolower((unsigned char)ch);
    return text;
}

static std::string api_trim_ascii(const std::string &text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace((unsigned char)text[begin])) begin += 1u;
    size_t end = text.size();
    while (end > begin && std::isspace((unsigned char)text[end - 1u])) end -= 1u;
    return text.substr(begin, end - begin);
}

static bool api_read_http_request(int fd, RteGpuHttpRequest &request, std::string &error) {
    timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::string data;
    char buffer[4096];
    size_t header_end = std::string::npos;
    while ((header_end = data.find("\r\n\r\n")) == std::string::npos) {
        if (data.size() > 65536u) { error = "HTTP header too large"; return false; }
        ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            error = std::string("recv failed: ") + std::strerror(errno);
            return false;
        }
        if (got == 0) { error = "connection closed before HTTP header"; return false; }
        data.append(buffer, (size_t)got);
    }
    std::string header = data.substr(0, header_end);
    std::istringstream stream(header);
    std::string line;
    if (!std::getline(stream, line)) { error = "empty HTTP request"; return false; }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream first(line);
    std::string version;
    if (!(first >> request.method >> request.path >> version)) { error = "invalid HTTP request line"; return false; }
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = api_lower_ascii(api_trim_ascii(line.substr(0, colon)));
        std::string value = api_trim_ascii(line.substr(colon + 1u));
        request.headers[key] = value;
    }
    uint64_t content_length = 0;
    auto length_header = request.headers.find("content-length");
    if (length_header != request.headers.end()) {
        char *end = nullptr;
        errno = 0;
        unsigned long long parsed = std::strtoull(length_header->second.c_str(), &end, 10);
        if (errno != 0 || end == length_header->second.c_str() || *end != '\0') { error = "invalid Content-Length"; return false; }
        content_length = (uint64_t)parsed;
    }
    if (content_length > 1024u * 1024u) { error = "HTTP body too large"; return false; }
    size_t body_begin = header_end + 4u;
    if (body_begin < data.size()) request.body.assign(data.data() + body_begin, data.size() - body_begin);
    while (request.body.size() < content_length) {
        ssize_t got = recv(fd, buffer, sizeof(buffer), 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            error = std::string("recv body failed: ") + std::strerror(errno);
            return false;
        }
        if (got == 0) { error = "connection closed before HTTP body"; return false; }
        request.body.append(buffer, (size_t)got);
    }
    if (request.body.size() > content_length) request.body.resize((size_t)content_length);
    return true;
}

static void api_send_all(int fd, const std::string &data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (n == 0) return;
        sent += (size_t)n;
    }
}

static void api_send_response(int fd, int status, const char *reason, const char *content_type, const std::string &body) {
    std::string header = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    header += "Content-Type: ";
    header += content_type;
    header += "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: GET,POST,OPTIONS\r\n\r\n";
    api_send_all(fd, header);
    api_send_all(fd, body);
}

static std::string api_index_html() {
    return std::string(
        "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>rte-gpu-route API</title><style>body{font-family:system-ui,sans-serif;margin:24px;max-width:960px;background:#f7f7f4;color:#202124}label{display:block;margin:12px 0 4px;font-weight:600}input,textarea,button{font:inherit}input{width:100%;box-sizing:border-box;padding:8px;border:1px solid #aaa;border-radius:6px}button{margin-top:12px;padding:8px 14px;border:1px solid #555;border-radius:6px;background:#fff;cursor:pointer}pre{white-space:pre-wrap;background:#111;color:#eee;padding:12px;border-radius:6px;overflow:auto;min-height:220px}</style></head>"
        "<body><h1>rte-gpu-route API</h1><form id=\"route\"><label>From</label><input id=\"from\" name=\"from\" autocomplete=\"street-address\"><label>To</label><input id=\"to\" name=\"to\" autocomplete=\"street-address\"><label>Depart</label><input id=\"depart\" name=\"depart\" placeholder=\"YYYY-MM-DDTHH:MM[:SS]\"><button>Route</button></form><pre id=\"out\"></pre>"
        "<script>const f=document.getElementById('route'),o=document.getElementById('out');f.addEventListener('submit',async e=>{e.preventDefault();const q={from:f.from.value,to:f.to.value};if(f.depart.value)q.depart=f.depart.value;o.textContent='Routing...';try{const r=await fetch('/route',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(q)});const t=await r.text();try{o.textContent=JSON.stringify(JSON.parse(t),null,2)}catch(_){o.textContent=t}}catch(err){o.textContent=String(err)}});</script></body></html>");
}

static int api_open_listener(const char *host, uint32_t port) {
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    std::string port_text = std::to_string(port);
    const char *node = (host == nullptr || host[0] == '\0' || std::strcmp(host, "*") == 0) ? nullptr : host;
    addrinfo *infos = nullptr;
    int gai = getaddrinfo(node, port_text.c_str(), &hints, &infos);
    if (gai != 0) throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(gai));
    int listener = -1;
    for (addrinfo *info = infos; info != nullptr; info = info->ai_next) {
        int fd = socket(info->ai_family, info->ai_socktype | SOCK_CLOEXEC, info->ai_protocol);
        if (fd < 0) continue;
        int reuse = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(fd, info->ai_addr, info->ai_addrlen) == 0 && listen(fd, 16) == 0) {
            listener = fd;
            break;
        }
        close(fd);
    }
    freeaddrinfo(infos);
    if (listener < 0) throw std::runtime_error("failed to bind API listener");
    return listener;
}

static void api_handle_route_request(int fd, const RteGpuPack &pack, RteGpuDeviceContext &device, const QueryOptions &base_options, const std::string &body) {
    try {
        RteGpuApiJsonValue root;
        RteGpuApiJsonParser parser(body);
        if (!parser.parse_root(root)) throw std::runtime_error(parser.error.empty() ? "invalid JSON" : parser.error);
        RteGpuApiParsedQuery parsed;
        api_prepare_query(base_options, root, parsed);
        RteGpuQueryTiming timing;
        auto address_start = std::chrono::steady_clock::now();
        if (parsed.options.from_text != nullptr || parsed.options.to_text != nullptr) resolve_address_queries(parsed.options);
        auto address_end = std::chrono::steady_clock::now();
        timing.address_ms = std::chrono::duration<double, std::milli>(address_end - address_start).count();
        timing.load_ms = 0.0;
        timing.copy_ms = 0.0;
        RteGpuRouteResult result;
        compute_route_query_silent(pack, device, parsed.options, timing, result);
        api_send_response(fd, 200, "OK", "application/json; charset=utf-8", api_route_response_json(pack, parsed.options, result));
    } catch (const std::exception &e) {
        api_send_response(fd, 400, "Bad Request", "application/json; charset=utf-8", api_error_json(e.what()));
    }
}

static void api_handle_http_request(int fd, const RteGpuPack &pack, RteGpuDeviceContext &device, const QueryOptions &base_options) {
    RteGpuHttpRequest request;
    std::string error;
    if (!api_read_http_request(fd, request, error)) {
        api_send_response(fd, 400, "Bad Request", "application/json; charset=utf-8", api_error_json(error));
        return;
    }
    std::string path = request.path;
    size_t question = path.find('?');
    if (question != std::string::npos) path.resize(question);
    if (request.method == "OPTIONS") {
        api_send_response(fd, 204, "No Content", "text/plain; charset=utf-8", "");
    } else if (request.method == "GET" && (path == "/" || path == "/index.html")) {
        api_send_response(fd, 200, "OK", "text/html; charset=utf-8", api_index_html());
    } else if (request.method == "GET" && path == "/health") {
        std::string body = "{\"status\":\"ok\",\"format\":\"RTEGPU01\",\"stops\":" + std::to_string(pack.header.stop_count) + ",\"trips\":" + std::to_string(pack.header.trip_count) + "}\n";
        api_send_response(fd, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && (path == "/route" || path == "/api/route")) {
        api_handle_route_request(fd, pack, device, base_options, request.body);
    } else {
        api_send_response(fd, 404, "Not Found", "application/json; charset=utf-8", api_error_json("unknown endpoint"));
    }
}

static int run_api_mode(const RteGpuPack &pack, RteGpuDeviceContext &device, QueryOptions &options, const RteGpuQueryTiming &setup_timing) {
    int listener = api_open_listener(options.api_host, options.api_port);
    std::fprintf(stderr, "rte-gpu-route: api ready http://%s:%u/ load_ms=%.3f host_to_device_ms=%.3f endpoint=POST /route\n", options.api_host, options.api_port, setup_timing.load_ms, setup_timing.copy_ms);
    for (;;) {
        int client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "rte-gpu-route: accept failed: %s\n", std::strerror(errno));
            break;
        }
        api_handle_http_request(client, pack, device, options);
        close(client);
    }
    close(listener);
    return 0;
}

struct RteGpuTuiTerminal {
    termios saved;
    bool raw = false;
    unsigned int rows = 24;
    unsigned int columns = 80;
};

struct RteGpuTuiKey {
    enum Kind { None, Text, Escape, Tab, Enter, Backspace, DeleteKey, Up, Down, Left, Right, Home, End, CtrlQ } kind = None;
    std::string text;
};

struct RteGpuTuiAddressCache {
    std::string from_text;
    std::string to_text;
    int32_t from_lat = 0;
    int32_t from_lon = 0;
    int32_t to_lat = 0;
    int32_t to_lon = 0;
    std::vector<int32_t> from_candidate_lat;
    std::vector<int32_t> from_candidate_lon;
    std::vector<int32_t> to_candidate_lat;
    std::vector<int32_t> to_candidate_lon;
    uint32_t from_match_count = 0;
    uint32_t to_match_count = 0;
    uint32_t address_threads = 0;
    uint64_t address_index_entries = 0;
    bool address_index_used = false;
    bool address_index_checked = false;
    bool valid = false;
    std::string address_index_status;
};

struct RteGpuTuiState {
    std::string from;
    std::string to;
    std::string depart;
    size_t cursor[3] = {0, 0, 0};
    unsigned int active = 0;
    std::string status;
    bool have_result = false;
    bool use_color = true;
    RteGpuTuiAddressCache address_cache;
    RteGpuRouteResult result;
};

static void tui_host_write(const char *text) {
    size_t remaining = std::strlen(text);
    while (remaining != 0) {
        ssize_t written = ::write(STDOUT_FILENO, text, remaining);
        if (written <= 0) return;
        text += written;
        remaining -= (size_t)written;
    }
}

static void tui_host_write_string(const std::string &text) {
    const char *data = text.data();
    size_t remaining = text.size();
    while (remaining != 0) {
        ssize_t written = ::write(STDOUT_FILENO, data, remaining);
        if (written <= 0) return;
        data += written;
        remaining -= (size_t)written;
    }
}

static void tui_clear_address_cache(RteGpuTuiState &state) {
    state.address_cache = RteGpuTuiAddressCache();
}

static bool tui_address_cache_matches(const RteGpuTuiState &state) {
    return state.address_cache.valid && state.address_cache.from_text == state.from && state.address_cache.to_text == state.to;
}

static void tui_store_address_cache(RteGpuTuiState &state, const QueryOptions &query) {
    if (!query.resolved_addresses || !query.have_from_coord || !query.have_to_coord) return;
    state.address_cache.from_text = state.from;
    state.address_cache.to_text = state.to;
    state.address_cache.from_lat = query.from_lat;
    state.address_cache.from_lon = query.from_lon;
    state.address_cache.to_lat = query.to_lat;
    state.address_cache.to_lon = query.to_lon;
    state.address_cache.from_candidate_lat = query.from_candidate_lat;
    state.address_cache.from_candidate_lon = query.from_candidate_lon;
    state.address_cache.to_candidate_lat = query.to_candidate_lat;
    state.address_cache.to_candidate_lon = query.to_candidate_lon;
    state.address_cache.from_match_count = query.from_match_count;
    state.address_cache.to_match_count = query.to_match_count;
    state.address_cache.address_threads = query.address_threads;
    state.address_cache.address_index_entries = query.address_index_entries;
    state.address_cache.address_index_used = query.address_index_used;
    state.address_cache.address_index_checked = query.address_index_checked;
    state.address_cache.address_index_status = query.address_index_status;
    state.address_cache.valid = true;
}

static void tui_apply_address_cache(const RteGpuTuiState &state, QueryOptions &query) {
    query.from_lat = state.address_cache.from_lat;
    query.from_lon = state.address_cache.from_lon;
    query.to_lat = state.address_cache.to_lat;
    query.to_lon = state.address_cache.to_lon;
    query.from_candidate_lat = state.address_cache.from_candidate_lat;
    query.from_candidate_lon = state.address_cache.from_candidate_lon;
    query.to_candidate_lat = state.address_cache.to_candidate_lat;
    query.to_candidate_lon = state.address_cache.to_candidate_lon;
    query.have_from_coord = true;
    query.have_to_coord = true;
    query.resolved_addresses = true;
    query.from_match_count = state.address_cache.from_match_count;
    query.to_match_count = state.address_cache.to_match_count;
    query.address_threads = state.address_cache.address_threads;
    query.address_index_entries = state.address_cache.address_index_entries;
    query.address_index_used = state.address_cache.address_index_used;
    query.address_index_checked = state.address_cache.address_index_checked;
    query.address_index_status = state.address_cache.address_index_status;
}

static void tui_host_move(unsigned int row, unsigned int column) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "\033[%u;%uH", row == 0 ? 1 : row, column == 0 ? 1 : column);
    tui_host_write(buffer);
}

static void tui_host_style(const char *style) {
    tui_host_write(style);
}

static void tui_host_refresh_size(RteGpuTuiTerminal &terminal) {
    winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row != 0 && ws.ws_col != 0) {
        terminal.rows = ws.ws_row;
        terminal.columns = ws.ws_col;
    } else {
        terminal.rows = 24;
        terminal.columns = 80;
    }
}

static bool tui_host_open(RteGpuTuiTerminal &terminal) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
    if (tcgetattr(STDIN_FILENO, &terminal.saved) != 0) return false;
    termios raw = terminal.saved;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_iflag &= (tcflag_t)~IXON;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    terminal.raw = true;
    tui_host_refresh_size(terminal);
    tui_host_write("\033[?1049h\033[?25l\033[2J");
    return true;
}

static void tui_host_close(RteGpuTuiTerminal &terminal) {
    tui_host_write("\033[0m\033[?25h\033[?1049l");
    if (terminal.raw) {
        (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal.saved);
        terminal.raw = false;
    }
}

static bool tui_read_byte_timeout(unsigned char *out, int timeout_ms) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ready = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
    if (ready <= 0) return false;
    return ::read(STDIN_FILENO, out, 1) == 1;
}

static RteGpuTuiKey tui_read_host_key() {
    RteGpuTuiKey key;
    unsigned char ch = 0;
    if (::read(STDIN_FILENO, &ch, 1) != 1) return key;
    if (ch == 17U) { key.kind = RteGpuTuiKey::CtrlQ; return key; }
    if (ch == 27U) {
        unsigned char next = 0;
        if (!tui_read_byte_timeout(&next, 20)) { key.kind = RteGpuTuiKey::Escape; return key; }
        if (next == '[') {
            unsigned char code = 0;
            if (!tui_read_byte_timeout(&code, 20)) { key.kind = RteGpuTuiKey::Escape; return key; }
            if (code == 'A') key.kind = RteGpuTuiKey::Up;
            else if (code == 'B') key.kind = RteGpuTuiKey::Down;
            else if (code == 'C') key.kind = RteGpuTuiKey::Right;
            else if (code == 'D') key.kind = RteGpuTuiKey::Left;
            else if (code == 'H') key.kind = RteGpuTuiKey::Home;
            else if (code == 'F') key.kind = RteGpuTuiKey::End;
            else if (code == '3') {
                unsigned char tilde = 0;
                (void)tui_read_byte_timeout(&tilde, 20);
                key.kind = RteGpuTuiKey::DeleteKey;
            } else key.kind = RteGpuTuiKey::Escape;
            return key;
        }
        key.kind = RteGpuTuiKey::Escape;
        return key;
    }
    if (ch == '\t') { key.kind = RteGpuTuiKey::Tab; return key; }
    if (ch == '\r' || ch == '\n') { key.kind = RteGpuTuiKey::Enter; return key; }
    if (ch == 127U || ch == 8U) { key.kind = RteGpuTuiKey::Backspace; return key; }
    if (ch >= 32U) {
        key.kind = RteGpuTuiKey::Text;
        key.text.push_back((char)ch);
        if ((ch & 0xe0U) == 0xc0U || (ch & 0xf0U) == 0xe0U || (ch & 0xf8U) == 0xf0U) {
            unsigned int need = (ch & 0xe0U) == 0xc0U ? 1U : ((ch & 0xf0U) == 0xe0U ? 2U : 3U);
            for (unsigned int i = 0; i < need; ++i) {
                unsigned char tail = 0;
                if (!tui_read_byte_timeout(&tail, 20)) break;
                key.text.push_back((char)tail);
            }
        }
    }
    return key;
}

static size_t tui_previous_utf8(const std::string &text, size_t cursor) {
    if (cursor == 0) return 0;
    cursor -= 1;
    while (cursor > 0 && (((unsigned char)text[cursor] & 0xc0U) == 0x80U)) cursor -= 1;
    return cursor;
}

static size_t tui_next_utf8(const std::string &text, size_t cursor) {
    if (cursor >= text.size()) return text.size();
    cursor += 1;
    while (cursor < text.size() && (((unsigned char)text[cursor] & 0xc0U) == 0x80U)) cursor += 1;
    return cursor;
}

static unsigned int tui_display_columns(const std::string &text, size_t bytes) {
    unsigned int columns = 0;
    if (bytes > text.size()) bytes = text.size();
    for (size_t i = 0; i < bytes; ++i) if ((((unsigned char)text[i] & 0xc0U) != 0x80U)) columns += 1;
    return columns;
}

static std::string tui_pack_string(const RteGpuPack &pack, uint32_t offset, uint32_t size) {
    if (offset <= pack.strings.size() && size <= pack.strings.size() - offset && size != 0) return std::string(pack.strings.data() + offset, size);
    return std::string("unknown");
}

static std::string tui_stop_name(const RteGpuPack &pack, uint32_t stop) {
    if (stop < pack.stop_name_offset.size() && stop < pack.stop_name_size.size()) return tui_pack_string(pack, pack.stop_name_offset[stop], pack.stop_name_size[stop]);
    return std::string("stop ") + std::to_string(stop);
}

static std::string tui_route_name(const RteGpuPack &pack, uint32_t route) {
    if (route < pack.route_short_offset.size() && route < pack.route_short_size.size() && pack.route_short_size[route] != 0) return tui_pack_string(pack, pack.route_short_offset[route], pack.route_short_size[route]);
    if (route < pack.route_long_offset.size() && route < pack.route_long_size.size() && pack.route_long_size[route] != 0) return tui_pack_string(pack, pack.route_long_offset[route], pack.route_long_size[route]);
    return std::string("route ") + std::to_string(route);
}

static std::string tui_time(uint32_t sec) {
    char buffer[16];
    rtegpu_format_time(sec, buffer);
    return std::string(buffer);
}

static std::string tui_color_wrap(bool use_color, const char *code, const std::string &text) {
    if (!use_color) return text;
    return std::string(code) + text + "\033[0m";
}

static std::string tui_stop_name_colored(const RteGpuPack &pack, bool use_color, uint32_t stop) {
    return tui_color_wrap(use_color, "\033[1;36m", tui_stop_name(pack, stop));
}

static std::string tui_route_label_colored(const RteGpuPack &pack, bool use_color, uint32_t mode, uint32_t route) {
    return tui_color_wrap(use_color, "\033[1;33m", std::string(mode_name(mode)) + " " + tui_route_name(pack, route));
}

static std::vector<std::string> tui_plan_lines_colored(const RteGpuPack &pack, const RteGpuRouteResult &result, bool use_color) {
    std::vector<std::string> lines;
    if (result.gpu_best_arrival == RTEGPU_INF_TIME) {
        lines.push_back("No route found for the current inputs.");
        return lines;
    }
    lines.push_back(std::string("Arrival ") + tui_time(result.gpu_best_arrival) + " via " + tui_stop_name_colored(pack, use_color, result.gpu_best_stop));
    if (!result.plan_found) {
        lines.push_back("Plan reconstruction unavailable.");
        return lines;
    }
    bool combine_final_walk = result.gpu_walk_m != 0 && !result.plan_legs.empty() && result.plan_legs.back().kind == RTEGPU_STATE_TRANSFER_WALK;
    size_t visible_legs = result.plan_legs.size() - (combine_final_walk ? 1U : 0U);
    for (size_t i = 0; i < visible_legs; ++i) {
        const RteGpuPlanLeg &leg = result.plan_legs[i];
        std::string prefix = std::to_string(i + 1U) + ". ";
        if (leg.kind == RTEGPU_STATE_ORIGIN_WALK) {
            lines.push_back(prefix + "walk " + std::to_string(leg.walk_m) + " m to " + tui_stop_name_colored(pack, use_color, leg.alight) + " arrive " + tui_time(leg.arrival));
        } else if (leg.kind == RTEGPU_STATE_TRANSFER_WALK) {
            lines.push_back(prefix + "walk " + std::to_string(leg.walk_m) + " m from " + tui_stop_name_colored(pack, use_color, leg.board) + " at " + tui_time(leg.departure) + " to " + tui_stop_name_colored(pack, use_color, leg.alight));
        } else if (leg.kind == RTEGPU_STATE_VEHICLE) {
            lines.push_back(prefix + "take " + tui_route_label_colored(pack, use_color, leg.mode, leg.route) + " from " + tui_stop_name_colored(pack, use_color, leg.board) + " at " + tui_time(leg.departure) + " to " + tui_stop_name_colored(pack, use_color, leg.alight) + " arrive " + tui_time(leg.arrival));
        }
    }
    if (combine_final_walk) {
        const RteGpuPlanLeg &leg = result.plan_legs.back();
        lines.push_back(std::to_string(visible_legs + 1U) + ". walk " + std::to_string(leg.walk_m + result.gpu_walk_m) + " m from " + tui_stop_name_colored(pack, use_color, leg.board) + " at " + tui_time(leg.departure) + " to destination arrive " + tui_time(result.gpu_best_arrival));
        return lines;
    }
    if (result.gpu_walk_m != 0) lines.push_back(std::to_string(lines.size()) + ". walk " + std::to_string(result.gpu_walk_m) + " m to destination arrive " + tui_time(result.gpu_best_arrival));
    return lines;
}

static std::string tui_depart_text(uint32_t date, uint32_t seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u", date / 10000U, (date / 100U) % 100U, date % 100U, seconds / 3600U, (seconds / 60U) % 60U, seconds % 60U);
    return std::string(buffer);
}

static bool tui_adjust_depart_text(std::string &depart, int delta_seconds) {
    uint32_t date = 0;
    uint32_t seconds = 0;
    if (!rtegpu_parse_depart(depart.c_str(), &date, &seconds)) return false;
    int64_t absolute = (int64_t)rtegpu_date_to_day(date) * 86400ll + (int64_t)seconds + (int64_t)delta_seconds;
    int64_t day = absolute / 86400ll;
    int64_t rem = absolute % 86400ll;
    if (rem < 0) {
        rem += 86400ll;
        day -= 1;
    }
    depart = tui_depart_text(rtegpu_day_to_date((int)day), (uint32_t)rem);
    return true;
}

static void tui_host_write_ansi_clipped(const std::string &text, unsigned int max_columns) {
    unsigned int columns = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == 27U && i + 1U < text.size() && text[i + 1U] == '[') {
            size_t end = i + 2U;
            while (end < text.size() && !(((unsigned char)text[end] >= 0x40U) && ((unsigned char)text[end] <= 0x7eU))) end += 1U;
            if (end < text.size()) end += 1U;
            tui_host_write_string(text.substr(i, end - i));
            i = end;
            continue;
        }
        if (columns >= max_columns) break;
        size_t char_len = 1U;
        if ((ch & 0xe0U) == 0xc0U) char_len = 2U;
        else if ((ch & 0xf0U) == 0xe0U) char_len = 3U;
        else if ((ch & 0xf8U) == 0xf0U) char_len = 4U;
        if (i + char_len > text.size()) char_len = text.size() - i;
        tui_host_write_string(text.substr(i, char_len));
        i += char_len;
        columns += 1U;
    }
    tui_host_style("\033[0m");
}

static void tui_draw_field(unsigned int row, const char *label, const std::string &value, bool active, unsigned int width) {
    tui_host_move(row, 2);
    tui_host_style(active ? "\033[7m" : "\033[36m");
    tui_host_write(label);
    tui_host_style("\033[0m");
    tui_host_write(" ");
    std::string shown = value;
    if (shown.size() > width) shown = shown.substr(shown.size() - width);
    tui_host_write_string(shown);
    for (unsigned int i = tui_display_columns(shown, shown.size()); i < width; ++i) tui_host_write(" ");
}

static void tui_render(const RteGpuPack &pack, RteGpuTuiTerminal &terminal, const RteGpuTuiState &state, const RteGpuQueryTiming &setup_timing) {
    tui_host_refresh_size(terminal);
    unsigned int field_width = terminal.columns > 16 ? terminal.columns - 16 : 20;
    tui_host_write("\033[?25l\033[H\033[2J");
    tui_host_move(1, 2);
    tui_host_style("\033[1;35m");
    tui_host_write("RTE GPU Route");
    tui_host_style("\033[0m");
    tui_host_move(1, 20);
    char setup[128];
    std::snprintf(setup, sizeof(setup), "resident load %.0f ms, gpu copy %.0f ms", setup_timing.load_ms, setup_timing.copy_ms);
    tui_host_write(setup);
    tui_draw_field(3, "From  ", state.from, state.active == 0, field_width);
    tui_draw_field(4, "To    ", state.to, state.active == 1, field_width);
    tui_draw_field(5, "Depart", state.depart, state.active == 2, field_width);
    tui_host_move(7, 2);
    tui_host_style("\033[2m");
    std::string help = "Tab/Up/Down switch fields, Enter runs relaxed lookup, Ctrl-Q or Esc exits. Live edits use the sidecar.";
    if (help.size() + 2U > terminal.columns) help.resize(terminal.columns > 5 ? terminal.columns - 5 : terminal.columns);
    tui_host_write_string(help);
    tui_host_style("\033[0m");
    tui_host_move(8, 2);
    tui_host_style(state.have_result ? "\033[32m" : "\033[33m");
    std::string status = state.status;
    if (status.size() + 2U > terminal.columns) status.resize(terminal.columns > 5 ? terminal.columns - 5 : terminal.columns);
    tui_host_write_string(status);
    tui_host_style("\033[0m");
    tui_host_move(10, 1);
    for (unsigned int i = 0; i < terminal.columns; ++i) tui_host_write("-");
    unsigned int row = 11;
    if (state.have_result) {
        char summary[160];
        std::snprintf(summary, sizeof(summary), "address %.3f ms | kernel %.3f ms | origin stops %u | destination stops %u", state.result.timing.address_ms, state.result.timing.gpu_ms, state.result.candidate_origin_stops, state.result.candidate_destination_stops);
        tui_host_move(row++, 2);
        tui_host_style("\033[1;36m");
        tui_host_write(summary);
        tui_host_style("\033[0m");
        std::vector<std::string> lines = tui_plan_lines_colored(pack, state.result, state.use_color);
        for (const std::string &line : lines) {
            if (row > terminal.rows) break;
            tui_host_move(row++, 2);
            tui_host_write_ansi_clipped(line, terminal.columns > 5 ? terminal.columns - 5 : terminal.columns);
        }
    }
    const std::string *field = state.active == 0 ? &state.from : (state.active == 1 ? &state.to : &state.depart);
    unsigned int cursor_column = 9U + tui_display_columns(*field, state.cursor[state.active]);
    if (cursor_column > terminal.columns) cursor_column = terminal.columns;
    tui_host_move(3U + state.active, cursor_column);
    tui_host_write("\033[?25h");
    std::fflush(stdout);
}

static void tui_try_route(const RteGpuPack &pack, RteGpuDeviceContext &device, const QueryOptions &base_options, RteGpuTuiState &state, bool allow_relaxed_scan, bool reuse_address_cache) {
    if (state.from.empty() || state.to.empty() || state.depart.empty()) {
        state.have_result = false;
        state.status = "Fill From, To, and Depart to route.";
        return;
    }
    QueryOptions query = base_options;
    query.from_text = state.from.c_str();
    query.to_text = state.to.c_str();
    query.show_plan = true;
    query.verify = false;
    query.json = false;
    query.interactive = true;
    query.address_index_only = !allow_relaxed_scan;
    query.have_from_coord = false;
    query.have_to_coord = false;
    query.from_candidate_lat.clear();
    query.from_candidate_lon.clear();
    query.to_candidate_lat.clear();
    query.to_candidate_lon.clear();
    query.resolved_addresses = false;
    query.address_index_used = false;
    query.address_index_checked = false;
    query.address_index_status = "not_used";
    if (!rtegpu_parse_depart(state.depart.c_str(), &query.depart_date, &query.depart_seconds)) {
        state.have_result = false;
        state.status = "Depart must look like YYYY-MM-DDTHH:MM[:SS].";
        return;
    }
    try {
        auto address_start = std::chrono::steady_clock::now();
        if (reuse_address_cache && tui_address_cache_matches(state)) {
            tui_apply_address_cache(state, query);
        } else {
            resolve_address_queries(query);
            tui_store_address_cache(state, query);
        }
        auto address_end = std::chrono::steady_clock::now();
        RteGpuQueryTiming timing;
        timing.address_ms = std::chrono::duration<double, std::milli>(address_end - address_start).count();
        timing.load_ms = 0.0;
        timing.copy_ms = 0.0;
        RteGpuRouteResult result;
        compute_route_query_silent(pack, device, query, timing, result);
        state.result = result;
        state.have_result = true;
        state.status = std::string("Route updated: ") + query.address_index_status + ", " + std::to_string(query.from_match_count) + "/" + std::to_string(query.to_match_count) + " address matches.";
    } catch (const std::exception &e) {
        state.have_result = false;
        if (!allow_relaxed_scan && std::strcmp(e.what(), "address not found in current address index") == 0) {
            state.status = "Not in sidecar. Press Enter for relaxed scan, or rebuild .addridx for live lookup.";
        } else {
            state.status = e.what();
        }
    }
}

static void tui_insert_text(std::string &field, size_t &cursor, const std::string &text) {
    field.insert(cursor, text);
    cursor += text.size();
}

static void tui_backspace(std::string &field, size_t &cursor) {
    if (cursor == 0) return;
    size_t previous = tui_previous_utf8(field, cursor);
    field.erase(previous, cursor - previous);
    cursor = previous;
}

static void tui_delete(std::string &field, size_t &cursor) {
    if (cursor >= field.size()) return;
    size_t next = tui_next_utf8(field, cursor);
    field.erase(cursor, next - cursor);
}

static int run_tui_mode(const RteGpuPack &pack, RteGpuDeviceContext &device, QueryOptions &options, const RteGpuQueryTiming &setup_timing) {
    RteGpuTuiTerminal terminal;
    if (!tui_host_open(terminal)) throw std::runtime_error("--tui requires an interactive terminal");
    RteGpuTuiState state;
    state.from = options.from_text != nullptr ? options.from_text : "";
    state.to = options.to_text != nullptr ? options.to_text : "";
    state.depart = tui_depart_text(options.depart_date, options.depart_seconds);
    state.use_color = options.use_color;
    state.cursor[0] = state.from.size();
    state.cursor[1] = state.to.size();
    state.cursor[2] = state.depart.size();
    state.status = "Ready.";
    tui_try_route(pack, device, options, state, false, false);
    bool running = true;
    while (running) {
        tui_render(pack, terminal, state, setup_timing);
        RteGpuTuiKey key = tui_read_host_key();
        std::string *field = state.active == 0 ? &state.from : (state.active == 1 ? &state.to : &state.depart);
        size_t *cursor = &state.cursor[state.active];
        bool dirty = false;
        bool force_relaxed_scan = false;
        bool reuse_address_cache = false;
        bool address_text_changed = false;
        switch (key.kind) {
            case RteGpuTuiKey::CtrlQ:
            case RteGpuTuiKey::Escape:
                running = false;
                break;
            case RteGpuTuiKey::Tab:
            case RteGpuTuiKey::Down:
                state.active = (state.active + 1U) % 3U;
                break;
            case RteGpuTuiKey::Enter:
                dirty = true;
                if (state.active == 2 && tui_address_cache_matches(state)) reuse_address_cache = true;
                else force_relaxed_scan = true;
                break;
            case RteGpuTuiKey::Up:
                state.active = (state.active + 2U) % 3U;
                break;
            case RteGpuTuiKey::Left:
                if (state.active == 2) {
                    if (tui_adjust_depart_text(state.depart, -300)) {
                        state.cursor[2] = state.depart.size();
                        dirty = true;
                        reuse_address_cache = tui_address_cache_matches(state);
                    } else {
                        state.status = "Depart must look like YYYY-MM-DDTHH:MM[:SS].";
                    }
                } else {
                    *cursor = tui_previous_utf8(*field, *cursor);
                }
                break;
            case RteGpuTuiKey::Right:
                if (state.active == 2) {
                    if (tui_adjust_depart_text(state.depart, 300)) {
                        state.cursor[2] = state.depart.size();
                        dirty = true;
                        reuse_address_cache = tui_address_cache_matches(state);
                    } else {
                        state.status = "Depart must look like YYYY-MM-DDTHH:MM[:SS].";
                    }
                } else {
                    *cursor = tui_next_utf8(*field, *cursor);
                }
                break;
            case RteGpuTuiKey::Home:
                *cursor = 0;
                break;
            case RteGpuTuiKey::End:
                *cursor = field->size();
                break;
            case RteGpuTuiKey::Backspace:
                tui_backspace(*field, *cursor);
                dirty = true;
                address_text_changed = state.active != 2;
                break;
            case RteGpuTuiKey::DeleteKey:
                tui_delete(*field, *cursor);
                dirty = true;
                address_text_changed = state.active != 2;
                break;
            case RteGpuTuiKey::Text:
                tui_insert_text(*field, *cursor, key.text);
                dirty = true;
                address_text_changed = state.active != 2;
                break;
            default:
                break;
        }
        if (dirty) {
            if (address_text_changed) tui_clear_address_cache(state);
            else if (state.active == 2 && tui_address_cache_matches(state)) reuse_address_cache = true;
            if (force_relaxed_scan && !reuse_address_cache) {
                state.have_result = false;
                state.status = "Running relaxed address scan...";
                tui_render(pack, terminal, state, setup_timing);
            }
            tui_try_route(pack, device, options, state, force_relaxed_scan, reuse_address_cache);
        }
    }
    tui_host_close(terminal);
    return 0;
}

int main(int argc, char **argv) {
    QueryOptions options = parse_args(argc, argv);
    try {
        if (options.build_address_index) {
            auto address_start = std::chrono::steady_clock::now();
            std::string index_path = default_address_index_path(options);
            options.address_index_entries = build_address_index_file(options.rte_path, index_path.c_str());
            options.address_index_status = "built";
            options.address_index_checked = true;
            if (options.from_text == nullptr && options.to_text == nullptr) {
                auto address_end = std::chrono::steady_clock::now();
                double address_ms = std::chrono::duration<double, std::milli>(address_end - address_start).count();
                if (options.json) {
                    std::printf("{\"address_index\":{\"status\":\"built\",\"path\":");
                    json_write_cstr(index_path.c_str());
                    std::printf(",\"entries\":%llu},\"timing_ms\":{\"address_index_build\":%.3f}}\n", (unsigned long long)options.address_index_entries, address_ms);
                } else {
                    std::printf("address_index_status: built\n");
                    std::printf("address_index_path: %s\n", index_path.c_str());
                    std::printf("address_index_entries: %llu\n", (unsigned long long)options.address_index_entries);
                    std::printf("address_index_build_ms: %.3f\n", address_ms);
                }
                return 0;
            }
        }

        RteGpuQueryTiming setup_timing;
        if (!options.interactive && options.rte_path != nullptr && options.from_text != nullptr && options.to_text != nullptr) {
            auto address_start = std::chrono::steady_clock::now();
            resolve_address_queries(options);
            auto address_end = std::chrono::steady_clock::now();
            setup_timing.address_ms = std::chrono::duration<double, std::milli>(address_end - address_start).count();
        }

        auto load_start = std::chrono::steady_clock::now();
        RteGpuPack pack = rtegpu_load_pack(options.pack_path, !options.show_plan);
        auto load_end = std::chrono::steady_clock::now();
        setup_timing.load_ms = std::chrono::duration<double, std::milli>(load_end - load_start).count();
        if (pack.header.stop_count > UINT32_MAX || pack.header.trip_count > UINT32_MAX || pack.header.service_count > UINT32_MAX || pack.header.event_count > UINT32_MAX) throw std::runtime_error("pack too large for prototype");
        RteGpuDeviceContext device;
        setup_timing.copy_ms = rtegpu_device_init(pack, options.show_plan, device);
        if (options.api) {
            int status = run_api_mode(pack, device, options, setup_timing);
            rtegpu_device_free(device);
            return status;
        }
        if (options.tui) {
            int status = run_tui_mode(pack, device, options, setup_timing);
            rtegpu_device_free(device);
            return status;
        }
        if (options.interactive) {
            std::fprintf(stderr, "rte-gpu-route: interactive ready load_ms=%.3f host_to_device_ms=%.3f input=FROM<TAB>TO\n", setup_timing.load_ms, setup_timing.copy_ms);
            std::string line;
            uint32_t query_index = 0;
            while (std::getline(std::cin, line)) {
                if (line == "quit" || line == "exit") break;
                if (line.empty()) continue;
                std::string from;
                std::string to;
                if (!split_interactive_query_line(line, from, to)) {
                    std::fprintf(stderr, "rte-gpu-route: expected FROM<TAB>TO\n");
                    continue;
                }
                QueryOptions query = options;
                query.from_text = from.c_str();
                query.to_text = to.c_str();
                query.have_from_coord = false;
                query.have_to_coord = false;
                query.from_candidate_lat.clear();
                query.from_candidate_lon.clear();
                query.to_candidate_lat.clear();
                query.to_candidate_lon.clear();
                query.from_stop = RTEGPU_NO_INDEX;
                query.to_stop = RTEGPU_NO_INDEX;
                query.resolved_addresses = false;
                query.from_match_count = 0;
                query.to_match_count = 0;
                query.address_index_used = false;
                query.address_index_checked = false;
                query.address_index_status = "not_used";
                auto address_start = std::chrono::steady_clock::now();
                resolve_address_queries(query);
                auto address_end = std::chrono::steady_clock::now();
                RteGpuQueryTiming query_timing;
                query_timing.address_ms = std::chrono::duration<double, std::milli>(address_end - address_start).count();
                query_timing.load_ms = 0.0;
                query_timing.copy_ms = 0.0;
                if (!query.json) std::printf("interactive_query: %u\n", ++query_index);
                int status = run_route_query(pack, device, query, query_timing);
                if (status != 0) std::fprintf(stderr, "rte-gpu-route: query %u exited with status %d\n", query_index, status);
            }
            rtegpu_device_free(device);
            return 0;
        }
        int status = run_route_query(pack, device, options, setup_timing);
        rtegpu_device_free(device);
        return status;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rte-gpu-route: %s\n", e.what());
        return 1;
    }
}
