#ifndef RTEGPU_COMMON_H
#define RTEGPU_COMMON_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define RTEGPU_MAGIC "RTEGPU01"
#define RTEGPU_HEADER_SIZE 256u
#define RTEGPU_SECTION_RECORD_SIZE 64u
#define RTEGPU_COORD_SCALE 10000000u
#define RTEGPU_INF_TIME 0xffffffffu

#define RTEGPU_SEC_STOP_LAT 1u
#define RTEGPU_SEC_STOP_LON 2u
#define RTEGPU_SEC_STOP_MODE 3u
#define RTEGPU_SEC_STOP_NAME_OFFSET 4u
#define RTEGPU_SEC_STOP_NAME_SIZE 5u
#define RTEGPU_SEC_TRIP_ROUTE 10u
#define RTEGPU_SEC_TRIP_SERVICE 11u
#define RTEGPU_SEC_TRIP_MODE 12u
#define RTEGPU_SEC_TRIP_EVENT_OFFSET 13u
#define RTEGPU_SEC_TRIP_EVENT_COUNT 14u
#define RTEGPU_SEC_EVENT_STOP 20u
#define RTEGPU_SEC_EVENT_ARRIVAL 21u
#define RTEGPU_SEC_EVENT_DEPARTURE 22u
#define RTEGPU_SEC_SERVICE_ACTIVE 30u
#define RTEGPU_SEC_ROUTE_SHORT_OFFSET 40u
#define RTEGPU_SEC_ROUTE_SHORT_SIZE 41u
#define RTEGPU_SEC_ROUTE_LONG_OFFSET 42u
#define RTEGPU_SEC_ROUTE_LONG_SIZE 43u
#define RTEGPU_SEC_ROUTE_MODE 44u
#define RTEGPU_SEC_ROUTE_TYPE 45u
#define RTEGPU_SEC_STRING_TABLE 50u
#define RTEGPU_SEC_TRANSFER_OFFSET 60u
#define RTEGPU_SEC_TRANSFER_TO 61u
#define RTEGPU_SEC_TRANSFER_WALK_SEC 62u

#define OSMRTE_HEADER_SIZE 256u
#define OSMRTE_SECTION_RECORD_SIZE 64u
#define OSMRTE_SECTION_TRANSIT_STOPS 0x0500u
#define OSMRTE_TRANSIT_SECTION_HEADER_SIZE 128u
#define OSMRTE_TRANSIT_STOP_RECORD_SIZE 40u
#define OSMRTE_TRANSIT_ROUTE_RECORD_SIZE 24u
#define OSMRTE_TRANSIT_SERVICE_RECORD_SIZE 32u
#define OSMRTE_TRANSIT_EXCEPTION_RECORD_SIZE 16u
#define OSMRTE_TRANSIT_TRIP_RECORD_SIZE 12u
#define OSMRTE_TRANSIT_EVENT_RECORD_SIZE 20u

struct RteGpuHeader {
    uint64_t file_size = 0;
    uint64_t build_unix_time = 0;
    uint64_t stop_count = 0;
    uint64_t route_count = 0;
    uint64_t service_count = 0;
    uint64_t trip_count = 0;
    uint64_t event_count = 0;
    uint64_t transfer_edge_count = 0;
    uint64_t section_directory_offset = RTEGPU_HEADER_SIZE;
    uint32_t section_count = 0;
    uint32_t section_record_size = RTEGPU_SECTION_RECORD_SIZE;
    uint32_t coord_scale = RTEGPU_COORD_SCALE;
    uint32_t max_rounds_hint = 6;
    uint32_t first_service_date = 0;
    uint32_t service_date_count = 0;
};

struct RteGpuSection {
    uint32_t type = 0;
    uint32_t flags = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t count = 0;
    uint32_t record_size = 0;
};

