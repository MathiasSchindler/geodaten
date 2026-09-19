#include "platform.h"
#include "runtime.h"

#define OSMRTE_HEADER_SIZE 256U
#define OSMRTE_SECTION_RECORD_SIZE 64U
#define OSMRTE_TILE_RECORD_SIZE 128U
#define OSMRTE_SECTION_ADDRESS_DICTIONARIES 0x0400U
#define OSMRTE_SECTION_TRANSIT_STOPS 0x0500U
#define OSMRTE_SECTION_TRANSIT_STOP_TO_PATTERN 0x0505U
#define OSMRTE_ADDRESS_SECTION_HEADER_SIZE 64U
#define OSMRTE_ADDRESS_RECORD_SIZE 80U
#define OSMRTE_ADDRESS_SCAN_BATCH_RECORDS 4096U

typedef struct {
    unsigned int type;
    unsigned int flags;
    unsigned long long offset;
    unsigned long long size;
    unsigned long long uncompressed_size;
    unsigned long long record_count;
    unsigned int record_size;
} OsmrteSectionRecord;

typedef struct {
    unsigned long long tile_id;
    unsigned int level;
    int x;
    int y;
    unsigned int flags;
    unsigned int local_node_count;
    unsigned int local_directed_edge_count;
    unsigned int portal_count;
    unsigned int stop_count;
    unsigned int address_count;
    unsigned int snap_cell_count;
    int min_lon_e7;
    int min_lat_e7;
    int max_lon_e7;
    int max_lat_e7;
    unsigned long long payload_offset;
    unsigned long long payload_size;
    unsigned long long payload_directory_offset;
    unsigned int payload_directory_count;
    unsigned int payload_directory_record_size;
    unsigned long long neighbor_mask;
} OsmrteTileRecord;

typedef struct {
    unsigned int entity_type;
    unsigned int flags;
    long long id;
    int lat_e7;
    int lon_e7;
    unsigned long long tile_id;
    unsigned int state_offset;
    unsigned int state_size;
    unsigned int city_offset;
    unsigned int city_size;
    unsigned int suburb_offset;
    unsigned int suburb_size;
    unsigned int street_offset;
    unsigned int street_size;
    unsigned int housenumber_offset;
    unsigned int housenumber_size;
    unsigned int postcode_offset;
    unsigned int postcode_size;
} OsmrteAddressRecord;

typedef struct {
    unsigned int version;
    unsigned int header_size;
    unsigned int endian_marker;
    unsigned long long file_size;
    unsigned long long build_unix_time;
    unsigned long long source_nodes;
    unsigned long long source_ways;
    unsigned long long source_relations;
    unsigned long long section_directory_offset;
    unsigned int section_count;
    unsigned int section_record_size;
    unsigned long long tile_directory_offset;
    unsigned long long tile_directory_size;
    unsigned int tile_record_size;
    unsigned int tile_count;
    unsigned int tile_scheme;
    unsigned int tile_level_count;
    unsigned int coord_scale;
    unsigned int routing_profile_mask;
    unsigned int projection_kind;
    unsigned int tile_size_m;
    int projection_origin_lat_e7;
    int projection_origin_lon_e7;
    unsigned int tile_lookup_kind;
    int min_lon_e7;
    int min_lat_e7;
    int max_lon_e7;
    int max_lat_e7;
    unsigned long long string_table_offset;
    unsigned long long string_table_size;
} OsmrteHeader;

typedef struct {
    int have_tile_query;
    int tile_lat_e7;
    int tile_lon_e7;
    int show_sections;
    const char *address_query;
} OsmrteInfoOptions;

typedef struct {
    int present;
    unsigned long long offset;
    unsigned long long size;
    unsigned long long record_count;
    unsigned int record_size;
} OsmrteAddressSection;

static int address_field_valid(unsigned int offset, unsigned int size, unsigned int strings_size);

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.rte [--sections] [--address TEXT] [--tile LAT,LON]\n");
    rt_write_cstr(2, "  --address TEXT   query address as STREET HOUSE[, PLACE]\n");
    rt_write_cstr(2, "  --tile LAT,LON   inspect the tile covering a coordinate\n");
}

static unsigned int read_u32_le(const unsigned char *in) {
    return (unsigned int)in[0] |
           ((unsigned int)in[1] << 8U) |
           ((unsigned int)in[2] << 16U) |
           ((unsigned int)in[3] << 24U);
}

static int read_i32_le(const unsigned char *in) {
    return (int)read_u32_le(in);
}

static unsigned long long read_u64_le(const unsigned char *in) {
    unsigned long long value = 0ULL;
    unsigned int index;

    for (index = 0U; index < 8U; ++index) value |= ((unsigned long long)in[index]) << (index * 8U);
    return value;
}

static long long read_i64_le(const unsigned char *in) {
    return (long long)read_u64_le(in);
}

static int read_exact(int fd, void *buffer, size_t count) {
    unsigned char *cursor = (unsigned char *)buffer;
    size_t offset = 0U;

    while (offset < count) {
        long bytes = platform_read(fd, cursor + offset, count - offset);
        if (bytes <= 0) return -1;
        offset += (size_t)bytes;
    }
    return 0;
}

