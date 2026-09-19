#include "rtegpu_common.h"

#include <ctime>
#include <iostream>
#include <unordered_map>

struct OsmrteSection {
    uint32_t type = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t count = 0;
    uint32_t record_size = 0;
};

struct OsmrteTransitOffsets {
    uint64_t stops = 0;
    uint64_t routes = 0;
    uint64_t services = 0;
    uint64_t exceptions = 0;
    uint64_t trips = 0;
    uint64_t events = 0;
    uint64_t strings = 0;
};

static void usage(const char *program) {
    std::fprintf(stderr, "Usage: %s FILE.rte OUT.rtegpu\n", program);
}

static OsmrteSection find_transit_section(std::ifstream &file) {
    unsigned char header[OSMRTE_HEADER_SIZE];
    rtegpu_read_at(file, 0, header, sizeof(header));
    if (std::memcmp(header, "OSMRTE01", 8) != 0) throw std::runtime_error("not an OSMRTE01 file");
    if (rtegpu_read_u32(header + 12) != OSMRTE_HEADER_SIZE) throw std::runtime_error("unsupported OSMRTE header");
    uint64_t section_offset = rtegpu_read_u64(header + 64);
    uint32_t section_count = rtegpu_read_u32(header + 72);
    uint32_t section_record_size = rtegpu_read_u32(header + 76);
    if (section_record_size != OSMRTE_SECTION_RECORD_SIZE) throw std::runtime_error("unsupported OSMRTE section record size");
    for (uint32_t i = 0; i < section_count; ++i) {
        unsigned char bytes[OSMRTE_SECTION_RECORD_SIZE];
        rtegpu_read_at(file, section_offset + (uint64_t)i * OSMRTE_SECTION_RECORD_SIZE, bytes, sizeof(bytes));
        uint32_t type = rtegpu_read_u32(bytes + 0);
        if (type == OSMRTE_SECTION_TRANSIT_STOPS) {
            OsmrteSection section;
            section.type = type;
            section.offset = rtegpu_read_u64(bytes + 8);
            section.size = rtegpu_read_u64(bytes + 16);
            section.count = rtegpu_read_u64(bytes + 32);
            section.record_size = rtegpu_read_u32(bytes + 40);
            return section;
        }
    }
    throw std::runtime_error("OSMRTE file has no transit section");
}

static void read_transit_header(std::ifstream &file, const OsmrteSection &section, RteGpuHeader &header, OsmrteTransitOffsets &offsets, uint32_t &string_size, uint32_t &exception_count_out) {
    unsigned char bytes[OSMRTE_TRANSIT_SECTION_HEADER_SIZE];
    if (section.record_size != OSMRTE_TRANSIT_STOP_RECORD_SIZE) throw std::runtime_error("unexpected OSMRTE transit record size");
    rtegpu_read_at(file, section.offset, bytes, sizeof(bytes));
    if (std::memcmp(bytes, "GTFSLEG1", 8) != 0) throw std::runtime_error("unsupported OSMRTE transit payload");
    if (rtegpu_read_u32(bytes + 12) != OSMRTE_TRANSIT_SECTION_HEADER_SIZE) throw std::runtime_error("unsupported OSMRTE transit header");
    header.stop_count = rtegpu_read_u64(bytes + 16);
    header.route_count = rtegpu_read_u64(bytes + 24);
    header.service_count = rtegpu_read_u64(bytes + 32);
    header.transfer_edge_count = 0;
    uint64_t exception_count = rtegpu_read_u64(bytes + 40);
    header.trip_count = rtegpu_read_u64(bytes + 48);
    header.event_count = rtegpu_read_u64(bytes + 56);
    string_size = rtegpu_read_u32(bytes + 64);
    offsets.stops = rtegpu_read_u64(bytes + 72);
    offsets.routes = rtegpu_read_u64(bytes + 80);
    offsets.services = rtegpu_read_u64(bytes + 88);
    offsets.exceptions = rtegpu_read_u64(bytes + 96);
    offsets.trips = rtegpu_read_u64(bytes + 104);
    offsets.events = rtegpu_read_u64(bytes + 112);
    offsets.strings = rtegpu_read_u64(bytes + 120);
    if (header.stop_count > UINT32_MAX || header.route_count > UINT32_MAX || header.service_count > UINT32_MAX || exception_count > UINT32_MAX || header.trip_count > UINT32_MAX || header.event_count > UINT32_MAX) throw std::runtime_error("RTEGPU prototype only supports 32-bit counts");
    exception_count_out = (uint32_t)exception_count;
}