struct RteGpuPack {
    RteGpuHeader header;
    std::vector<RteGpuSection> sections;
    std::vector<int32_t> stop_lat;
    std::vector<int32_t> stop_lon;
    std::vector<uint32_t> stop_mode;
    std::vector<uint32_t> stop_name_offset;
    std::vector<uint32_t> stop_name_size;
    std::vector<uint32_t> trip_route;
    std::vector<uint32_t> trip_service;
    std::vector<uint32_t> trip_mode;
    std::vector<uint32_t> trip_event_offset;
    std::vector<uint32_t> trip_event_count;
    std::vector<uint32_t> event_stop;
    std::vector<uint32_t> event_arrival;
    std::vector<uint32_t> event_departure;
    std::vector<uint8_t> service_active;
    std::vector<uint32_t> route_short_offset;
    std::vector<uint32_t> route_short_size;
    std::vector<uint32_t> route_long_offset;
    std::vector<uint32_t> route_long_size;
    std::vector<uint32_t> route_mode;
    std::vector<uint32_t> route_type;
    std::vector<uint32_t> transfer_offset;
    std::vector<uint32_t> transfer_to;
    std::vector<uint32_t> transfer_walk_sec;
    std::vector<char> strings;
};

static inline uint32_t rtegpu_read_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int32_t rtegpu_read_i32(const unsigned char *p) {
    return (int32_t)rtegpu_read_u32(p);
}

static inline uint64_t rtegpu_read_u64(const unsigned char *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static inline void rtegpu_write_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 255u);
    p[1] = (unsigned char)((v >> 8) & 255u);
    p[2] = (unsigned char)((v >> 16) & 255u);
    p[3] = (unsigned char)((v >> 24) & 255u);
}

static inline void rtegpu_write_u64(unsigned char *p, uint64_t v) {
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)((v >> (i * 8)) & 255u);
}

static inline uint64_t rtegpu_align_u64(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static inline void rtegpu_read_at(std::ifstream &file, uint64_t offset, void *data, size_t size) {
    file.seekg((std::streamoff)offset, std::ios::beg);
    if (!file.good()) throw std::runtime_error("seek failed");
    file.read((char *)data, (std::streamsize)size);
    if ((size_t)file.gcount() != size) throw std::runtime_error("short read");
}

static inline void rtegpu_write_padding(std::ofstream &file, uint64_t current, uint64_t target) {
    unsigned char zeros[4096];
    std::memset(zeros, 0, sizeof(zeros));
    while (current < target) {
        size_t chunk = (size_t)std::min<uint64_t>(sizeof(zeros), target - current);
        file.write((const char *)zeros, (std::streamsize)chunk);
        current += chunk;
    }
}

static inline int rtegpu_days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

static inline void rtegpu_civil_from_days(int z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int yy = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

static inline int rtegpu_date_to_day(uint32_t date) {
    int y = (int)(date / 10000u);
    unsigned m = (date / 100u) % 100u;
    unsigned d = date % 100u;
    return rtegpu_days_from_civil(y, m, d);
}

static inline uint32_t rtegpu_day_to_date(int day) {
    int y;
    unsigned m, d;
    rtegpu_civil_from_days(day, &y, &m, &d);
    return (uint32_t)y * 10000u + m * 100u + d;
}

static inline unsigned rtegpu_day_of_week_monday0(uint32_t date) {
    int z = rtegpu_date_to_day(date);
    int dow = (z + 3) % 7;
    if (dow < 0) dow += 7;
    return (unsigned)dow;
}

static inline bool rtegpu_parse_u32_arg(const char *text, uint32_t *out) {
    char *end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value > std::numeric_limits<uint32_t>::max()) return false;
    *out = (uint32_t)value;
    return true;
}

static inline bool rtegpu_parse_latlon(const char *text, int32_t *lat_e7, int32_t *lon_e7) {
    char *end = nullptr;
    double lat = std::strtod(text, &end);
    if (end == text || *end != ',') return false;
    double lon = std::strtod(end + 1, &end);
    if (*end != '\0') return false;
    *lat_e7 = (int32_t)(lat * 10000000.0 + (lat >= 0.0 ? 0.5 : -0.5));
    *lon_e7 = (int32_t)(lon * 10000000.0 + (lon >= 0.0 ? 0.5 : -0.5));
    return true;
}

static inline bool rtegpu_parse_depart(const char *text, uint32_t *date, uint32_t *seconds) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(text, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 71 || mi < 0 || mi > 59 || s < 0 || s > 59) return false;
    *date = (uint32_t)y * 10000u + (uint32_t)mo * 100u + (uint32_t)d;
    *seconds = (uint32_t)h * 3600u + (uint32_t)mi * 60u + (uint32_t)s;
    return true;
}