static int read_at(int fd, unsigned long long offset, void *buffer, size_t count) {
    if (platform_seek(fd, (long long)offset, PLATFORM_SEEK_SET) < 0) return -1;
    return read_exact(fd, buffer, count);
}

static void write_field_u32(const char *name, unsigned int value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    rt_write_uint(1, value);
    rt_write_char(1, '\n');
}

static void write_field_i32(const char *name, int value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    rt_write_int(1, value);
    rt_write_char(1, '\n');
}

static void write_field_u64(const char *name, unsigned long long value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    rt_write_uint(1, value);
    rt_write_char(1, '\n');
}

static void write_field_bool(const char *name, int value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    rt_write_cstr(1, value ? "yes" : "no");
    rt_write_char(1, '\n');
}

static void write_hex_u32(unsigned int value) {
    static const char hex[] = "0123456789abcdef";
    int shift;

    rt_write_cstr(1, "0x");
    for (shift = 28; shift >= 0; shift -= 4) rt_write_char(1, hex[(value >> (unsigned int)shift) & 0x0fU]);
}

static void write_hex_u64(unsigned long long value) {
    static const char hex[] = "0123456789abcdef";
    int shift;

    rt_write_cstr(1, "0x");
    for (shift = 60; shift >= 0; shift -= 4) rt_write_char(1, hex[(value >> (unsigned int)shift) & 0x0fU]);
}

static void write_coord_e7_value(int value) {
    unsigned int fraction;
    unsigned int divisor;
    unsigned int absolute;

    if (value < 0) {
        rt_write_char(1, '-');
        absolute = (unsigned int)(-value);
    } else {
        absolute = (unsigned int)value;
    }
    rt_write_uint(1, absolute / 10000000U);
    rt_write_char(1, '.');
    fraction = absolute % 10000000U;
    for (divisor = 1000000U; divisor != 0U; divisor /= 10U) {
        rt_write_char(1, (char)('0' + (fraction / divisor) % 10U));
    }
}

static void write_field_coord(const char *name, int value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    write_coord_e7_value(value);
    rt_write_char(1, '\n');
}

static int parse_coord_part(const char *text, size_t size, int *value_out) {
    size_t index = 0U;
    unsigned long long whole = 0ULL;
    unsigned long long fraction = 0ULL;
    unsigned int fraction_digits = 0U;
    int negative = 0;
    long long value;

    if (size == 0U) return -1;
    if (text[index] == '-') {
        negative = 1;
        index += 1U;
    }
    if (index >= size || text[index] < '0' || text[index] > '9') return -1;
    while (index < size && text[index] >= '0' && text[index] <= '9') {
        whole = whole * 10ULL + (unsigned long long)(text[index] - '0');
        if (whole > 180ULL) return -1;
        index += 1U;
    }
    if (index < size && text[index] == '.') {
        index += 1U;
        while (index < size && text[index] >= '0' && text[index] <= '9') {
            if (fraction_digits >= 7U) return -1;
            fraction = fraction * 10ULL + (unsigned long long)(text[index] - '0');
            fraction_digits += 1U;
            index += 1U;
        }
    }
    if (index != size) return -1;
    while (fraction_digits < 7U) {
        fraction *= 10ULL;
        fraction_digits += 1U;
    }
    value = (long long)(whole * 10000000ULL + fraction);
    if (negative) value = -value;
    if (value < -2147483648LL || value > 2147483647LL) return -1;
    *value_out = (int)value;
    return 0;
}

static int parse_lat_lon_arg(const char *text, int *lat_e7_out, int *lon_e7_out) {
    size_t index = 0U;
    size_t comma = 0U;

    while (text[index] != '\0') {
        if (text[index] == ',') {
            if (comma != 0U) return -1;
            comma = index;
        }
        index += 1U;
    }
    if (comma == 0U || comma + 1U >= index) return -1;
    if (parse_coord_part(text, comma, lat_e7_out) != 0) return -1;
    if (parse_coord_part(text + comma + 1U, index - comma - 1U, lon_e7_out) != 0) return -1;
    if (*lat_e7_out < -900000000 || *lat_e7_out > 900000000) return -1;
    if (*lon_e7_out < -1800000000 || *lon_e7_out > 1800000000) return -1;
    return 0;
}

static int floor_div_ll(long long value, long long divisor) {
    long long quotient = value / divisor;
    long long remainder = value % divisor;

    if (remainder != 0LL && ((remainder < 0LL) != (divisor < 0LL))) quotient -= 1LL;
    return (int)quotient;
}

static unsigned int abs_i32(int value) {
    return value < 0 ? (unsigned int)(-value) : (unsigned int)value;
}