static int transfer_cell_for_e7(int value) {
    const int cell_e7 = 50000;
    if (value >= 0) return value / cell_e7;
    return -(((-value) + cell_e7 - 1) / cell_e7);
}

static uint64_t transfer_cell_key(int lat_cell, int lon_cell) {
    return ((uint64_t)(uint32_t)lat_cell << 32) | (uint32_t)lon_cell;
}

static void build_transfer_csr(const std::vector<int32_t> &stop_lat, const std::vector<int32_t> &stop_lon, std::vector<uint32_t> &transfer_offset, std::vector<uint32_t> &transfer_to, std::vector<uint32_t> &transfer_walk_sec) {
    const uint32_t transfer_walk_m = 800;
    const int radius = 4;
    uint32_t stop_count = (uint32_t)stop_lat.size();
    std::unordered_map<uint64_t, std::vector<uint32_t> > cells;
    std::vector<uint32_t> counts(stop_count, 0);

    cells.reserve(stop_count * 2u);
    for (uint32_t i = 0; i < stop_count; ++i) {
        int lat_cell = transfer_cell_for_e7(stop_lat[i]);
        int lon_cell = transfer_cell_for_e7(stop_lon[i]);
        cells[transfer_cell_key(lat_cell, lon_cell)].push_back(i);
    }
    for (uint32_t i = 0; i < stop_count; ++i) {
        int lat_cell = transfer_cell_for_e7(stop_lat[i]);
        int lon_cell = transfer_cell_for_e7(stop_lon[i]);
        for (int dlat = -radius; dlat <= radius; ++dlat) {
            for (int dlon = -radius; dlon <= radius; ++dlon) {
                auto found = cells.find(transfer_cell_key(lat_cell + dlat, lon_cell + dlon));
                if (found == cells.end()) continue;
                const std::vector<uint32_t> &bucket = found->second;
                for (uint32_t to : bucket) {
                    if (to == i) continue;
                    uint32_t meters = rtegpu_direct_distance_m(stop_lat[i], stop_lon[i], stop_lat[to], stop_lon[to]);
                    if (meters <= transfer_walk_m) counts[i] += 1;
                }
            }
        }
        if ((i + 1u) % 100000u == 0 || i + 1u == stop_count) std::fprintf(stderr, "rte-to-rtegpu: transfer_count %u/%u\n", i + 1u, stop_count);
    }
    transfer_offset.resize((size_t)stop_count + 1u);
    transfer_offset[0] = 0;
    for (uint32_t i = 0; i < stop_count; ++i) {
        if (transfer_offset[i] > UINT32_MAX - counts[i]) throw std::runtime_error("too many transfer edges");
        transfer_offset[i + 1u] = transfer_offset[i] + counts[i];
    }
    transfer_to.resize(transfer_offset[stop_count]);
    transfer_walk_sec.resize(transfer_offset[stop_count]);
    std::vector<uint32_t> cursor = transfer_offset;
    for (uint32_t i = 0; i < stop_count; ++i) {
        int lat_cell = transfer_cell_for_e7(stop_lat[i]);
        int lon_cell = transfer_cell_for_e7(stop_lon[i]);
        for (int dlat = -radius; dlat <= radius; ++dlat) {
            for (int dlon = -radius; dlon <= radius; ++dlon) {
                auto found = cells.find(transfer_cell_key(lat_cell + dlat, lon_cell + dlon));
                if (found == cells.end()) continue;
                const std::vector<uint32_t> &bucket = found->second;
                for (uint32_t to : bucket) {
                    if (to == i) continue;
                    uint32_t meters = rtegpu_direct_distance_m(stop_lat[i], stop_lon[i], stop_lat[to], stop_lon[to]);
                    if (meters <= transfer_walk_m) {
                        uint32_t edge = cursor[i]++;
                        transfer_to[edge] = to;
                        transfer_walk_sec[edge] = rtegpu_walking_seconds(meters);
                    }
                }
            }
        }
        if ((i + 1u) % 100000u == 0 || i + 1u == stop_count) std::fprintf(stderr, "rte-to-rtegpu: transfer_fill %u/%u\n", i + 1u, stop_count);
    }
}