static inline uint64_t rtegpu_isqrt_u64(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = 1ull << 62u;
    while (bit > value) bit >>= 2u;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1u) + bit;
        } else {
            result >>= 1u;
        }
        bit >>= 2u;
    }
    return result;
}

static inline uint32_t rtegpu_abs_i32(int32_t value) {
    return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
}

static inline uint32_t rtegpu_cos_degrees_q1000000(uint32_t degrees) {
    static const uint32_t table[91] = {
        1000000u,999848u,999391u,998630u,997564u,996195u,994522u,992546u,990268u,987688u,
        984808u,981627u,978148u,974370u,970296u,965926u,961262u,956305u,951057u,945519u,
        939693u,933580u,927184u,920505u,913545u,906308u,898794u,891007u,882948u,874620u,
        866025u,857167u,848048u,838671u,829038u,819152u,809017u,798636u,788011u,777146u,
        766044u,754710u,743145u,731354u,719340u,707107u,694658u,681998u,669131u,656059u,
        642788u,629320u,615661u,601815u,587785u,573576u,559193u,544639u,529919u,515038u,
        500000u,484810u,469472u,453990u,438371u,422618u,406737u,390731u,374607u,358368u,
        342020u,325568u,309017u,292372u,275637u,258819u,241922u,224951u,207912u,190809u,
        173648u,156434u,139173u,121869u,104528u,87156u,69756u,52336u,34899u,17452u,0u
    };
    if (degrees >= 900000000u) return 0;
    uint32_t whole = degrees / 10000000u;
    uint32_t fraction = degrees % 10000000u;
    if (whole >= 90u) return 0;
    uint32_t left = table[whole];
    uint32_t right = table[whole + 1u];
    return left - (uint32_t)(((uint64_t)(left - right) * fraction) / 10000000ull);
}

static inline uint32_t rtegpu_meters_per_degree_lon_from_lat_e7(int32_t lat_e7) {
    uint32_t cosine = rtegpu_cos_degrees_q1000000(rtegpu_abs_i32(lat_e7));
    return (uint32_t)((111320ull * (uint64_t)cosine + 500000ull) / 1000000ull);
}

static inline uint32_t rtegpu_direct_distance_m(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
    int64_t dlat = (int64_t)lat2 - (int64_t)lat1;
    int64_t dlon = (int64_t)lon2 - (int64_t)lon1;
    uint32_t meters_per_degree_lon = rtegpu_meters_per_degree_lon_from_lat_e7((lat1 + lat2) / 2);
    int64_t dy = (dlat * 111320ll) / 10000000ll;
    int64_t dx = (dlon * (int64_t)meters_per_degree_lon) / 10000000ll;
    uint64_t ax = dx < 0 ? (uint64_t)(-dx) : (uint64_t)dx;
    uint64_t ay = dy < 0 ? (uint64_t)(-dy) : (uint64_t)dy;
    uint64_t distance = rtegpu_isqrt_u64(ax * ax + ay * ay);
    return distance > 0xffffffffull ? 0xffffffffu : (uint32_t)distance;
}

static inline uint32_t rtegpu_walking_seconds(uint32_t meters) {
    return (uint32_t)(((uint64_t)meters * 3600ull + 4799ull) / 4800ull);
}

static inline void rtegpu_format_time(uint32_t sec, char out[16]) {
    std::snprintf(out, 16, "%02u:%02u:%02u", sec / 3600u, (sec / 60u) % 60u, sec % 60u);
}

static inline void rtegpu_write_header_bytes(const RteGpuHeader &h, unsigned char out[RTEGPU_HEADER_SIZE]) {
    std::memset(out, 0, RTEGPU_HEADER_SIZE);
    std::memcpy(out, RTEGPU_MAGIC, 8);
    rtegpu_write_u32(out + 8, 1);
    rtegpu_write_u32(out + 12, RTEGPU_HEADER_SIZE);
    rtegpu_write_u32(out + 16, 0x01020304u);
    rtegpu_write_u32(out + 20, 0);
    rtegpu_write_u64(out + 24, h.file_size);
    rtegpu_write_u64(out + 32, h.build_unix_time);
    rtegpu_write_u64(out + 40, h.stop_count);
    rtegpu_write_u64(out + 48, h.route_count);
    rtegpu_write_u64(out + 56, h.service_count);
    rtegpu_write_u64(out + 64, h.trip_count);
    rtegpu_write_u64(out + 72, h.event_count);
    rtegpu_write_u64(out + 80, h.transfer_edge_count);
    rtegpu_write_u64(out + 88, h.section_directory_offset);
    rtegpu_write_u32(out + 96, h.section_count);
    rtegpu_write_u32(out + 100, h.section_record_size);
    rtegpu_write_u32(out + 104, h.coord_scale);
    rtegpu_write_u32(out + 108, h.max_rounds_hint);
    rtegpu_write_u32(out + 112, h.first_service_date);
    rtegpu_write_u32(out + 116, h.service_date_count);
}