static unsigned int cos_degrees_q1000000(unsigned int degrees) {
    static const unsigned int table[91] = {
        1000000U, 999848U, 999391U, 998630U, 997564U, 996195U, 994522U, 992546U, 990268U, 987688U,
        984808U, 981627U, 978148U, 974370U, 970296U, 965926U, 961262U, 956305U, 951057U, 945519U,
        939693U, 933580U, 927184U, 920505U, 913545U, 906308U, 898794U, 891007U, 882948U, 874620U,
        866025U, 857167U, 848048U, 838671U, 829038U, 819152U, 809017U, 798636U, 788011U, 777146U,
        766044U, 754710U, 743145U, 731354U, 719340U, 707107U, 694658U, 681998U, 669131U, 656059U,
        642788U, 629320U, 615661U, 601815U, 587785U, 573576U, 559193U, 544639U, 529919U, 515038U,
        500000U, 484810U, 469472U, 453990U, 438371U, 422618U, 406737U, 390731U, 374607U, 358368U,
        342020U, 325568U, 309017U, 292372U, 275637U, 258819U, 241922U, 224951U, 207912U, 190809U,
        173648U, 156434U, 139173U, 121869U, 104528U, 87156U, 69756U, 52336U, 34899U, 17452U, 0U
    };
    unsigned int whole;
    unsigned int fraction;
    unsigned int left;
    unsigned int right;

    if (degrees >= 900000000U) return 0U;
    whole = degrees / 10000000U;
    fraction = degrees % 10000000U;
    if (whole >= 90U) return 0U;
    left = table[whole];
    right = table[whole + 1U];
    return left - (unsigned int)(((unsigned long long)(left - right) * fraction) / 10000000ULL);
}

static unsigned int meters_per_degree_lon_from_lat_e7(int lat_e7) {
    unsigned int cosine = cos_degrees_q1000000(abs_i32(lat_e7));

    return (unsigned int)((111320ULL * (unsigned long long)cosine + 500000ULL) / 1000000ULL);
}

static unsigned long long route_tile_id(int x, int y) {
    long long x_code = (long long)x + 0x10000000LL;
    long long y_code = (long long)y + 0x10000000LL;

    if (x_code < 0LL) x_code = 0LL;
    if (y_code < 0LL) y_code = 0LL;
    return (((unsigned long long)x_code) << 29U) | (unsigned long long)y_code;
}

static int parse_header(const unsigned char bytes[OSMRTE_HEADER_SIZE], OsmrteHeader *header) {
    if (memcmp(bytes, "OSMRTE01", 8U) != 0) return -1;
    rt_memset(header, 0, sizeof(*header));
    header->version = read_u32_le(bytes + 8U);
    header->header_size = read_u32_le(bytes + 12U);
    header->endian_marker = read_u32_le(bytes + 16U);
    header->file_size = read_u64_le(bytes + 24U);
    header->build_unix_time = read_u64_le(bytes + 32U);
    header->source_nodes = read_u64_le(bytes + 40U);
    header->source_ways = read_u64_le(bytes + 48U);
    header->source_relations = read_u64_le(bytes + 56U);
    header->section_directory_offset = read_u64_le(bytes + 64U);
    header->section_count = read_u32_le(bytes + 72U);
    header->section_record_size = read_u32_le(bytes + 76U);
    header->tile_directory_offset = read_u64_le(bytes + 80U);
    header->tile_directory_size = read_u64_le(bytes + 88U);
    header->tile_record_size = read_u32_le(bytes + 96U);
    header->tile_count = read_u32_le(bytes + 100U);
    header->tile_scheme = read_u32_le(bytes + 104U);
    header->tile_level_count = read_u32_le(bytes + 108U);
    header->coord_scale = read_u32_le(bytes + 112U);
    header->routing_profile_mask = read_u32_le(bytes + 116U);
    header->projection_kind = read_u32_le(bytes + 120U);
    header->tile_size_m = read_u32_le(bytes + 124U);
    header->projection_origin_lat_e7 = read_i32_le(bytes + 128U);
    header->projection_origin_lon_e7 = read_i32_le(bytes + 132U);
    header->tile_lookup_kind = read_u32_le(bytes + 136U);
    header->min_lon_e7 = read_i32_le(bytes + 144U);
    header->min_lat_e7 = read_i32_le(bytes + 148U);
    header->max_lon_e7 = read_i32_le(bytes + 152U);
    header->max_lat_e7 = read_i32_le(bytes + 156U);
    header->string_table_offset = read_u64_le(bytes + 160U);
    header->string_table_size = read_u64_le(bytes + 168U);
    return 0;
}

static void parse_section_record(const unsigned char bytes[OSMRTE_SECTION_RECORD_SIZE], OsmrteSectionRecord *record) {
    record->type = read_u32_le(bytes + 0U);
    record->flags = read_u32_le(bytes + 4U);
    record->offset = read_u64_le(bytes + 8U);
    record->size = read_u64_le(bytes + 16U);
    record->uncompressed_size = read_u64_le(bytes + 24U);
    record->record_count = read_u64_le(bytes + 32U);
    record->record_size = read_u32_le(bytes + 40U);
}