static void add_section(std::vector<RteGpuSection> &sections, uint32_t type, uint64_t count, uint32_t record_size, uint64_t &cursor) {
    RteGpuSection section;
    cursor = rtegpu_align_u64(cursor, 128);
    section.type = type;
    section.offset = cursor;
    section.count = count;
    section.record_size = record_size;
    section.size = count * (uint64_t)record_size;
    sections.push_back(section);
    cursor += section.size;
}

static void write_array(std::ofstream &out, uint64_t &cursor, const RteGpuSection &section, const void *data) {
    rtegpu_write_padding(out, cursor, section.offset);
    cursor = section.offset;
    if (section.size != 0) out.write((const char *)data, (std::streamsize)section.size);
    cursor += section.size;
}

static void write_u8_array(std::ofstream &out, uint64_t &cursor, const RteGpuSection &section, const std::vector<uint8_t> &data) {
    write_array(out, cursor, section, data.empty() ? nullptr : data.data());
}

static void write_u32_array(std::ofstream &out, uint64_t &cursor, const RteGpuSection &section, const std::vector<uint32_t> &data) {
    write_array(out, cursor, section, data.empty() ? nullptr : data.data());
}

static void write_i32_array(std::ofstream &out, uint64_t &cursor, const RteGpuSection &section, const std::vector<int32_t> &data) {
    write_array(out, cursor, section, data.empty() ? nullptr : data.data());
}