static inline RteGpuHeader rtegpu_parse_header_bytes(const unsigned char in[RTEGPU_HEADER_SIZE]) {
    RteGpuHeader h;
    if (std::memcmp(in, RTEGPU_MAGIC, 8) != 0) throw std::runtime_error("not an RTEGPU01 file");
    if (rtegpu_read_u32(in + 8) != 1 || rtegpu_read_u32(in + 12) != RTEGPU_HEADER_SIZE) throw std::runtime_error("unsupported RTEGPU header");
    h.file_size = rtegpu_read_u64(in + 24);
    h.build_unix_time = rtegpu_read_u64(in + 32);
    h.stop_count = rtegpu_read_u64(in + 40);
    h.route_count = rtegpu_read_u64(in + 48);
    h.service_count = rtegpu_read_u64(in + 56);
    h.trip_count = rtegpu_read_u64(in + 64);
    h.event_count = rtegpu_read_u64(in + 72);
    h.transfer_edge_count = rtegpu_read_u64(in + 80);
    h.section_directory_offset = rtegpu_read_u64(in + 88);
    h.section_count = rtegpu_read_u32(in + 96);
    h.section_record_size = rtegpu_read_u32(in + 100);
    h.coord_scale = rtegpu_read_u32(in + 104);
    h.max_rounds_hint = rtegpu_read_u32(in + 108);
    h.first_service_date = rtegpu_read_u32(in + 112);
    h.service_date_count = rtegpu_read_u32(in + 116);
    if (h.section_record_size != RTEGPU_SECTION_RECORD_SIZE) throw std::runtime_error("unsupported RTEGPU section record size");
    return h;
}

static inline void rtegpu_write_section_bytes(const RteGpuSection &s, unsigned char out[RTEGPU_SECTION_RECORD_SIZE]) {
    std::memset(out, 0, RTEGPU_SECTION_RECORD_SIZE);
    rtegpu_write_u32(out + 0, s.type);
    rtegpu_write_u32(out + 4, s.flags);
    rtegpu_write_u64(out + 8, s.offset);
    rtegpu_write_u64(out + 16, s.size);
    rtegpu_write_u64(out + 24, s.count);
    rtegpu_write_u32(out + 32, s.record_size);
}

static inline RteGpuSection rtegpu_parse_section_bytes(const unsigned char in[RTEGPU_SECTION_RECORD_SIZE]) {
    RteGpuSection s;
    s.type = rtegpu_read_u32(in + 0);
    s.flags = rtegpu_read_u32(in + 4);
    s.offset = rtegpu_read_u64(in + 8);
    s.size = rtegpu_read_u64(in + 16);
    s.count = rtegpu_read_u64(in + 24);
    s.record_size = rtegpu_read_u32(in + 32);
    return s;
}

static inline const RteGpuSection *rtegpu_find_section(const RteGpuPack &pack, uint32_t type) {
    for (const auto &section : pack.sections) if (section.type == type) return &section;
    return nullptr;
}

static inline void rtegpu_read_vector(std::ifstream &file, const RteGpuSection &section, std::vector<uint32_t> &out) {
    if (section.record_size != 4) throw std::runtime_error("unexpected u32 section record size");
    out.resize((size_t)section.count);
    if (!out.empty()) rtegpu_read_at(file, section.offset, out.data(), out.size() * sizeof(uint32_t));
}

static inline void rtegpu_read_vector_i32(std::ifstream &file, const RteGpuSection &section, std::vector<int32_t> &out) {
    if (section.record_size != 4) throw std::runtime_error("unexpected i32 section record size");
    out.resize((size_t)section.count);
    if (!out.empty()) rtegpu_read_at(file, section.offset, out.data(), out.size() * sizeof(int32_t));
}