static void parse_tile_record(const unsigned char bytes[OSMRTE_TILE_RECORD_SIZE], OsmrteTileRecord *record) {
    record->tile_id = read_u64_le(bytes + 0U);
    record->level = read_u32_le(bytes + 8U);
    record->x = read_i32_le(bytes + 12U);
    record->y = read_i32_le(bytes + 16U);
    record->flags = read_u32_le(bytes + 20U);
    record->local_node_count = read_u32_le(bytes + 24U);
    record->local_directed_edge_count = read_u32_le(bytes + 28U);
    record->portal_count = read_u32_le(bytes + 32U);
    record->stop_count = read_u32_le(bytes + 36U);
    record->address_count = read_u32_le(bytes + 40U);
    record->snap_cell_count = read_u32_le(bytes + 44U);
    record->min_lon_e7 = read_i32_le(bytes + 48U);
    record->min_lat_e7 = read_i32_le(bytes + 52U);
    record->max_lon_e7 = read_i32_le(bytes + 56U);
    record->max_lat_e7 = read_i32_le(bytes + 60U);
    record->payload_offset = read_u64_le(bytes + 64U);
    record->payload_size = read_u64_le(bytes + 72U);
    record->payload_directory_offset = read_u64_le(bytes + 80U);
    record->payload_directory_count = read_u32_le(bytes + 88U);
    record->payload_directory_record_size = read_u32_le(bytes + 92U);
    record->neighbor_mask = read_u64_le(bytes + 96U);
}

static void parse_address_record(const unsigned char bytes[OSMRTE_ADDRESS_RECORD_SIZE], OsmrteAddressRecord *record) {
    record->entity_type = read_u32_le(bytes + 0U);
    record->flags = read_u32_le(bytes + 4U);
    record->id = read_i64_le(bytes + 8U);
    record->lat_e7 = read_i32_le(bytes + 16U);
    record->lon_e7 = read_i32_le(bytes + 20U);
    record->tile_id = read_u64_le(bytes + 24U);
    record->state_offset = read_u32_le(bytes + 32U);
    record->state_size = read_u32_le(bytes + 36U);
    record->city_offset = read_u32_le(bytes + 40U);
    record->city_size = read_u32_le(bytes + 44U);
    record->suburb_offset = read_u32_le(bytes + 48U);
    record->suburb_size = read_u32_le(bytes + 52U);
    record->street_offset = read_u32_le(bytes + 56U);
    record->street_size = read_u32_le(bytes + 60U);
    record->housenumber_offset = read_u32_le(bytes + 64U);
    record->housenumber_size = read_u32_le(bytes + 68U);
    record->postcode_offset = read_u32_le(bytes + 72U);
    record->postcode_size = read_u32_le(bytes + 76U);
}

static size_t normalize_text_copy(const char *data, size_t size, char *out, size_t out_capacity) {
    size_t index = 0U;
    size_t used = 0U;

    while (index < size && used + 1U < out_capacity) {
        unsigned char ch = (unsigned char)data[index];
        if (ch >= 'A' && ch <= 'Z') {
            out[used++] = (char)(ch + ('a' - 'A'));
            index += 1U;
        } else if (ch == 0xc3U && index + 1U < size && (unsigned char)data[index + 1U] == 0x9fU) {
            if (used + 2U >= out_capacity) break;
            out[used++] = 's';
            out[used++] = 's';
            index += 2U;
        } else {
            out[used++] = (char)ch;
            index += 1U;
        }
    }
    out[used] = '\0';
    return used;
}