static bool service_active_on_date(const std::vector<uint32_t> &service_start, const std::vector<uint32_t> &service_end, const std::vector<uint32_t> &service_dow, const std::vector<uint32_t> &service_exception_offset, const std::vector<uint32_t> &service_exception_count, const std::vector<uint32_t> &exception_service, const std::vector<uint32_t> &exception_date, const std::vector<uint32_t> &exception_type, uint32_t service_index, uint32_t date) {
    if (service_index >= service_start.size()) return false;
    uint32_t begin = service_exception_offset[service_index];
    uint32_t count = service_exception_count[service_index];
    for (uint32_t i = 0; i < count && begin + i < exception_date.size(); ++i) {
        uint32_t exception_index = begin + i;
        (void)exception_service;
        if (exception_date[exception_index] == date) return exception_type[exception_index] == 1;
    }
    if (service_start[service_index] == 0 || service_end[service_index] == 0 || date < service_start[service_index] || date > service_end[service_index]) return false;
    return (service_dow[service_index] & (1u << rtegpu_day_of_week_monday0(date))) != 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    try {
        const char *input_path = argv[1];
        const char *output_path = argv[2];
        std::ifstream input(input_path, std::ios::binary);
        if (!input) throw std::runtime_error("could not open input .rte");

        OsmrteSection transit_section = find_transit_section(input);
        RteGpuHeader header;
        OsmrteTransitOffsets offsets;
        uint32_t string_size = 0;
        uint32_t exception_count = 0;
        read_transit_header(input, transit_section, header, offsets, string_size, exception_count);

        uint32_t stop_count = (uint32_t)header.stop_count;
        uint32_t route_count = (uint32_t)header.route_count;
        uint32_t service_count = (uint32_t)header.service_count;
        uint32_t trip_count = (uint32_t)header.trip_count;
        uint32_t event_count = (uint32_t)header.event_count;

        std::vector<int32_t> stop_lat(stop_count), stop_lon(stop_count);
        std::vector<uint32_t> stop_mode(stop_count), stop_name_offset(stop_count), stop_name_size(stop_count);
        std::vector<uint32_t> route_short_offset(route_count), route_short_size(route_count), route_long_offset(route_count), route_long_size(route_count), route_mode(route_count), route_type(route_count);
        std::vector<uint32_t> service_start(service_count), service_end(service_count), service_dow(service_count), service_exception_offset(service_count), service_exception_count(service_count);
        std::vector<uint32_t> exception_service(exception_count), exception_date(exception_count), exception_type(exception_count);
        std::vector<uint32_t> trip_route(trip_count), trip_service(trip_count), trip_mode(trip_count), trip_event_offset(trip_count, UINT32_MAX), trip_event_count(trip_count, 0);
        std::vector<uint32_t> event_stop(event_count), event_arrival(event_count), event_departure(event_count);
        std::vector<uint32_t> transfer_offset, transfer_to, transfer_walk_sec;
        std::vector<char> strings(string_size);

        std::vector<unsigned char> raw;
        raw.resize(std::max<uint32_t>(OSMRTE_TRANSIT_STOP_RECORD_SIZE, std::max<uint32_t>(OSMRTE_TRANSIT_ROUTE_RECORD_SIZE, std::max<uint32_t>(OSMRTE_TRANSIT_SERVICE_RECORD_SIZE, OSMRTE_TRANSIT_TRIP_RECORD_SIZE))) * 1024u);

        for (uint32_t i = 0; i < stop_count; ++i) {
            unsigned char bytes[OSMRTE_TRANSIT_STOP_RECORD_SIZE];
            rtegpu_read_at(input, transit_section.offset + offsets.stops + (uint64_t)i * OSMRTE_TRANSIT_STOP_RECORD_SIZE, bytes, sizeof(bytes));
            stop_name_offset[i] = rtegpu_read_u32(bytes + 8);
            stop_name_size[i] = rtegpu_read_u32(bytes + 12);
            stop_lat[i] = rtegpu_read_i32(bytes + 16);
            stop_lon[i] = rtegpu_read_i32(bytes + 20);
            stop_mode[i] = rtegpu_read_u32(bytes + 32);
        }

        for (uint32_t i = 0; i < route_count; ++i) {
            unsigned char bytes[OSMRTE_TRANSIT_ROUTE_RECORD_SIZE];
            rtegpu_read_at(input, transit_section.offset + offsets.routes + (uint64_t)i * OSMRTE_TRANSIT_ROUTE_RECORD_SIZE, bytes, sizeof(bytes));
            route_short_offset[i] = rtegpu_read_u32(bytes + 0);
            route_short_size[i] = rtegpu_read_u32(bytes + 4);
            route_long_offset[i] = rtegpu_read_u32(bytes + 8);
            route_long_size[i] = rtegpu_read_u32(bytes + 12);
            route_mode[i] = rtegpu_read_u32(bytes + 16);
            route_type[i] = rtegpu_read_u32(bytes + 20);
        }

        for (uint32_t i = 0; i < service_count; ++i) {
            unsigned char bytes[OSMRTE_TRANSIT_SERVICE_RECORD_SIZE];
            rtegpu_read_at(input, transit_section.offset + offsets.services + (uint64_t)i * OSMRTE_TRANSIT_SERVICE_RECORD_SIZE, bytes, sizeof(bytes));
            service_start[i] = rtegpu_read_u32(bytes + 8);
            service_end[i] = rtegpu_read_u32(bytes + 12);
            service_dow[i] = rtegpu_read_u32(bytes + 16);
            service_exception_offset[i] = rtegpu_read_u32(bytes + 20);
            service_exception_count[i] = rtegpu_read_u32(bytes + 24);
        }

        for (uint32_t i = 0; i < exception_count; ++i) {
            unsigned char bytes[OSMRTE_TRANSIT_EXCEPTION_RECORD_SIZE];
            rtegpu_read_at(input, transit_section.offset + offsets.exceptions + (uint64_t)i * OSMRTE_TRANSIT_EXCEPTION_RECORD_SIZE, bytes, sizeof(bytes));
            exception_service[i] = rtegpu_read_u32(bytes + 0);
            exception_date[i] = rtegpu_read_u32(bytes + 4);
            exception_type[i] = rtegpu_read_u32(bytes + 8);
        }

        uint32_t min_date = UINT32_MAX;
        uint32_t max_date = 0;
        for (uint32_t i = 0; i < service_count; ++i) {
            if (service_start[i] != 0 && service_start[i] < min_date) min_date = service_start[i];
            if (service_end[i] != 0 && service_end[i] > max_date) max_date = service_end[i];
        }
        for (uint32_t i = 0; i < exception_count; ++i) {
            if (exception_date[i] != 0 && exception_date[i] < min_date) min_date = exception_date[i];
            if (exception_date[i] != 0 && exception_date[i] > max_date) max_date = exception_date[i];
        }
        if (min_date == UINT32_MAX || max_date == 0) {
            min_date = max_date = 0;
        }
        header.first_service_date = min_date;
        header.service_date_count = min_date == 0 ? 0 : (uint32_t)(rtegpu_date_to_day(max_date) - rtegpu_date_to_day(min_date) + 1);
        if (header.service_date_count > 3660) throw std::runtime_error("unexpectedly large service date range");

        std::vector<uint8_t> service_active((size_t)header.service_date_count * service_count, 0);
        for (uint32_t day_index = 0; day_index < header.service_date_count; ++day_index) {
            uint32_t date = rtegpu_day_to_date(rtegpu_date_to_day(min_date) + (int)day_index);
            for (uint32_t service_index = 0; service_index < service_count; ++service_index) {
                service_active[(size_t)day_index * service_count + service_index] = service_active_on_date(service_start, service_end, service_dow, service_exception_offset, service_exception_count, exception_service, exception_date, exception_type, service_index, date) ? 1 : 0;
            }
        }

        for (uint32_t i = 0; i < trip_count; ++i) {
            unsigned char bytes[OSMRTE_TRANSIT_TRIP_RECORD_SIZE];
            rtegpu_read_at(input, transit_section.offset + offsets.trips + (uint64_t)i * OSMRTE_TRANSIT_TRIP_RECORD_SIZE, bytes, sizeof(bytes));
            trip_route[i] = rtegpu_read_u32(bytes + 0);
            trip_service[i] = rtegpu_read_u32(bytes + 4);
            trip_mode[i] = rtegpu_read_u32(bytes + 8);
        }

        const uint32_t chunk_records = 1u << 20;
        std::vector<unsigned char> event_raw((size_t)chunk_records * OSMRTE_TRANSIT_EVENT_RECORD_SIZE);
        uint32_t previous_trip = 0;
        bool have_previous_trip = false;
        for (uint32_t base = 0; base < event_count; base += chunk_records) {
            uint32_t count = std::min<uint32_t>(chunk_records, event_count - base);
            rtegpu_read_at(input, transit_section.offset + offsets.events + (uint64_t)base * OSMRTE_TRANSIT_EVENT_RECORD_SIZE, event_raw.data(), (size_t)count * OSMRTE_TRANSIT_EVENT_RECORD_SIZE);
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t event_index = base + j;
                unsigned char *bytes = event_raw.data() + (size_t)j * OSMRTE_TRANSIT_EVENT_RECORD_SIZE;
                uint32_t trip_index = rtegpu_read_u32(bytes + 0);
                if (trip_index >= trip_count) throw std::runtime_error("event references invalid trip");
                if (have_previous_trip && trip_index < previous_trip) throw std::runtime_error("events are not grouped by trip");
                have_previous_trip = true;
                previous_trip = trip_index;
                if (trip_event_count[trip_index] == 0) trip_event_offset[trip_index] = event_index;
                trip_event_count[trip_index] += 1;
                event_stop[event_index] = rtegpu_read_u32(bytes + 4);
                event_arrival[event_index] = rtegpu_read_u32(bytes + 8);
                event_departure[event_index] = rtegpu_read_u32(bytes + 12);
            }
            if ((base + count) % 5000000u == 0 || base + count == event_count) {
                std::fprintf(stderr, "rte-to-rtegpu: events %u/%u\n", base + count, event_count);
            }
        }
        for (uint32_t i = 0; i < trip_count; ++i) if (trip_event_offset[i] == UINT32_MAX) trip_event_offset[i] = 0;
        if (string_size != 0) rtegpu_read_at(input, transit_section.offset + offsets.strings, strings.data(), strings.size());
        build_transfer_csr(stop_lat, stop_lon, transfer_offset, transfer_to, transfer_walk_sec);
        header.transfer_edge_count = transfer_to.size();

        std::vector<RteGpuSection> sections;
        header.section_directory_offset = RTEGPU_HEADER_SIZE;
        header.section_record_size = RTEGPU_SECTION_RECORD_SIZE;
        uint64_t cursor = RTEGPU_HEADER_SIZE + 24ull * RTEGPU_SECTION_RECORD_SIZE;
        add_section(sections, RTEGPU_SEC_STOP_LAT, stop_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_STOP_LON, stop_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_STOP_MODE, stop_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_STOP_NAME_OFFSET, stop_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_STOP_NAME_SIZE, stop_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_TRIP_ROUTE, trip_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_TRIP_SERVICE, trip_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_TRIP_MODE, trip_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_TRIP_EVENT_OFFSET, trip_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_TRIP_EVENT_COUNT, trip_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_EVENT_STOP, event_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_EVENT_ARRIVAL, event_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_EVENT_DEPARTURE, event_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_SERVICE_ACTIVE, service_active.size(), 1, cursor);
        add_section(sections, RTEGPU_SEC_ROUTE_SHORT_OFFSET, route_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_ROUTE_SHORT_SIZE, route_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_ROUTE_LONG_OFFSET, route_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_ROUTE_LONG_SIZE, route_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_ROUTE_MODE, route_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_ROUTE_TYPE, route_count, 4, cursor);
        add_section(sections, RTEGPU_SEC_TRANSFER_OFFSET, transfer_offset.size(), 4, cursor);
        add_section(sections, RTEGPU_SEC_TRANSFER_TO, transfer_to.size(), 4, cursor);
        add_section(sections, RTEGPU_SEC_TRANSFER_WALK_SEC, transfer_walk_sec.size(), 4, cursor);
        add_section(sections, RTEGPU_SEC_STRING_TABLE, string_size, 1, cursor);
        header.section_count = (uint32_t)sections.size();
        header.file_size = cursor;
        header.build_unix_time = (uint64_t)std::time(nullptr);

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("could not open output .rtegpu");
        unsigned char header_bytes[RTEGPU_HEADER_SIZE];
        rtegpu_write_header_bytes(header, header_bytes);
        out.write((const char *)header_bytes, sizeof(header_bytes));
        for (const RteGpuSection &section : sections) {
            unsigned char section_bytes[RTEGPU_SECTION_RECORD_SIZE];
            rtegpu_write_section_bytes(section, section_bytes);
            out.write((const char *)section_bytes, sizeof(section_bytes));
        }
        uint64_t write_cursor = RTEGPU_HEADER_SIZE + (uint64_t)sections.size() * RTEGPU_SECTION_RECORD_SIZE;
        size_t section_index = 0;
        write_i32_array(out, write_cursor, sections[section_index++], stop_lat);
        write_i32_array(out, write_cursor, sections[section_index++], stop_lon);
        write_u32_array(out, write_cursor, sections[section_index++], stop_mode);
        write_u32_array(out, write_cursor, sections[section_index++], stop_name_offset);
        write_u32_array(out, write_cursor, sections[section_index++], stop_name_size);
        write_u32_array(out, write_cursor, sections[section_index++], trip_route);
        write_u32_array(out, write_cursor, sections[section_index++], trip_service);
        write_u32_array(out, write_cursor, sections[section_index++], trip_mode);
        write_u32_array(out, write_cursor, sections[section_index++], trip_event_offset);
        write_u32_array(out, write_cursor, sections[section_index++], trip_event_count);
        write_u32_array(out, write_cursor, sections[section_index++], event_stop);
        write_u32_array(out, write_cursor, sections[section_index++], event_arrival);
        write_u32_array(out, write_cursor, sections[section_index++], event_departure);
        write_u8_array(out, write_cursor, sections[section_index++], service_active);
        write_u32_array(out, write_cursor, sections[section_index++], route_short_offset);
        write_u32_array(out, write_cursor, sections[section_index++], route_short_size);
        write_u32_array(out, write_cursor, sections[section_index++], route_long_offset);
        write_u32_array(out, write_cursor, sections[section_index++], route_long_size);
        write_u32_array(out, write_cursor, sections[section_index++], route_mode);
        write_u32_array(out, write_cursor, sections[section_index++], route_type);
        write_u32_array(out, write_cursor, sections[section_index++], transfer_offset);
        write_u32_array(out, write_cursor, sections[section_index++], transfer_to);
        write_u32_array(out, write_cursor, sections[section_index++], transfer_walk_sec);
        write_array(out, write_cursor, sections[section_index++], strings.empty() ? nullptr : strings.data());
        out.close();
        if (!out) throw std::runtime_error("failed while writing .rtegpu");

        std::printf("format: RTEGPU01\n");
        std::printf("output: %s\n", output_path);
        std::printf("stops: %u\n", stop_count);
        std::printf("routes: %u\n", route_count);
        std::printf("services: %u\n", service_count);
        std::printf("service_dates: %u\n", header.service_date_count);
        std::printf("trips: %u\n", trip_count);
        std::printf("events: %u\n", event_count);
        std::printf("transfer_edges: %u\n", (uint32_t)transfer_to.size());
        std::printf("strings: %u\n", string_size);
        std::printf("file_size: %llu\n", (unsigned long long)header.file_size);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "rte-to-rtegpu: %s\n", e.what());
        return 1;
    }
}