static inline void rtegpu_read_vector_u8(std::ifstream &file, const RteGpuSection &section, std::vector<uint8_t> &out) {
    if (section.record_size != 1) throw std::runtime_error("unexpected u8 section record size");
    out.resize((size_t)section.count);
    if (!out.empty()) rtegpu_read_at(file, section.offset, out.data(), out.size());
}

static inline RteGpuPack rtegpu_load_pack(const char *path, bool hot_only) {
    RteGpuPack pack;
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("could not open RTEGPU file");
    unsigned char header_bytes[RTEGPU_HEADER_SIZE];
    rtegpu_read_at(file, 0, header_bytes, sizeof(header_bytes));
    pack.header = rtegpu_parse_header_bytes(header_bytes);
    pack.sections.resize(pack.header.section_count);
    for (uint32_t i = 0; i < pack.header.section_count; ++i) {
        unsigned char section_bytes[RTEGPU_SECTION_RECORD_SIZE];
        rtegpu_read_at(file, pack.header.section_directory_offset + (uint64_t)i * RTEGPU_SECTION_RECORD_SIZE, section_bytes, sizeof(section_bytes));
        pack.sections[i] = rtegpu_parse_section_bytes(section_bytes);
    }
    auto require = [&](uint32_t type) -> const RteGpuSection & {
        const RteGpuSection *section = rtegpu_find_section(pack, type);
        if (!section) throw std::runtime_error("RTEGPU file missing required section");
        return *section;
    };
    rtegpu_read_vector_i32(file, require(RTEGPU_SEC_STOP_LAT), pack.stop_lat);
    rtegpu_read_vector_i32(file, require(RTEGPU_SEC_STOP_LON), pack.stop_lon);
    rtegpu_read_vector(file, require(RTEGPU_SEC_STOP_MODE), pack.stop_mode);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRIP_ROUTE), pack.trip_route);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRIP_SERVICE), pack.trip_service);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRIP_MODE), pack.trip_mode);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRIP_EVENT_OFFSET), pack.trip_event_offset);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRIP_EVENT_COUNT), pack.trip_event_count);
    rtegpu_read_vector(file, require(RTEGPU_SEC_EVENT_STOP), pack.event_stop);
    rtegpu_read_vector(file, require(RTEGPU_SEC_EVENT_ARRIVAL), pack.event_arrival);
    rtegpu_read_vector(file, require(RTEGPU_SEC_EVENT_DEPARTURE), pack.event_departure);
    rtegpu_read_vector_u8(file, require(RTEGPU_SEC_SERVICE_ACTIVE), pack.service_active);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRANSFER_OFFSET), pack.transfer_offset);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRANSFER_TO), pack.transfer_to);
    rtegpu_read_vector(file, require(RTEGPU_SEC_TRANSFER_WALK_SEC), pack.transfer_walk_sec);
    if (!hot_only) {
        rtegpu_read_vector(file, require(RTEGPU_SEC_STOP_NAME_OFFSET), pack.stop_name_offset);
        rtegpu_read_vector(file, require(RTEGPU_SEC_STOP_NAME_SIZE), pack.stop_name_size);
        rtegpu_read_vector(file, require(RTEGPU_SEC_ROUTE_SHORT_OFFSET), pack.route_short_offset);
        rtegpu_read_vector(file, require(RTEGPU_SEC_ROUTE_SHORT_SIZE), pack.route_short_size);
        rtegpu_read_vector(file, require(RTEGPU_SEC_ROUTE_LONG_OFFSET), pack.route_long_offset);
        rtegpu_read_vector(file, require(RTEGPU_SEC_ROUTE_LONG_SIZE), pack.route_long_size);
        rtegpu_read_vector(file, require(RTEGPU_SEC_ROUTE_MODE), pack.route_mode);
        rtegpu_read_vector(file, require(RTEGPU_SEC_ROUTE_TYPE), pack.route_type);
        const RteGpuSection &strings = require(RTEGPU_SEC_STRING_TABLE);
        pack.strings.resize((size_t)strings.size);
        if (!pack.strings.empty()) rtegpu_read_at(file, strings.offset, pack.strings.data(), pack.strings.size());
    }
    return pack;
}

#endif