static int normalized_is_alnum(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

static int normalized_char_is_trim(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',';
}

static void normalized_trim_span(const char *text, size_t size, size_t *start_out, size_t *end_out) {
    size_t start = 0U;
    size_t end = size;

    while (start < end && normalized_char_is_trim(text[start])) start += 1U;
    while (end > start && normalized_char_is_trim(text[end - 1U])) end -= 1U;
    *start_out = start;
    *end_out = end;
}

static int normalized_span_equals(const char *left, size_t left_size, const char *right, size_t right_size) {
    if (left_size != right_size) return 0;
    if (left_size == 0U) return 0;
    return memcmp(left, right, left_size) == 0;
}

static int normalized_field_copy(const char *strings, unsigned int strings_size, unsigned int offset, unsigned int size, char *out, size_t out_capacity, size_t *out_size) {
    if (!address_field_valid(offset, size, strings_size)) return 0;
    *out_size = normalize_text_copy(strings + offset, size, out, out_capacity);
    return 1;
}

static int normalized_place_matches(const char *strings, unsigned int strings_size, unsigned int offset, unsigned int size, const char *place_norm, size_t place_norm_size) {
    char field_norm[512];
    size_t field_norm_size;

    if (place_norm_size == 0U) return 1;
    if (!normalized_field_copy(strings, strings_size, offset, size, field_norm, sizeof(field_norm), &field_norm_size)) return 0;
    return normalized_span_equals(field_norm, field_norm_size, place_norm, place_norm_size);
}

static int normalized_address_place_matches(const char *strings, unsigned int strings_size, const OsmrteAddressRecord *record, const char *place_norm, size_t place_norm_size) {
    if (place_norm_size == 0U) return 1;
    if (normalized_place_matches(strings, strings_size, record->city_offset, record->city_size, place_norm, place_norm_size)) return 1;
    if (normalized_place_matches(strings, strings_size, record->suburb_offset, record->suburb_size, place_norm, place_norm_size)) return 1;
    if (normalized_place_matches(strings, strings_size, record->postcode_offset, record->postcode_size, place_norm, place_norm_size)) return 1;
    return 0;
}

static int normalized_street_house_matches(const char *strings, unsigned int strings_size, const OsmrteAddressRecord *record, const char *main_norm, size_t main_norm_size) {
    char street_norm[512];
    char house_norm[128];
    size_t street_norm_size;
    size_t house_norm_size;
    size_t start;

    if (!normalized_field_copy(strings, strings_size, record->street_offset, record->street_size, street_norm, sizeof(street_norm), &street_norm_size)) return 0;
    if (!normalized_field_copy(strings, strings_size, record->housenumber_offset, record->housenumber_size, house_norm, sizeof(house_norm), &house_norm_size)) return 0;
    if (street_norm_size == 0U || house_norm_size == 0U || main_norm_size < house_norm_size) return 0;

    for (start = 0U; start + house_norm_size <= main_norm_size; ++start) {
        int left_ok = start == 0U || !normalized_is_alnum(main_norm[start - 1U]);
        int right_ok = start + house_norm_size == main_norm_size || !normalized_is_alnum(main_norm[start + house_norm_size]);
        size_t street_start;
        size_t street_end;

        if (!left_ok || !right_ok || memcmp(main_norm + start, house_norm, house_norm_size) != 0) continue;
        normalized_trim_span(main_norm, start, &street_start, &street_end);
        if (street_end <= street_start) continue;
        if (normalized_span_equals(main_norm + street_start, street_end - street_start, street_norm, street_norm_size)) return 1;
    }
    return 0;
}

static void split_normalized_address_query(const char *query_norm, size_t query_norm_size, const char **main_out, size_t *main_size_out, const char **place_out, size_t *place_size_out) {
    size_t comma = query_norm_size;
    size_t start;
    size_t end;
    size_t index;

    for (index = 0U; index < query_norm_size; ++index) {
        if (query_norm[index] == ',') {
            comma = index;
            break;
        }
    }
    normalized_trim_span(query_norm, comma, &start, &end);
    *main_out = query_norm + start;
    *main_size_out = end - start;
    if (comma < query_norm_size) {
        normalized_trim_span(query_norm + comma + 1U, query_norm_size - comma - 1U, &start, &end);
        *place_out = query_norm + comma + 1U + start;
        *place_size_out = end - start;
    } else {
        *place_out = query_norm + query_norm_size;
        *place_size_out = 0U;
    }
}

static int read_tile_at(int fd, const OsmrteHeader *header, unsigned int index, OsmrteTileRecord *record) {
    unsigned char bytes[OSMRTE_TILE_RECORD_SIZE];
    unsigned long long offset = header->tile_directory_offset + (unsigned long long)index * header->tile_record_size;

    if (header->tile_record_size != OSMRTE_TILE_RECORD_SIZE) return -1;
    if (read_at(fd, offset, bytes, sizeof(bytes)) != 0) return -1;
    parse_tile_record(bytes, record);
    return 0;
}

static int find_tile_by_id(int fd, const OsmrteHeader *header, unsigned long long tile_id, OsmrteTileRecord *record, unsigned int *index_out) {
    unsigned int low = 0U;
    unsigned int high = header->tile_count;

    while (low < high) {
        unsigned int mid = low + (high - low) / 2U;
        OsmrteTileRecord candidate;
        if (read_tile_at(fd, header, mid, &candidate) != 0) return -1;
        if (candidate.tile_id == tile_id) {
            *record = candidate;
            if (index_out != 0) *index_out = mid;
            return 1;
        }
        if (candidate.tile_id < tile_id) low = mid + 1U;
        else high = mid;
    }
    return 0;
}

static void lat_lon_to_tile(const OsmrteHeader *header, int lat_e7, int lon_e7, int *tile_x_out, int *tile_y_out, unsigned long long *tile_id_out) {
    unsigned int meters_per_degree_lon = meters_per_degree_lon_from_lat_e7(header->projection_origin_lat_e7);
    long long metric_x_m = ((long long)(lon_e7 - header->projection_origin_lon_e7) * (long long)meters_per_degree_lon) / 10000000LL;
    long long metric_y_m = ((long long)(lat_e7 - header->projection_origin_lat_e7) * 111320LL) / 10000000LL;
    int tile_x = floor_div_ll(metric_x_m, (long long)header->tile_size_m);
    int tile_y = floor_div_ll(metric_y_m, (long long)header->tile_size_m);

    *tile_x_out = tile_x;
    *tile_y_out = tile_y;
    *tile_id_out = route_tile_id(tile_x, tile_y);
}

static int read_sections(int fd, const OsmrteHeader *header, int show_sections, OsmrteAddressSection *address_section, int *addresses_present, int *gtfs_present) {
    unsigned char bytes[OSMRTE_SECTION_RECORD_SIZE];
    unsigned int index;

    *addresses_present = 0;
    *gtfs_present = 0;
    rt_memset(address_section, 0, sizeof(*address_section));
    if (header->section_record_size != OSMRTE_SECTION_RECORD_SIZE) return -1;
    for (index = 0U; index < header->section_count; ++index) {
        OsmrteSectionRecord section;
        if (read_at(fd, header->section_directory_offset + (unsigned long long)index * header->section_record_size, bytes, sizeof(bytes)) != 0) return -1;
        parse_section_record(bytes, &section);
        if (section.type == OSMRTE_SECTION_ADDRESS_DICTIONARIES) {
            *addresses_present = 1;
            address_section->present = 1;
            address_section->offset = section.offset;
            address_section->size = section.size;
            address_section->record_count = section.record_count;
            address_section->record_size = section.record_size;
        }
        if (section.type >= OSMRTE_SECTION_TRANSIT_STOPS && section.type <= OSMRTE_SECTION_TRANSIT_STOP_TO_PATTERN) *gtfs_present = 1;
        if (show_sections) {
            rt_write_cstr(1, "section["); rt_write_uint(1, index); rt_write_cstr(1, "].type: "); write_hex_u32(section.type); rt_write_char(1, '\n');
            rt_write_cstr(1, "section["); rt_write_uint(1, index); rt_write_cstr(1, "].offset: "); rt_write_uint(1, section.offset); rt_write_char(1, '\n');
            rt_write_cstr(1, "section["); rt_write_uint(1, index); rt_write_cstr(1, "].size: "); rt_write_uint(1, section.size); rt_write_char(1, '\n');
            rt_write_cstr(1, "section["); rt_write_uint(1, index); rt_write_cstr(1, "].record_count: "); rt_write_uint(1, section.record_count); rt_write_char(1, '\n');
            rt_write_cstr(1, "section["); rt_write_uint(1, index); rt_write_cstr(1, "].record_size: "); rt_write_uint(1, section.record_size); rt_write_char(1, '\n');
        }
    }
    return 0;
}

static void write_header_info(const OsmrteHeader *header, int addresses_present, int gtfs_present) {
    rt_write_cstr(1, "magic: OSMRTE01\n");
    write_field_u32("version", header->version);
    write_field_u32("header_size", header->header_size);
    write_field_u64("file_size", header->file_size);
    write_field_u64("source_nodes", header->source_nodes);
    write_field_u64("source_ways", header->source_ways);
    write_field_u64("source_relations", header->source_relations);
    write_field_u32("section_count", header->section_count);
    write_field_u32("tile_count", header->tile_count);
    write_field_u32("tile_size_m", header->tile_size_m);
    write_field_u32("tile_scheme", header->tile_scheme);
    write_field_u32("projection_kind", header->projection_kind);
    write_field_coord("projection_origin_lat", header->projection_origin_lat_e7);
    write_field_coord("projection_origin_lon", header->projection_origin_lon_e7);
    write_field_coord("min_lat", header->min_lat_e7);
    write_field_coord("min_lon", header->min_lon_e7);
    write_field_coord("max_lat", header->max_lat_e7);
    write_field_coord("max_lon", header->max_lon_e7);
    write_field_u64("tile_directory_offset", header->tile_directory_offset);
    write_field_u64("tile_directory_size", header->tile_directory_size);
    write_field_u64("string_table_offset", header->string_table_offset);
    write_field_u64("string_table_size", header->string_table_size);
    write_field_bool("addresses_present", addresses_present);
    write_field_bool("gtfs_present", gtfs_present);
}

static void write_tile_info(const OsmrteTileRecord *tile, unsigned int index) {
    rt_write_cstr(1, "tile_found: yes\n");
    write_field_u32("tile_index", index);
    rt_write_cstr(1, "tile_id: "); write_hex_u64(tile->tile_id); rt_write_char(1, '\n');
    write_field_i32("tile_x", tile->x);
    write_field_i32("tile_y", tile->y);
    write_field_coord("tile_min_lat", tile->min_lat_e7);
    write_field_coord("tile_min_lon", tile->min_lon_e7);
    write_field_coord("tile_max_lat", tile->max_lat_e7);
    write_field_coord("tile_max_lon", tile->max_lon_e7);
    write_field_u32("tile_local_nodes", tile->local_node_count);
    write_field_u32("tile_directed_edges", tile->local_directed_edge_count);
    write_field_u32("tile_addresses", tile->address_count);
    write_field_u32("tile_stops", tile->stop_count);
    write_field_u32("tile_snap_cells", tile->snap_cell_count);
    write_field_u64("tile_payload_offset", tile->payload_offset);
    write_field_u64("tile_payload_size", tile->payload_size);
    rt_write_cstr(1, "tile_neighbor_mask: "); write_hex_u64(tile->neighbor_mask); rt_write_char(1, '\n');
}

static int write_text_n(const char *text, size_t size) {
    return rt_write_all(1, text, size);
}

static const char *entity_type_name(unsigned int entity_type) {
    if (entity_type == 1U) return "node";
    if (entity_type == 2U) return "way";
    if (entity_type == 3U) return "relation";
    return "unknown";
}

static int address_field_valid(unsigned int offset, unsigned int size, unsigned int strings_size) {
    if (offset > strings_size) return 0;
    if (size > strings_size - offset) return 0;
    return 1;
}

static void write_address_field(const char *prefix, const char *strings, unsigned int strings_size, unsigned int offset, unsigned int size) {
    rt_write_cstr(1, prefix);
    if (address_field_valid(offset, size, strings_size)) (void)write_text_n(strings + offset, size);
    rt_write_char(1, '\n');
}

static void write_address_match(const OsmrteAddressRecord *record, const char *strings, unsigned int strings_size, unsigned int match_index) {
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); rt_write_cstr(1, "].type: "); rt_write_cstr(1, entity_type_name(record->entity_type)); rt_write_char(1, '\n');
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); rt_write_cstr(1, "].id: "); rt_write_int(1, record->id); rt_write_char(1, '\n');
    if ((record->flags & 1U) != 0U) {
        rt_write_cstr(1, "address["); rt_write_uint(1, match_index); rt_write_cstr(1, "].lat: "); write_coord_e7_value(record->lat_e7); rt_write_char(1, '\n');
        rt_write_cstr(1, "address["); rt_write_uint(1, match_index); rt_write_cstr(1, "].lon: "); write_coord_e7_value(record->lon_e7); rt_write_char(1, '\n');
        rt_write_cstr(1, "address["); rt_write_uint(1, match_index); rt_write_cstr(1, "].tile_id: "); write_hex_u64(record->tile_id); rt_write_char(1, '\n');
    }
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); write_address_field("].city: ", strings, strings_size, record->city_offset, record->city_size);
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); write_address_field("].suburb: ", strings, strings_size, record->suburb_offset, record->suburb_size);
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); write_address_field("].street: ", strings, strings_size, record->street_offset, record->street_size);
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); write_address_field("].housenumber: ", strings, strings_size, record->housenumber_offset, record->housenumber_size);
    rt_write_cstr(1, "address["); rt_write_uint(1, match_index); write_address_field("].postcode: ", strings, strings_size, record->postcode_offset, record->postcode_size);
}

static int query_address_section(int fd, const OsmrteAddressSection *section, const char *query) {
    unsigned char header[OSMRTE_ADDRESS_SECTION_HEADER_SIZE];
    unsigned long long header_record_count;
    unsigned int record_size;
    unsigned int strings_size;
    unsigned long long strings_offset;
    char *strings;
    char *query_norm;
    unsigned char *record_buffer;
    size_t query_size = rt_strlen(query);
    size_t query_norm_size;
    const char *query_main_norm;
    const char *query_place_norm;
    size_t query_main_norm_size;
    size_t query_place_norm_size;
    unsigned long long index;
    unsigned long long match_count = 0ULL;
    unsigned int shown_count = 0U;

    if (!section->present) return -1;
    if (section->record_size != OSMRTE_ADDRESS_RECORD_SIZE) return -1;
    if (read_at(fd, section->offset, header, sizeof(header)) != 0) return -1;
    if (memcmp(header, "ADDRIDX1", 8U) != 0) return -1;
    header_record_count = read_u64_le(header + 16U);
    record_size = read_u32_le(header + 24U);
    strings_size = read_u32_le(header + 28U);
    strings_offset = read_u64_le(header + 32U);
    if (record_size != OSMRTE_ADDRESS_RECORD_SIZE || header_record_count != section->record_count) return -1;
    if (strings_offset > section->size || (unsigned long long)strings_size > section->size - strings_offset) return -1;
    strings = (char *)rt_malloc((size_t)strings_size + 1U);
    query_norm = (char *)rt_malloc(query_size * 2U + 1U);
    record_buffer = (unsigned char *)rt_malloc((size_t)OSMRTE_ADDRESS_SCAN_BATCH_RECORDS * OSMRTE_ADDRESS_RECORD_SIZE);
    if (strings == 0 || query_norm == 0 || record_buffer == 0) {
        rt_free(strings);
        rt_free(query_norm);
        rt_free(record_buffer);
        return -1;
    }
    if (strings_size != 0U && read_at(fd, section->offset + strings_offset, strings, strings_size) != 0) {
        rt_free(strings);
        rt_free(query_norm);
        rt_free(record_buffer);
        return -1;
    }
    strings[strings_size] = '\0';
    query_norm_size = normalize_text_copy(query, query_size, query_norm, query_size * 2U + 1U);
    split_normalized_address_query(query_norm, query_norm_size, &query_main_norm, &query_main_norm_size, &query_place_norm, &query_place_norm_size);

    for (index = 0ULL; index < section->record_count;) {
        unsigned long long remaining = section->record_count - index;
        unsigned int batch_count = remaining > OSMRTE_ADDRESS_SCAN_BATCH_RECORDS ? OSMRTE_ADDRESS_SCAN_BATCH_RECORDS : (unsigned int)remaining;
        size_t batch_size = (size_t)batch_count * OSMRTE_ADDRESS_RECORD_SIZE;
        unsigned int batch_index;

        if (read_at(fd, section->offset + OSMRTE_ADDRESS_SECTION_HEADER_SIZE + index * OSMRTE_ADDRESS_RECORD_SIZE, record_buffer, batch_size) != 0) {
            rt_free(strings);
            rt_free(query_norm);
            rt_free(record_buffer);
            return -1;
        }
        for (batch_index = 0U; batch_index < batch_count; ++batch_index) {
            OsmrteAddressRecord record;
            parse_address_record(record_buffer + (size_t)batch_index * OSMRTE_ADDRESS_RECORD_SIZE, &record);
            if (normalized_address_place_matches(strings, strings_size, &record, query_place_norm, query_place_norm_size) &&
                normalized_street_house_matches(strings, strings_size, &record, query_main_norm, query_main_norm_size)) {
                if (match_count == 0ULL) rt_write_cstr(1, "address_lookup: found\n");
                if (shown_count < 20U) {
                    write_address_match(&record, strings, strings_size, shown_count);
                    shown_count += 1U;
                }
                match_count += 1ULL;
            }
        }
        index += batch_count;
    }
    if (match_count == 0ULL) rt_write_cstr(1, "address_lookup: not_found\n");
    else {
        rt_write_cstr(1, "address_match_count: "); rt_write_uint(1, match_count); rt_write_char(1, '\n');
        if (match_count > shown_count) { rt_write_cstr(1, "address_matches_shown: "); rt_write_uint(1, shown_count); rt_write_char(1, '\n'); }
    }
    rt_free(strings);
    rt_free(query_norm);
    rt_free(record_buffer);
    return 0;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "rte-info";
    const char *path;
    OsmrteInfoOptions options;
    OsmrteHeader header;
    OsmrteAddressSection address_section;
    unsigned char header_bytes[OSMRTE_HEADER_SIZE];
    int addresses_present;
    int gtfs_present;
    int fd;
    int argi = 2;

    if (argc >= 2 && (rt_strcmp(argv[1], "-h") == 0 || rt_strcmp(argv[1], "--help") == 0)) { write_usage(program); return 0; }
    if (argc < 2) { write_usage(program); return 1; }
    path = argv[1];
    rt_memset(&options, 0, sizeof(options));
    while (argi < argc) {
        if (rt_strcmp(argv[argi], "--sections") == 0) { options.show_sections = 1; argi += 1; }
        else if (rt_strcmp(argv[argi], "--tile") == 0) {
            argi += 1;
            if (argi >= argc || parse_lat_lon_arg(argv[argi], &options.tile_lat_e7, &options.tile_lon_e7) != 0) { write_usage(program); return 1; }
            options.have_tile_query = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--address") == 0) {
            argi += 1;
            if (argi >= argc) { write_usage(program); return 1; }
            options.address_query = argv[argi];
            argi += 1;
        } else { write_usage(program); return 1; }
    }

    fd = platform_open_read(path);
    if (fd < 0) { rt_write_cstr(2, "rte-info: could not open route pack\n"); return 1; }
    if (read_exact(fd, header_bytes, sizeof(header_bytes)) != 0 || parse_header(header_bytes, &header) != 0) {
        (void)platform_close(fd);
        rt_write_cstr(2, "rte-info: invalid or unsupported OSMRTE01 header\n");
        return 1;
    }
    if (header.header_size != OSMRTE_HEADER_SIZE || header.section_record_size != OSMRTE_SECTION_RECORD_SIZE || header.tile_record_size != OSMRTE_TILE_RECORD_SIZE) {
        (void)platform_close(fd);
        rt_write_cstr(2, "rte-info: unsupported OSMRTE01 record layout\n");
        return 1;
    }
    if (read_sections(fd, &header, options.show_sections, &address_section, &addresses_present, &gtfs_present) != 0) {
        (void)platform_close(fd);
        rt_write_cstr(2, "rte-info: could not read section directory\n");
        return 1;
    }

    write_header_info(&header, addresses_present, gtfs_present);
    if (options.have_tile_query) {
        int tile_x;
        int tile_y;
        unsigned long long tile_id;
        OsmrteTileRecord tile;
        unsigned int tile_index = 0U;
        int result;

        lat_lon_to_tile(&header, options.tile_lat_e7, options.tile_lon_e7, &tile_x, &tile_y, &tile_id);
        rt_write_cstr(1, "query_tile_x: "); rt_write_int(1, tile_x); rt_write_char(1, '\n');
        rt_write_cstr(1, "query_tile_y: "); rt_write_int(1, tile_y); rt_write_char(1, '\n');
        rt_write_cstr(1, "query_tile_id: "); write_hex_u64(tile_id); rt_write_char(1, '\n');
        result = find_tile_by_id(fd, &header, tile_id, &tile, &tile_index);
        if (result < 0) { (void)platform_close(fd); rt_write_cstr(2, "rte-info: could not read tile directory\n"); return 1; }
        if (result == 0) rt_write_cstr(1, "tile_found: no\n");
        else write_tile_info(&tile, tile_index);
    }
    if (options.address_query != 0) {
        rt_write_cstr(1, "address_query: "); rt_write_cstr(1, options.address_query); rt_write_char(1, '\n');
        if (!addresses_present) {
            rt_write_cstr(1, "address_lookup: unavailable\n");
            rt_write_cstr(1, "address_lookup_reason: no address section in this route pack\n");
        } else {
            if (query_address_section(fd, &address_section, options.address_query) != 0) {
                (void)platform_close(fd);
                rt_write_cstr(2, "rte-info: could not query address section\n");
                return 1;
            }
        }
    }
    (void)platform_close(fd);
    return 0;
}
