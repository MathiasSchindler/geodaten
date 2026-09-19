#include "platform.h"
#include "runtime.h"

#define RTE_HEADER_SIZE 256U
#define RTE_SECTION_RECORD_SIZE 64U
#define RTE_TILE_RECORD_SIZE 128U
#define RTE_ADDRESS_HEADER_SIZE 64U
#define RTE_ADDRESS_RECORD_SIZE 80U
#define RTE_ADDRESS_SCAN_BATCH_RECORDS 4096U
#define RTE_SECTION_ADDRESS_DICTIONARIES 0x0400U
#define RTE_SECTION_TRANSIT_STOPS 0x0500U
#define RTE_TRANSIT_SECTION_HEADER_SIZE 128U
#define RTE_TRANSIT_STOP_RECORD_SIZE 40U
#define RTE_TRANSIT_ROUTE_RECORD_SIZE 24U
#define RTE_TRANSIT_SERVICE_RECORD_SIZE 32U
#define RTE_TRANSIT_EXCEPTION_RECORD_SIZE 16U
#define RTE_TRANSIT_TRIP_RECORD_SIZE 12U
#define RTE_TRANSIT_EVENT_RECORD_SIZE 20U
#define RTE_TILE_PAYLOAD_HEADER_SIZE 64U
#define RTE_TILE_PAYLOAD_DIRECTORY_RECORD_SIZE 32U
#define RTE_TILE_TYPE_WALKING_NODES 0x1000U
#define RTE_TILE_TYPE_WALKING_OFFSETS 0x1001U
#define RTE_TILE_TYPE_WALKING_EDGES 0x1002U
#define RTE_TRANSIT_NO_INDEX 0xffffffffU
#define RTE_TRANSIT_INF_TIME 0xffffffffU
#define RTE_TRANSIT_ACCESS_WALK_M 800U
#define RTE_TRANSIT_EGRESS_WALK_M 1500U
#define RTE_TRANSIT_EGRESS_WALK_SCORE_WEIGHT 2U
#define RTE_TRANSIT_TRANSFER_WALK_M 800U
#define RTE_TRANSIT_MAX_ROUNDS 6U
#define RTE_TRANSIT_MAX_PLAN_LEGS 16U
#define RTE_TRANSIT_STOP_GRID_CELL_E7 50000
#define RTE_TRANSIT_TRANSFER_CELL_RADIUS 4
#define RTE_TRANSIT_STATE_NONE 0U
#define RTE_TRANSIT_STATE_ORIGIN_WALK 1U
#define RTE_TRANSIT_STATE_VEHICLE 2U
#define RTE_TRANSIT_STATE_TRANSFER_WALK 3U

typedef struct {
    unsigned long long offset;
    unsigned long long size;
    unsigned long long record_count;
    unsigned int record_size;
    int present;
} RteAddressSection;

typedef struct {
    unsigned long long offset;
    unsigned long long size;
    unsigned long long record_count;
    unsigned int record_size;
    int present;
} RteTransitSection;

typedef struct {
    unsigned long long section_directory_offset;
    unsigned int section_count;
    unsigned int section_record_size;
    unsigned long long tile_directory_offset;
    unsigned int tile_record_size;
    unsigned int tile_count;
    unsigned int tile_size_m;
    int projection_origin_lat_e7;
    int projection_origin_lon_e7;
} RteHeader;

typedef struct {
    unsigned long long tile_id;
    int x;
    int y;
    unsigned int local_node_count;
    unsigned int local_directed_edge_count;
    unsigned int address_count;
    unsigned long long payload_offset;
    unsigned long long payload_size;
} RteTileRecord;

typedef struct {
    unsigned int entity_type;
    unsigned int flags;
    long long id;
    int lat_e7;
    int lon_e7;
    unsigned long long tile_id;
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
} RteAddressRecord;

typedef struct {
    RteAddressRecord record;
    unsigned int match_count;
    int found;
} RteResolvedAddress;

typedef struct {
    int lat_e7;
    int lon_e7;
    unsigned int first_edge;
    unsigned long long distance;
    unsigned int previous;
    unsigned int previous_edge_meters;
    unsigned char settled;
} RteGraphNode;

typedef struct {
    int lat_e7;
    int lon_e7;
    unsigned int index;
    int used;
} RteGraphNodeSlot;

typedef struct {
    unsigned int to;
    unsigned int next;
    unsigned int meters;
} RteGraphEdge;

typedef struct {
    RteGraphNode *nodes;
    RteGraphNodeSlot *slots;
    RteGraphEdge *edges;
    unsigned int node_count;
    unsigned int node_capacity;
    unsigned int slot_capacity;
    unsigned int edge_count;
    unsigned int edge_capacity;
} RteGraph;

typedef struct {
    unsigned int node;
    unsigned long long distance;
} RteHeapItem;

typedef struct {
    RteHeapItem *items;
    unsigned int count;
    unsigned int capacity;
} RteHeap;

typedef struct {
    unsigned int id_offset;
    unsigned int id_size;
    unsigned int name_offset;
    unsigned int name_size;
    int lat_e7;
    int lon_e7;
    unsigned long long tile_id;
    unsigned int mode_mask;
} RteTransitStop;

typedef struct {
    unsigned int short_name_offset;
    unsigned int short_name_size;
    unsigned int long_name_offset;
    unsigned int long_name_size;
    unsigned int mode;
    unsigned int route_type;
} RteTransitRoute;

typedef struct {
    unsigned int id_offset;
    unsigned int id_size;
    unsigned int start_date;
    unsigned int end_date;
    unsigned int dow_mask;
    unsigned int exception_offset;
    unsigned int exception_count;
} RteTransitService;

typedef struct {
    unsigned int service_index;
    unsigned int date;
    unsigned int exception_type;
} RteTransitException;

typedef struct {
    unsigned int route_index;
    unsigned int service_index;
    unsigned int mode;
} RteTransitTrip;

typedef struct {
    unsigned int trip_index;
    unsigned int stop_index;
    unsigned int arrival_sec;
    unsigned int departure_sec;
    unsigned int sequence;
} RteTransitEvent;

typedef struct {
    RteTransitStop *stops;
    RteTransitRoute *routes;
    RteTransitService *services;
    RteTransitException *exceptions;
    RteTransitTrip *trips;
    RteTransitEvent *events;
    char *strings;
    unsigned int stop_count;
    unsigned int route_count;
    unsigned int service_count;
    unsigned int exception_count;
    unsigned int trip_count;
    unsigned int event_count;
    unsigned int string_size;
} RteTransitStore;

typedef struct {
    int lat_cell;
    int lon_cell;
    unsigned int first_entry;
    int used;
} RteTransitStopGridSlot;

typedef struct {
    int lat_cell;
    int lon_cell;
    unsigned int stop_index;
    unsigned int next_entry;
} RteTransitStopGridEntry;

typedef struct {
    RteTransitStopGridSlot *slots;
    RteTransitStopGridEntry *entries;
    unsigned int slot_capacity;
    unsigned int entry_count;
} RteTransitStopGrid;

typedef struct {
    unsigned int kind;
    unsigned int previous_stop_index;
    unsigned int trip_index;
    unsigned int route_index;
    unsigned int mode;
    unsigned int board_stop_index;
    unsigned int alight_stop_index;
    unsigned int departure_sec;
    unsigned int arrival_sec;
    unsigned int walk_m;
} RteTransitStopState;

typedef struct {
    unsigned int kind;
    unsigned int mode;
    unsigned int route_index;
    unsigned int board_stop_index;
    unsigned int alight_stop_index;
    unsigned int departure_sec;
    unsigned int arrival_sec;
    unsigned int walk_m;
} RteTransitPlanLeg;

typedef struct {
    int found;
    unsigned int mode;
    unsigned int route_index;
    unsigned int board_stop_index;
    unsigned int alight_stop_index;
    unsigned int board_departure_sec;
    unsigned int alight_arrival_sec;
    unsigned int walk_to_stop_m;
    unsigned int walk_from_stop_m;
    unsigned int total_sec;
    unsigned int origin_departure_sec;
    unsigned int active_trips;
    unsigned int candidate_origin_stops;
    unsigned int candidate_destination_stops;
    unsigned int transit_leg_count;
    unsigned int leg_count;
    RteTransitPlanLeg legs[RTE_TRANSIT_MAX_PLAN_LEGS];
    unsigned long long events_scanned;
    unsigned long long board_candidates;
} RteTransitPlan;

typedef struct {
    int use_color;
    int json;
    unsigned long long json_seq;
    const char *map_path;
    const char *rpack_path;
    const char *map_width_arg;
    const char *map_height_arg;
    int have_depart;
    int have_arrive;
    int transit_only;
    int verbose;
    unsigned int depart_date;
    unsigned int depart_seconds;
    unsigned int arrive_date;
    unsigned int arrive_seconds;
} RteOutput;

static int address_field_valid(unsigned int offset, unsigned int size, unsigned int strings_size);
static void write_distance_human(unsigned long long meters);

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.rte FROM_ADDRESS TO_ADDRESS [--transit] [--verbose] [--color] [--no-color] [--json] [--depart YYYY-MM-DDTHH:MM[:SS]] [--arrive YYYY-MM-DDTHH:MM[:SS]] [--map OUT.png] [--rpack FILE.rpack] [--width N] [--height N]\n");
}

static int parse_dimension_arg(const char *text) {
    unsigned long long value;

    return rt_parse_uint(text, &value) == 0 && value != 0ULL && value <= 10000ULL ? 0 : -1;
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text += 1;
    return text;
}

static int parse_coord_component_e7(const char **cursor_io, int max_abs_e7, int *value_out) {
    const char *cursor = skip_spaces(*cursor_io);
    int sign = 1;
    unsigned int whole = 0U;
    unsigned int fraction = 0U;
    unsigned int fraction_digits = 0U;
    unsigned int digit_count = 0U;
    unsigned int value;

    if (*cursor == '-') { sign = -1; cursor += 1; }
    else if (*cursor == '+') cursor += 1;

    while (*cursor >= '0' && *cursor <= '9') {
        if (whole > 1000U) return -1;
        whole = whole * 10U + (unsigned int)(*cursor - '0');
        cursor += 1;
        digit_count += 1U;
    }
    if (digit_count == 0U) return -1;
    if (*cursor == '.') {
        cursor += 1;
        while (*cursor >= '0' && *cursor <= '9') {
            if (fraction_digits < 7U) {
                fraction = fraction * 10U + (unsigned int)(*cursor - '0');
                fraction_digits += 1U;
            }
            cursor += 1;
        }
    }
    while (fraction_digits < 7U) { fraction *= 10U; fraction_digits += 1U; }
    if (whole > (unsigned int)(max_abs_e7 / 10000000)) return -1;
    value = whole * 10000000U + fraction;
    if (value > (unsigned int)max_abs_e7) return -1;
    *value_out = sign < 0 ? -(int)value : (int)value;
    *cursor_io = skip_spaces(cursor);
    return 0;
}

static int parse_coordinate_query(const char *query, int *lat_e7_out, int *lon_e7_out) {
    const char *cursor = query;
    int lat_e7;
    int lon_e7;
    if (parse_coord_component_e7(&cursor, 900000000, &lat_e7) != 0) return -1;
    if (*cursor != ',') return -1;
    cursor += 1;
    if (parse_coord_component_e7(&cursor, 1800000000, &lon_e7) != 0) return -1;
    if (*cursor != '\0') return -1;
    *lat_e7_out = lat_e7;
    *lon_e7_out = lon_e7;
    return 0;
}

static int parse_two_digits(const char *text, unsigned int *value_out) {
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9') return -1;
    *value_out = (unsigned int)(text[0] - '0') * 10U + (unsigned int)(text[1] - '0');
    return 0;
}

static int parse_four_digits(const char *text, unsigned int *value_out) {
    unsigned int value = 0U;
    unsigned int index;
    for (index = 0U; index < 4U; ++index) {
        if (text[index] < '0' || text[index] > '9') return -1;
        value = value * 10U + (unsigned int)(text[index] - '0');
    }
    *value_out = value;
    return 0;
}

static int parse_datetime_arg(const char *text, unsigned int *date_out, unsigned int *seconds_out) {
    size_t length = rt_strlen(text);
    unsigned int year;
    unsigned int month;
    unsigned int day;
    unsigned int hour;
    unsigned int minute;
    unsigned int second = 0U;
    if (!(length == 16U || length == 19U)) return -1;
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':') return -1;
    if (length == 19U && text[16] != ':') return -1;
    if (parse_four_digits(text, &year) != 0 || parse_two_digits(text + 5U, &month) != 0 || parse_two_digits(text + 8U, &day) != 0 ||
        parse_two_digits(text + 11U, &hour) != 0 || parse_two_digits(text + 14U, &minute) != 0) return -1;
    if (length == 19U && parse_two_digits(text + 17U, &second) != 0) return -1;
    if (year < 2000U || year > 2100U || month < 1U || month > 12U || day < 1U || day > 31U || hour > 23U || minute > 59U || second > 59U) return -1;
    *date_out = year * 10000U + month * 100U + day;
    *seconds_out = hour * 3600U + minute * 60U + second;
    return 0;
}

static void civil_from_days(long long z, int *year_out, unsigned int *month_out, unsigned int *day_out) {
    long long era;
    unsigned long long doe;
    unsigned long long yoe;
    unsigned long long doy;
    unsigned long long mp;
    unsigned int month;

    z += 719468LL;
    era = (z >= 0LL ? z : z - 146096LL) / 146097LL;
    doe = (unsigned long long)(z - era * 146097LL);
    yoe = (doe - doe / 1460ULL + doe / 36524ULL - doe / 146096ULL) / 365ULL;
    doy = doe - (365ULL * yoe + yoe / 4ULL - yoe / 100ULL);
    mp = (5ULL * doy + 2ULL) / 153ULL;

    *day_out = (unsigned int)(doy - (153ULL * mp + 2ULL) / 5ULL + 1ULL);
    month = (unsigned int)(mp < 10ULL ? mp + 3ULL : mp - 9ULL);
    *month_out = month;
    *year_out = (int)yoe + (int)(era * 400LL) + (month <= 2U ? 1 : 0);
}

static int set_depart_now(RteOutput *output) {
    long long epoch_seconds = platform_get_epoch_time();
    long long days;
    unsigned long long seconds_of_day;
    int year;
    unsigned int month;
    unsigned int day;

    if (epoch_seconds < 0) return -1;
    days = epoch_seconds / 86400LL;
    seconds_of_day = (unsigned long long)(epoch_seconds % 86400LL);
    civil_from_days(days, &year, &month, &day);
    if (year < 2000 || year > 2100) return -1;
    output->depart_date = (unsigned int)year * 10000U + month * 100U + day;
    output->depart_seconds = (unsigned int)seconds_of_day;
    output->have_depart = 1;
    return 0;
}

static int day_of_week_monday0(unsigned int yyyymmdd) {
    static const int table[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned int year = yyyymmdd / 10000U;
    unsigned int month = (yyyymmdd / 100U) % 100U;
    unsigned int day = yyyymmdd % 100U;
    unsigned int year_adj = year;
    int sunday0;
    if (month < 1U || month > 12U) return 0;
    if (month < 3U) year_adj -= 1U;
    sunday0 = (int)((year_adj + year_adj / 4U - year_adj / 100U + year_adj / 400U + (unsigned int)table[month - 1U] + day) % 7U);
    return (sunday0 + 6) % 7;
}

static char hex_digit(unsigned int value) {
    value &= 15U;
    return value < 10U ? (char)('0' + value) : (char)('a' + value - 10U);
}

static void json_write_escaped(int fd, const char *text, size_t size) {
    size_t index;

    rt_write_char(fd, '"');
    for (index = 0U; index < size; ++index) {
        unsigned char ch = (unsigned char)text[index];
        if (ch == '"' || ch == '\\') {
            rt_write_char(fd, '\\');
            rt_write_char(fd, (char)ch);
        } else if (ch == '\n') {
            rt_write_cstr(fd, "\\n");
        } else if (ch == '\r') {
            rt_write_cstr(fd, "\\r");
        } else if (ch == '\t') {
            rt_write_cstr(fd, "\\t");
        } else if (ch < 0x20U) {
            rt_write_cstr(fd, "\\u00");
            rt_write_char(fd, hex_digit(ch >> 4U));
            rt_write_char(fd, hex_digit(ch));
        } else {
            rt_write_char(fd, (char)ch);
        }
    }
    rt_write_char(fd, '"');
}

static void json_write_cstr_escaped(int fd, const char *text) {
    json_write_escaped(fd, text, rt_strlen(text));
}

static void json_event_begin(RteOutput *output, int fd, const char *event) {
    output->json_seq += 1ULL;
    rt_write_cstr(fd, "{\"schema\":\"newos.tool.v1\",\"tool\":\"rte-route\",\"stream\":\"");
    rt_write_cstr(fd, fd == 2 ? "stderr" : "stdout");
    rt_write_cstr(fd, "\",\"event\":");
    json_write_cstr_escaped(fd, event);
    rt_write_cstr(fd, ",\"seq\":");
    rt_write_uint(fd, output->json_seq);
}

static void json_write_coord_e7(int fd, int value) {
    unsigned int absolute;
    unsigned int fraction;
    unsigned int divisor;

    if (value < 0) {
        rt_write_char(fd, '-');
        absolute = (unsigned int)(-value);
    } else {
        absolute = (unsigned int)value;
    }
    rt_write_uint(fd, absolute / 10000000U);
    rt_write_char(fd, '.');
    fraction = absolute % 10000000U;
    for (divisor = 1000000U; divisor != 0U; divisor /= 10U) rt_write_char(fd, (char)('0' + (fraction / divisor) % 10U));
}

static void json_write_coord_pair(int fd, const char *prefix, int lat_e7, int lon_e7) {
    rt_write_cstr(fd, ",\"");
    rt_write_cstr(fd, prefix);
    rt_write_cstr(fd, "_lat\":");
    json_write_coord_e7(fd, lat_e7);
    rt_write_cstr(fd, ",\"");
    rt_write_cstr(fd, prefix);
    rt_write_cstr(fd, "_lon\":");
    json_write_coord_e7(fd, lon_e7);
}

static void json_write_table_string_or_null(int fd, const char *name, const char *strings, unsigned int strings_size, unsigned int offset, unsigned int size) {
    rt_write_cstr(fd, ",\"");
    rt_write_cstr(fd, name);
    rt_write_cstr(fd, "\":");
    if (address_field_valid(offset, size, strings_size) && size != 0U) json_write_escaped(fd, strings + offset, size);
    else rt_write_cstr(fd, "null");
}

static void json_write_hex_u64_string(int fd, unsigned long long value) {
    int shift;
    int started = 0;

    rt_write_cstr(fd, "\"0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        unsigned int digit = (unsigned int)((value >> (unsigned int)shift) & 15ULL);
        if (digit != 0U || started || shift == 0) {
            rt_write_char(fd, hex_digit(digit));
            started = 1;
        }
    }
    rt_write_char(fd, '"');
}

static void json_diagnostic(RteOutput *output, const char *level, const char *message, const char *detail) {
    json_event_begin(output, 2, "diagnostic");
    rt_write_cstr(2, ",\"level\":");
    json_write_cstr_escaped(2, level);
    rt_write_cstr(2, ",\"message\":");
    json_write_cstr_escaped(2, message);
    rt_write_cstr(2, ",\"detail\":");
    if (detail != 0) json_write_cstr_escaped(2, detail);
    else rt_write_cstr(2, "null");
    rt_write_cstr(2, "}\n");
}

static void write_color(const RteOutput *output, const char *code) {
    if (output->use_color) rt_write_cstr(1, code);
}

static void write_color_reset(const RteOutput *output) {
    if (output->use_color) rt_write_cstr(1, "\033[0m");
}

static void write_colored_cstr(const RteOutput *output, const char *code, const char *text) {
    write_color(output, code);
    rt_write_cstr(1, text);
    write_color_reset(output);
}

static void write_colored_uint(const RteOutput *output, const char *code, unsigned long long value) {
    write_color(output, code);
    rt_write_uint(1, value);
    write_color_reset(output);
}

static unsigned int read_u32_le(const unsigned char *in) {
    return (unsigned int)in[0] | ((unsigned int)in[1] << 8U) | ((unsigned int)in[2] << 16U) | ((unsigned int)in[3] << 24U);
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

static void write_hex_u64(unsigned long long value) {
    static const char hex[] = "0123456789abcdef";
    int shift;

    rt_write_cstr(1, "0x");
    for (shift = 60; shift >= 0; shift -= 4) rt_write_char(1, hex[(value >> (unsigned int)shift) & 0x0fU]);
}

static void write_coord_e7(int value) {
    unsigned int absolute;
    unsigned int fraction;
    unsigned int divisor;

    if (value < 0) {
        rt_write_char(1, '-');
        absolute = (unsigned int)(-value);
    } else {
        absolute = (unsigned int)value;
    }
    rt_write_uint(1, absolute / 10000000U);
    rt_write_char(1, '.');
    fraction = absolute % 10000000U;
    for (divisor = 1000000U; divisor != 0U; divisor /= 10U) rt_write_char(1, (char)('0' + (fraction / divisor) % 10U));
}

static int append_char(char *buffer, size_t capacity, size_t *used, char ch) {
    if (*used + 1U >= capacity) return -1;
    buffer[*used] = ch;
    *used += 1U;
    buffer[*used] = '\0';
    return 0;
}

static int append_cstr(char *buffer, size_t capacity, size_t *used, const char *text) {
    size_t index;
    for (index = 0U; text[index] != '\0'; ++index) { if (append_char(buffer, capacity, used, text[index]) != 0) return -1; }
    return 0;
}

static int append_coord_e7(char *buffer, size_t capacity, size_t *used, int value) {
    unsigned int absolute;
    unsigned int fraction;
    unsigned int divisor;
    char digits[16];
    unsigned int count = 0U;
    unsigned int whole;

    if (value < 0) {
        if (append_char(buffer, capacity, used, '-') != 0) return -1;
        absolute = (unsigned int)(-value);
    } else {
        absolute = (unsigned int)value;
    }
    whole = absolute / 10000000U;
    if (whole == 0U) digits[count++] = '0';
    while (whole != 0U) { digits[count++] = (char)('0' + (whole % 10U)); whole /= 10U; }
    while (count != 0U) { count -= 1U; if (append_char(buffer, capacity, used, digits[count]) != 0) return -1; }
    if (append_char(buffer, capacity, used, '.') != 0) return -1;
    fraction = absolute % 10000000U;
    for (divisor = 1000000U; divisor != 0U; divisor /= 10U) { if (append_char(buffer, capacity, used, (char)('0' + (fraction / divisor) % 10U)) != 0) return -1; }
    return 0;
}

static int path_join_sibling_tool(const char *program, const char *tool_name, char *buffer, size_t capacity) {
    size_t length = rt_strlen(program);
    size_t slash = length;
    size_t used = 0U;

    while (slash > 0U && program[slash - 1U] != '/') slash -= 1U;
    if (slash == 0U) return append_cstr(buffer, capacity, &used, tool_name);
    if (slash >= capacity) return -1;
    memcpy(buffer, program, slash);
    used = slash;
    buffer[used] = '\0';
    return append_cstr(buffer, capacity, &used, tool_name);
}

static int derive_temp_polyline_path(const char *map_path, char *buffer, size_t capacity) {
    size_t used = 0U;
    if (append_cstr(buffer, capacity, &used, map_path) != 0) return -1;
    return append_cstr(buffer, capacity, &used, ".route-polyline.tmp");
}

static int path_readable(const char *path) {
    int fd = platform_open_read(path);
    if (fd < 0) return 0;
    (void)platform_close(fd);
    return 1;
}

static int replace_extension(const char *path, const char *extension, char *buffer, size_t capacity) {
    size_t length = rt_strlen(path);
    size_t dot = length;
    size_t used = 0U;

    while (dot > 0U && path[dot - 1U] != '/' && path[dot - 1U] != '.') dot -= 1U;
    if (dot == 0U || path[dot - 1U] == '/') dot = length;
    if (dot >= capacity) return -1;
    memcpy(buffer, path, dot);
    used = dot;
    buffer[used] = '\0';
    return append_cstr(buffer, capacity, &used, extension);
}

static const char *infer_rpack_path(const char *rte_path, char *buffer, size_t capacity) {
    static const char *fallbacks[] = {
        "build/brandenburg-260525.rpack",
        "data/brandenburg-260525.rpack",
        "data/germany.rpack",
        "build/germany.rpack"
    };
    unsigned int index;

    if (replace_extension(rte_path, ".rpack", buffer, capacity) == 0 && path_readable(buffer)) return buffer;
    for (index = 0U; index < (unsigned int)(sizeof(fallbacks) / sizeof(fallbacks[0])); ++index) {
        if (path_readable(fallbacks[index])) return fallbacks[index];
    }
    return 0;
}

static int derive_temp_render_log_path(const char *map_path, char *buffer, size_t capacity) {
    size_t size = rt_strlen(map_path);
    const char *suffix = ".render-log.tmp";
    size_t suffix_size = rt_strlen(suffix);

    if (size + suffix_size + 1U > capacity) return -1;
    memcpy(buffer, map_path, size);
    memcpy(buffer + size, suffix, suffix_size + 1U);
    return 0;
}

static int parse_header(const unsigned char bytes[RTE_HEADER_SIZE], RteHeader *header) {
    if (memcmp(bytes, "OSMRTE01", 8U) != 0) return -1;
    rt_memset(header, 0, sizeof(*header));
    header->section_directory_offset = read_u64_le(bytes + 64U);
    header->section_count = read_u32_le(bytes + 72U);
    header->section_record_size = read_u32_le(bytes + 76U);
    header->tile_directory_offset = read_u64_le(bytes + 80U);
    header->tile_record_size = read_u32_le(bytes + 96U);
    header->tile_count = read_u32_le(bytes + 100U);
    header->tile_size_m = read_u32_le(bytes + 124U);
    header->projection_origin_lat_e7 = read_i32_le(bytes + 128U);
    header->projection_origin_lon_e7 = read_i32_le(bytes + 132U);
    if (read_u32_le(bytes + 12U) != RTE_HEADER_SIZE) return -1;
    if (header->section_record_size != RTE_SECTION_RECORD_SIZE || header->tile_record_size != RTE_TILE_RECORD_SIZE) return -1;
    return 0;
}

static void parse_tile_record(const unsigned char bytes[RTE_TILE_RECORD_SIZE], RteTileRecord *record) {
    record->tile_id = read_u64_le(bytes + 0U);
    record->x = read_i32_le(bytes + 12U);
    record->y = read_i32_le(bytes + 16U);
    record->local_node_count = read_u32_le(bytes + 20U);
    record->local_directed_edge_count = read_u32_le(bytes + 24U);
    record->address_count = read_u32_le(bytes + 40U);
    record->payload_offset = read_u64_le(bytes + 64U);
    record->payload_size = read_u64_le(bytes + 72U);
}

static void parse_address_record(const unsigned char bytes[RTE_ADDRESS_RECORD_SIZE], RteAddressRecord *record) {
    record->entity_type = read_u32_le(bytes + 0U);
    record->flags = read_u32_le(bytes + 4U);
    record->id = read_i64_le(bytes + 8U);
    record->lat_e7 = read_i32_le(bytes + 16U);
    record->lon_e7 = read_i32_le(bytes + 20U);
    record->tile_id = read_u64_le(bytes + 24U);
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

static int read_sections(int fd, const RteHeader *header, RteAddressSection *address_section, RteTransitSection *transit_section) {
    unsigned char bytes[RTE_SECTION_RECORD_SIZE];
    unsigned int index;

    rt_memset(address_section, 0, sizeof(*address_section));
    rt_memset(transit_section, 0, sizeof(*transit_section));
    for (index = 0U; index < header->section_count; ++index) {
        unsigned int type;
        if (read_at(fd, header->section_directory_offset + (unsigned long long)index * RTE_SECTION_RECORD_SIZE, bytes, sizeof(bytes)) != 0) return -1;
        type = read_u32_le(bytes + 0U);
        if (type == RTE_SECTION_ADDRESS_DICTIONARIES) {
            address_section->present = 1;
            address_section->offset = read_u64_le(bytes + 8U);
            address_section->size = read_u64_le(bytes + 16U);
            address_section->record_count = read_u64_le(bytes + 32U);
            address_section->record_size = read_u32_le(bytes + 40U);
        }
        if (type == RTE_SECTION_TRANSIT_STOPS) {
            transit_section->present = 1;
            transit_section->offset = read_u64_le(bytes + 8U);
            transit_section->size = read_u64_le(bytes + 16U);
            transit_section->record_count = read_u64_le(bytes + 32U);
            transit_section->record_size = read_u32_le(bytes + 40U);
        }
    }
    return 0;
}

static unsigned int abs_i32(int value) {
    return value < 0 ? (unsigned int)(-value) : (unsigned int)value;
}

static unsigned int cos_degrees_q1000000(unsigned int degrees) {
    static const unsigned int table[91] = {
        1000000U,999848U,999391U,998630U,997564U,996195U,994522U,992546U,990268U,987688U,
        984808U,981627U,978148U,974370U,970296U,965926U,961262U,956305U,951057U,945519U,
        939693U,933580U,927184U,920505U,913545U,906308U,898794U,891007U,882948U,874620U,
        866025U,857167U,848048U,838671U,829038U,819152U,809017U,798636U,788011U,777146U,
        766044U,754710U,743145U,731354U,719340U,707107U,694658U,681998U,669131U,656059U,
        642788U,629320U,615661U,601815U,587785U,573576U,559193U,544639U,529919U,515038U,
        500000U,484810U,469472U,453990U,438371U,422618U,406737U,390731U,374607U,358368U,
        342020U,325568U,309017U,292372U,275637U,258819U,241922U,224951U,207912U,190809U,
        173648U,156434U,139173U,121869U,104528U,87156U,69756U,52336U,34899U,17452U,0U
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

static int floor_div_ll(long long value, long long divisor) {
    long long quotient = value / divisor;
    long long remainder = value % divisor;
    if (remainder != 0LL && ((remainder < 0LL) != (divisor < 0LL))) quotient -= 1LL;
    return (int)quotient;
}

static unsigned long long route_tile_id(int x, int y) {
    long long x_code = (long long)x + 0x10000000LL;
    long long y_code = (long long)y + 0x10000000LL;
    if (x_code < 0LL) x_code = 0LL;
    if (y_code < 0LL) y_code = 0LL;
    return (((unsigned long long)x_code) << 29U) | (unsigned long long)y_code;
}

static void lat_lon_to_tile(const RteHeader *header, int lat_e7, int lon_e7, int *tile_x_out, int *tile_y_out, unsigned long long *tile_id_out) {
    unsigned int meters_per_degree_lon = meters_per_degree_lon_from_lat_e7(header->projection_origin_lat_e7);
    long long metric_x_m = ((long long)(lon_e7 - header->projection_origin_lon_e7) * (long long)meters_per_degree_lon) / 10000000LL;
    long long metric_y_m = ((long long)(lat_e7 - header->projection_origin_lat_e7) * 111320LL) / 10000000LL;
    int tile_x = floor_div_ll(metric_x_m, (long long)header->tile_size_m);
    int tile_y = floor_div_ll(metric_y_m, (long long)header->tile_size_m);
    *tile_x_out = tile_x;
    *tile_y_out = tile_y;
    *tile_id_out = route_tile_id(tile_x, tile_y);
}

static int read_tile_at(int fd, const RteHeader *header, unsigned int index, RteTileRecord *record) {
    unsigned char bytes[RTE_TILE_RECORD_SIZE];
    if (read_at(fd, header->tile_directory_offset + (unsigned long long)index * RTE_TILE_RECORD_SIZE, bytes, sizeof(bytes)) != 0) return -1;
    parse_tile_record(bytes, record);
    return 0;
}

static int find_tile_by_id(int fd, const RteHeader *header, unsigned long long tile_id, RteTileRecord *record) {
    unsigned int low = 0U;
    unsigned int high = header->tile_count;
    while (low < high) {
        unsigned int mid = low + (high - low) / 2U;
        RteTileRecord candidate;
        if (read_tile_at(fd, header, mid, &candidate) != 0) return -1;
        if (candidate.tile_id == tile_id) { *record = candidate; return 1; }
        if (candidate.tile_id < tile_id) low = mid + 1U;
        else high = mid;
    }
    return 0;
}

static size_t normalize_text_copy(const char *data, size_t size, char *out, size_t out_capacity) {
    size_t index = 0U;
    size_t used = 0U;
    while (index < size && used + 1U < out_capacity) {
        unsigned char ch = (unsigned char)data[index];
        if (ch >= 'A' && ch <= 'Z') { out[used++] = (char)(ch + ('a' - 'A')); index += 1U; }
        else if (ch == 0xc3U && index + 1U < size && (unsigned char)data[index + 1U] == 0x9fU) {
            if (used + 2U >= out_capacity) break;
            out[used++] = 's'; out[used++] = 's'; index += 2U;
        } else { out[used++] = (char)ch; index += 1U; }
    }
    out[used] = '\0';
    return used;
}

static int address_field_valid(unsigned int offset, unsigned int size, unsigned int strings_size) {
    return offset <= strings_size && size <= strings_size - offset;
}

static int normalized_char_is_trim(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',';
}

static int normalized_is_alnum(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
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
    return left_size != 0U && left_size == right_size && memcmp(left, right, left_size) == 0;
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

static int normalized_address_place_matches(const char *strings, unsigned int strings_size, const RteAddressRecord *record, const char *place_norm, size_t place_norm_size) {
    if (place_norm_size == 0U) return 1;
    if (normalized_place_matches(strings, strings_size, record->city_offset, record->city_size, place_norm, place_norm_size)) return 1;
    if (normalized_place_matches(strings, strings_size, record->suburb_offset, record->suburb_size, place_norm, place_norm_size)) return 1;
    if (normalized_place_matches(strings, strings_size, record->postcode_offset, record->postcode_size, place_norm, place_norm_size)) return 1;
    return 0;
}

static int normalized_street_house_matches(const char *strings, unsigned int strings_size, const RteAddressRecord *record, const char *main_norm, size_t main_norm_size) {
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
        if (normalized_span_equals(main_norm + street_start, street_end - street_start, street_norm, street_norm_size)) return 1;
    }
    return 0;
}

static void split_normalized_address_query(const char *query_norm, size_t query_norm_size, const char **main_out, size_t *main_size_out, const char **place_out, size_t *place_size_out) {
    size_t comma = query_norm_size;
    size_t start;
    size_t end;
    size_t index;
    for (index = 0U; index < query_norm_size; ++index) { if (query_norm[index] == ',') { comma = index; break; } }
    normalized_trim_span(query_norm, comma, &start, &end);
    *main_out = query_norm + start;
    *main_size_out = end - start;
    if (comma < query_norm_size) {
        normalized_trim_span(query_norm + comma + 1U, query_norm_size - comma - 1U, &start, &end);
        *place_out = query_norm + comma + 1U + start;
        *place_size_out = end - start;
    } else { *place_out = query_norm + query_norm_size; *place_size_out = 0U; }
}

static int read_address_strings(int fd, const RteAddressSection *section, char **strings_out, unsigned int *strings_size_out) {
    unsigned char header[RTE_ADDRESS_HEADER_SIZE];
    unsigned int strings_size;
    unsigned long long strings_offset;
    char *strings;

    if (!section->present || section->record_size != RTE_ADDRESS_RECORD_SIZE) return -1;
    if (read_at(fd, section->offset, header, sizeof(header)) != 0 || memcmp(header, "ADDRIDX1", 8U) != 0) return -1;
    if (read_u32_le(header + 24U) != RTE_ADDRESS_RECORD_SIZE) return -1;
    strings_size = read_u32_le(header + 28U);
    strings_offset = read_u64_le(header + 32U);
    if (strings_offset > section->size || (unsigned long long)strings_size > section->size - strings_offset) return -1;
    strings = (char *)rt_malloc((size_t)strings_size + 1U);
    if (strings == 0) return -1;
    if (strings_size != 0U && read_at(fd, section->offset + strings_offset, strings, strings_size) != 0) { rt_free(strings); return -1; }
    strings[strings_size] = '\0';
    *strings_out = strings;
    *strings_size_out = strings_size;
    return 0;
}

static void parse_transit_stop_record(const unsigned char bytes[RTE_TRANSIT_STOP_RECORD_SIZE], RteTransitStop *record) {
    record->id_offset = read_u32_le(bytes + 0U);
    record->id_size = read_u32_le(bytes + 4U);
    record->name_offset = read_u32_le(bytes + 8U);
    record->name_size = read_u32_le(bytes + 12U);
    record->lat_e7 = read_i32_le(bytes + 16U);
    record->lon_e7 = read_i32_le(bytes + 20U);
    record->tile_id = read_u64_le(bytes + 24U);
    record->mode_mask = read_u32_le(bytes + 32U);
}

static void parse_transit_route_record(const unsigned char bytes[RTE_TRANSIT_ROUTE_RECORD_SIZE], RteTransitRoute *record) {
    record->short_name_offset = read_u32_le(bytes + 0U);
    record->short_name_size = read_u32_le(bytes + 4U);
    record->long_name_offset = read_u32_le(bytes + 8U);
    record->long_name_size = read_u32_le(bytes + 12U);
    record->mode = read_u32_le(bytes + 16U);
    record->route_type = read_u32_le(bytes + 20U);
}

static void parse_transit_service_record(const unsigned char bytes[RTE_TRANSIT_SERVICE_RECORD_SIZE], RteTransitService *record) {
    record->id_offset = read_u32_le(bytes + 0U);
    record->id_size = read_u32_le(bytes + 4U);
    record->start_date = read_u32_le(bytes + 8U);
    record->end_date = read_u32_le(bytes + 12U);
    record->dow_mask = read_u32_le(bytes + 16U);
    record->exception_offset = read_u32_le(bytes + 20U);
    record->exception_count = read_u32_le(bytes + 24U);
}

static void parse_transit_exception_record(const unsigned char bytes[RTE_TRANSIT_EXCEPTION_RECORD_SIZE], RteTransitException *record) {
    record->service_index = read_u32_le(bytes + 0U);
    record->date = read_u32_le(bytes + 4U);
    record->exception_type = read_u32_le(bytes + 8U);
}

static void parse_transit_trip_record(const unsigned char bytes[RTE_TRANSIT_TRIP_RECORD_SIZE], RteTransitTrip *record) {
    record->route_index = read_u32_le(bytes + 0U);
    record->service_index = read_u32_le(bytes + 4U);
    record->mode = read_u32_le(bytes + 8U);
}

static void parse_transit_event_record(const unsigned char bytes[RTE_TRANSIT_EVENT_RECORD_SIZE], RteTransitEvent *record) {
    record->trip_index = read_u32_le(bytes + 0U);
    record->stop_index = read_u32_le(bytes + 4U);
    record->arrival_sec = read_u32_le(bytes + 8U);
    record->departure_sec = read_u32_le(bytes + 12U);
    record->sequence = read_u32_le(bytes + 16U);
}

static int read_transit_array(int fd, unsigned long long absolute_offset, unsigned int count, unsigned int file_record_size, size_t memory_record_size, void *records, void (*parse_record)(const unsigned char *, void *)) {
    unsigned char *raw;
    unsigned int index;
    unsigned long long size = (unsigned long long)count * file_record_size;
    if (count == 0U) return 0;
    raw = (unsigned char *)rt_malloc((size_t)size);
    if (raw == 0) return -1;
    if (read_at(fd, absolute_offset, raw, (size_t)size) != 0) { rt_free(raw); return -1; }
    for (index = 0U; index < count; ++index) parse_record(raw + (unsigned long long)index * file_record_size, (char *)records + (size_t)index * memory_record_size);
    rt_free(raw);
    return 0;
}

static int read_transit_section(int fd, const RteTransitSection *section, RteTransitStore *store) {
    unsigned char header[RTE_TRANSIT_SECTION_HEADER_SIZE];
    unsigned long long stops_offset;
    unsigned long long routes_offset;
    unsigned long long services_offset;
    unsigned long long exceptions_offset;
    unsigned long long trips_offset;
    unsigned long long events_offset;
    unsigned long long strings_offset;
    unsigned int strings_size;
    unsigned long long stop_count;
    unsigned long long route_count;
    unsigned long long service_count;
    unsigned long long exception_count;
    unsigned long long trip_count;
    unsigned long long event_count;

    rt_memset(store, 0, sizeof(*store));
    if (!section->present) return 0;
    if (section->record_size != RTE_TRANSIT_STOP_RECORD_SIZE || read_at(fd, section->offset, header, sizeof(header)) != 0 || memcmp(header, "GTFSLEG1", 8U) != 0) return -1;
    if (read_u32_le(header + 12U) != RTE_TRANSIT_SECTION_HEADER_SIZE) return -1;
    stop_count = read_u64_le(header + 16U);
    route_count = read_u64_le(header + 24U);
    service_count = read_u64_le(header + 32U);
    exception_count = read_u64_le(header + 40U);
    trip_count = read_u64_le(header + 48U);
    event_count = read_u64_le(header + 56U);
    strings_size = read_u32_le(header + 64U);
    stops_offset = read_u64_le(header + 72U);
    routes_offset = read_u64_le(header + 80U);
    services_offset = read_u64_le(header + 88U);
    exceptions_offset = read_u64_le(header + 96U);
    trips_offset = read_u64_le(header + 104U);
    events_offset = read_u64_le(header + 112U);
    strings_offset = read_u64_le(header + 120U);
    if (stop_count > 0xffffffffULL || route_count > 0xffffffffULL || service_count > 0xffffffffULL || exception_count > 0xffffffffULL || trip_count > 0xffffffffULL || event_count > 0xffffffffULL) return -1;
    if (strings_offset > section->size || (unsigned long long)strings_size > section->size - strings_offset) return -1;
    store->stop_count = (unsigned int)stop_count;
    store->route_count = (unsigned int)route_count;
    store->service_count = (unsigned int)service_count;
    store->exception_count = (unsigned int)exception_count;
    store->trip_count = (unsigned int)trip_count;
    store->event_count = (unsigned int)event_count;
    store->string_size = strings_size;
    store->stops = store->stop_count != 0U ? (RteTransitStop *)rt_malloc((size_t)store->stop_count * sizeof(*store->stops)) : 0;
    store->routes = store->route_count != 0U ? (RteTransitRoute *)rt_malloc((size_t)store->route_count * sizeof(*store->routes)) : 0;
    store->services = store->service_count != 0U ? (RteTransitService *)rt_malloc((size_t)store->service_count * sizeof(*store->services)) : 0;
    store->exceptions = store->exception_count != 0U ? (RteTransitException *)rt_malloc((size_t)store->exception_count * sizeof(*store->exceptions)) : 0;
    store->trips = store->trip_count != 0U ? (RteTransitTrip *)rt_malloc((size_t)store->trip_count * sizeof(*store->trips)) : 0;
    store->events = store->event_count != 0U ? (RteTransitEvent *)rt_malloc((size_t)store->event_count * sizeof(*store->events)) : 0;
    store->strings = (char *)rt_malloc((size_t)strings_size + 1U);
    if ((store->stop_count != 0U && store->stops == 0) || (store->route_count != 0U && store->routes == 0) || (store->service_count != 0U && store->services == 0) ||
        (store->exception_count != 0U && store->exceptions == 0) || (store->trip_count != 0U && store->trips == 0) || (store->event_count != 0U && store->events == 0) || store->strings == 0) return -1;
    if (read_transit_array(fd, section->offset + stops_offset, store->stop_count, RTE_TRANSIT_STOP_RECORD_SIZE, sizeof(*store->stops), store->stops, (void (*)(const unsigned char *, void *))parse_transit_stop_record) != 0 ||
        read_transit_array(fd, section->offset + routes_offset, store->route_count, RTE_TRANSIT_ROUTE_RECORD_SIZE, sizeof(*store->routes), store->routes, (void (*)(const unsigned char *, void *))parse_transit_route_record) != 0 ||
        read_transit_array(fd, section->offset + services_offset, store->service_count, RTE_TRANSIT_SERVICE_RECORD_SIZE, sizeof(*store->services), store->services, (void (*)(const unsigned char *, void *))parse_transit_service_record) != 0 ||
        read_transit_array(fd, section->offset + exceptions_offset, store->exception_count, RTE_TRANSIT_EXCEPTION_RECORD_SIZE, sizeof(*store->exceptions), store->exceptions, (void (*)(const unsigned char *, void *))parse_transit_exception_record) != 0 ||
        read_transit_array(fd, section->offset + trips_offset, store->trip_count, RTE_TRANSIT_TRIP_RECORD_SIZE, sizeof(*store->trips), store->trips, (void (*)(const unsigned char *, void *))parse_transit_trip_record) != 0 ||
        read_transit_array(fd, section->offset + events_offset, store->event_count, RTE_TRANSIT_EVENT_RECORD_SIZE, sizeof(*store->events), store->events, (void (*)(const unsigned char *, void *))parse_transit_event_record) != 0) return -1;
    if (strings_size != 0U && read_at(fd, section->offset + strings_offset, store->strings, strings_size) != 0) return -1;
    store->strings[strings_size] = '\0';
    return 0;
}

static int resolve_address(int fd, const RteAddressSection *section, const char *query, const char *strings, unsigned int strings_size, RteResolvedAddress *resolved) {
    char *query_norm;
    unsigned char *record_buffer;
    size_t query_size = rt_strlen(query);
    size_t query_norm_size;
    const char *query_main_norm;
    const char *query_place_norm;
    size_t query_main_norm_size;
    size_t query_place_norm_size;
    unsigned long long index;

    rt_memset(resolved, 0, sizeof(*resolved));
    query_norm = (char *)rt_malloc(query_size * 2U + 1U);
    record_buffer = (unsigned char *)rt_malloc((size_t)RTE_ADDRESS_SCAN_BATCH_RECORDS * RTE_ADDRESS_RECORD_SIZE);
    if (query_norm == 0 || record_buffer == 0) { rt_free(query_norm); rt_free(record_buffer); return -1; }
    query_norm_size = normalize_text_copy(query, query_size, query_norm, query_size * 2U + 1U);
    split_normalized_address_query(query_norm, query_norm_size, &query_main_norm, &query_main_norm_size, &query_place_norm, &query_place_norm_size);

    for (index = 0ULL; index < section->record_count;) {
        unsigned long long remaining = section->record_count - index;
        unsigned int batch_count = remaining > RTE_ADDRESS_SCAN_BATCH_RECORDS ? RTE_ADDRESS_SCAN_BATCH_RECORDS : (unsigned int)remaining;
        size_t batch_size = (size_t)batch_count * RTE_ADDRESS_RECORD_SIZE;
        unsigned int batch_index;
        if (read_at(fd, section->offset + RTE_ADDRESS_HEADER_SIZE + index * RTE_ADDRESS_RECORD_SIZE, record_buffer, batch_size) != 0) {
            rt_free(query_norm); rt_free(record_buffer); return -1;
        }
        for (batch_index = 0U; batch_index < batch_count; ++batch_index) {
            RteAddressRecord record;
            parse_address_record(record_buffer + (size_t)batch_index * RTE_ADDRESS_RECORD_SIZE, &record);
            if (normalized_address_place_matches(strings, strings_size, &record, query_place_norm, query_place_norm_size) &&
                normalized_street_house_matches(strings, strings_size, &record, query_main_norm, query_main_norm_size)) {
                resolved->match_count += 1U;
                if (!resolved->found || ((record.flags & 1U) != 0U && (resolved->record.flags & 1U) == 0U)) {
                    resolved->record = record;
                    resolved->found = 1;
                }
            }
        }
        index += batch_count;
    }
    rt_free(query_norm);
    rt_free(record_buffer);
    return 0;
}

static int resolve_query(int fd, const RteHeader *header, const RteAddressSection *section, const char *query, const char *strings, unsigned int strings_size, RteResolvedAddress *resolved) {
    int lat_e7;
    int lon_e7;
    int tile_x;
    int tile_y;
    unsigned long long tile_id;

    if (parse_coordinate_query(query, &lat_e7, &lon_e7) != 0) return resolve_address(fd, section, query, strings, strings_size, resolved);
    lat_lon_to_tile(header, lat_e7, lon_e7, &tile_x, &tile_y, &tile_id);
    (void)tile_x;
    (void)tile_y;
    rt_memset(resolved, 0, sizeof(*resolved));
    resolved->found = 1;
    resolved->match_count = 1U;
    resolved->record.flags = 1U;
    resolved->record.lat_e7 = lat_e7;
    resolved->record.lon_e7 = lon_e7;
    resolved->record.tile_id = tile_id;
    return 0;
}

static void write_text_field(const char *prefix, const char *strings, unsigned int strings_size, unsigned int offset, unsigned int size) {
    rt_write_cstr(1, prefix);
    if (address_field_valid(offset, size, strings_size)) (void)rt_write_all(1, strings + offset, size);
    rt_write_char(1, '\n');
}

static const char *entity_type_name(unsigned int type) {
    if (type == 1U) return "node";
    if (type == 2U) return "way";
    if (type == 3U) return "relation";
    return "unknown";
}

static void write_address_summary(const char *label, const RteResolvedAddress *resolved, const char *strings, unsigned int strings_size) {
    rt_write_cstr(1, label); rt_write_cstr(1, "_matches: "); rt_write_uint(1, resolved->match_count); rt_write_char(1, '\n');
    if (!resolved->found) { rt_write_cstr(1, label); rt_write_cstr(1, "_status: not_found\n"); return; }
    rt_write_cstr(1, label); rt_write_cstr(1, "_status: found\n");
    rt_write_cstr(1, label); rt_write_cstr(1, "_type: "); rt_write_cstr(1, entity_type_name(resolved->record.entity_type)); rt_write_char(1, '\n');
    rt_write_cstr(1, label); rt_write_cstr(1, "_id: "); rt_write_int(1, resolved->record.id); rt_write_char(1, '\n');
    rt_write_cstr(1, label); write_text_field("_city: ", strings, strings_size, resolved->record.city_offset, resolved->record.city_size);
    rt_write_cstr(1, label); write_text_field("_street: ", strings, strings_size, resolved->record.street_offset, resolved->record.street_size);
    rt_write_cstr(1, label); write_text_field("_housenumber: ", strings, strings_size, resolved->record.housenumber_offset, resolved->record.housenumber_size);
    rt_write_cstr(1, label); write_text_field("_postcode: ", strings, strings_size, resolved->record.postcode_offset, resolved->record.postcode_size);
    rt_write_cstr(1, label); rt_write_cstr(1, "_has_coordinate: "); rt_write_cstr(1, (resolved->record.flags & 1U) != 0U ? "yes" : "no"); rt_write_char(1, '\n');
    if ((resolved->record.flags & 1U) != 0U) {
        rt_write_cstr(1, label); rt_write_cstr(1, "_lat: "); write_coord_e7(resolved->record.lat_e7); rt_write_char(1, '\n');
        rt_write_cstr(1, label); rt_write_cstr(1, "_lon: "); write_coord_e7(resolved->record.lon_e7); rt_write_char(1, '\n');
        rt_write_cstr(1, label); rt_write_cstr(1, "_tile_id: "); write_hex_u64(resolved->record.tile_id); rt_write_char(1, '\n');
    }
}

static void json_write_address_event(RteOutput *output, const char *role, const char *query, const RteResolvedAddress *resolved, const char *strings, unsigned int strings_size) {
    json_event_begin(output, 1, "address");
    rt_write_cstr(1, ",\"data\":{\"role\":");
    json_write_cstr_escaped(1, role);
    rt_write_cstr(1, ",\"query\":");
    json_write_cstr_escaped(1, query);
    rt_write_cstr(1, ",\"match_count\":");
    rt_write_uint(1, resolved->match_count);
    rt_write_cstr(1, ",\"status\":");
    json_write_cstr_escaped(1, resolved->found ? "found" : "not_found");
    if (resolved->found) {
        rt_write_cstr(1, ",\"entity_type\":");
        json_write_cstr_escaped(1, entity_type_name(resolved->record.entity_type));
        rt_write_cstr(1, ",\"id\":");
        rt_write_int(1, resolved->record.id);
        json_write_table_string_or_null(1, "city", strings, strings_size, resolved->record.city_offset, resolved->record.city_size);
        json_write_table_string_or_null(1, "suburb", strings, strings_size, resolved->record.suburb_offset, resolved->record.suburb_size);
        json_write_table_string_or_null(1, "street", strings, strings_size, resolved->record.street_offset, resolved->record.street_size);
        json_write_table_string_or_null(1, "housenumber", strings, strings_size, resolved->record.housenumber_offset, resolved->record.housenumber_size);
        json_write_table_string_or_null(1, "postcode", strings, strings_size, resolved->record.postcode_offset, resolved->record.postcode_size);
        rt_write_cstr(1, ",\"has_coordinate\":");
        rt_write_cstr(1, (resolved->record.flags & 1U) != 0U ? "true" : "false");
        if ((resolved->record.flags & 1U) != 0U) {
            rt_write_cstr(1, ",\"lat\":");
            json_write_coord_e7(1, resolved->record.lat_e7);
            rt_write_cstr(1, ",\"lon\":");
            json_write_coord_e7(1, resolved->record.lon_e7);
            rt_write_cstr(1, ",\"tile_id\":");
            json_write_hex_u64_string(1, resolved->record.tile_id);
        }
    }
    rt_write_cstr(1, "}}\n");
}

static void json_write_metadata_event(RteOutput *output, const char *from_query, const char *to_query) {
    json_event_begin(output, 1, "metadata");
    rt_write_cstr(1, ",\"data\":{\"format\":\"OSMRTE01\",\"profile\":\"walking\",\"from_query\":");
    json_write_cstr_escaped(1, from_query);
    rt_write_cstr(1, ",\"to_query\":");
    json_write_cstr_escaped(1, to_query);
    rt_write_cstr(1, "}}\n");
}

static void json_write_route_status_event(RteOutput *output, const char *status, const char *reason) {
    json_event_begin(output, 1, "route_status");
    rt_write_cstr(1, ",\"data\":{\"status\":");
    json_write_cstr_escaped(1, status);
    rt_write_cstr(1, ",\"reason\":");
    if (reason != 0) json_write_cstr_escaped(1, reason);
    else rt_write_cstr(1, "null");
    rt_write_cstr(1, "}}\n");
}

static unsigned long long isqrt_u64(unsigned long long value) {
    unsigned long long result = 0ULL;
    unsigned long long bit = 1ULL << 62U;
    while (bit > value) bit >>= 2U;
    while (bit != 0ULL) {
        if (value >= result + bit) { value -= result + bit; result = (result >> 1U) + bit; }
        else result >>= 1U;
        bit >>= 2U;
    }
    return result;
}

static unsigned int direct_distance_m(int from_lat_e7, int from_lon_e7, int to_lat_e7, int to_lon_e7) {
    long long dlat = (long long)to_lat_e7 - (long long)from_lat_e7;
    long long dlon = (long long)to_lon_e7 - (long long)from_lon_e7;
    unsigned int meters_per_degree_lon = meters_per_degree_lon_from_lat_e7((from_lat_e7 + to_lat_e7) / 2);
    long long dy = (dlat * 111320LL) / 10000000LL;
    long long dx = (dlon * (long long)meters_per_degree_lon) / 10000000LL;
    unsigned long long ax = dx < 0 ? (unsigned long long)(-dx) : (unsigned long long)dx;
    unsigned long long ay = dy < 0 ? (unsigned long long)(-dy) : (unsigned long long)dy;
    unsigned long long distance = isqrt_u64(ax * ax + ay * ay);
    return distance > 0xffffffffULL ? 0xffffffffU : (unsigned int)distance;
}

static const char *transit_mode_name(unsigned int mode) {
    if (mode == 1U) return "tram";
    if (mode == 2U) return "subway";
    if (mode == 3U) return "train";
    if (mode == 4U) return "bus";
    return "transit";
}

static int transit_string_valid(const RteTransitStore *store, unsigned int offset, unsigned int size) {
    return offset <= store->string_size && size <= store->string_size - offset;
}

static void write_transit_string(const RteTransitStore *store, unsigned int offset, unsigned int size) {
    if (transit_string_valid(store, offset, size) && size != 0U) (void)rt_write_all(1, store->strings + offset, size);
}

static void json_write_transit_string_or_null(int fd, const RteTransitStore *store, unsigned int offset, unsigned int size) {
    if (transit_string_valid(store, offset, size) && size != 0U) json_write_escaped(fd, store->strings + offset, size);
    else rt_write_cstr(fd, "null");
}

static void write_hhmm(unsigned int seconds) {
    unsigned int day = seconds / 86400U;
    unsigned int in_day = seconds % 86400U;
    unsigned int hour = in_day / 3600U;
    unsigned int minute = (in_day / 60U) % 60U;
    if (hour < 10U) rt_write_char(1, '0');
    rt_write_uint(1, hour);
    rt_write_char(1, ':');
    if (minute < 10U) rt_write_char(1, '0');
    rt_write_uint(1, minute);
    if (day > 0U) { rt_write_cstr(1, " (+"); rt_write_uint(1, day); rt_write_cstr(1, "d)"); }
}

static int transit_service_active(const RteTransitStore *store, unsigned int service_index, unsigned int date) {
    const RteTransitService *service;
    unsigned int index;
    unsigned int weekday;
    int base_active;
    if (service_index >= store->service_count) return 0;
    service = &store->services[service_index];
    for (index = 0U; index < service->exception_count && service->exception_offset + index < store->exception_count; ++index) {
        const RteTransitException *exception = &store->exceptions[service->exception_offset + index];
        if (exception->date == date) return exception->exception_type == 1U;
    }
    if (service->start_date == 0U || service->end_date == 0U || date < service->start_date || date > service->end_date) return 0;
    weekday = (unsigned int)day_of_week_monday0(date);
    base_active = (service->dow_mask & (1U << weekday)) != 0U;
    return base_active;
}

static unsigned int walking_seconds_for_meters(unsigned int meters) {
    return (unsigned int)(((unsigned long long)meters * 3600ULL + 4799ULL) / 4800ULL);
}

static unsigned int transit_finish_score(unsigned int arrival_sec, unsigned int egress_walk_sec) {
    unsigned long long score = (unsigned long long)arrival_sec + (unsigned long long)egress_walk_sec * (unsigned long long)RTE_TRANSIT_EGRESS_WALK_SCORE_WEIGHT;
    return score > 0xffffffffULL ? 0xffffffffU : (unsigned int)score;
}

static int transit_cell_for_e7(int value) {
    if (value >= 0) return value / RTE_TRANSIT_STOP_GRID_CELL_E7;
    return -(((-value) + RTE_TRANSIT_STOP_GRID_CELL_E7 - 1) / RTE_TRANSIT_STOP_GRID_CELL_E7);
}

static unsigned int transit_hash_cell(int lat_cell, int lon_cell) {
    unsigned int x = (unsigned int)lat_cell;
    unsigned int y = (unsigned int)lon_cell;
    unsigned int hash = x * 2654435761U;
    hash ^= y + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    return hash;
}

static int transit_stop_grid_build(const RteTransitStore *store, RteTransitStopGrid *grid) {
    unsigned int capacity = 1U;
    unsigned int index;

    rt_memset(grid, 0, sizeof(*grid));
    while (capacity < store->stop_count * 2U && capacity < 0x80000000U) capacity <<= 1U;
    if (capacity < 16U) capacity = 16U;
    grid->slots = (RteTransitStopGridSlot *)rt_malloc(sizeof(*grid->slots) * capacity);
    grid->entries = (RteTransitStopGridEntry *)rt_malloc(sizeof(*grid->entries) * (size_t)store->stop_count);
    if (grid->slots == 0 || (store->stop_count != 0U && grid->entries == 0)) return -1;
    rt_memset(grid->slots, 0, sizeof(*grid->slots) * capacity);
    grid->slot_capacity = capacity;
    for (index = 0U; index < store->stop_count; ++index) {
        int lat_cell = transit_cell_for_e7(store->stops[index].lat_e7);
        int lon_cell = transit_cell_for_e7(store->stops[index].lon_e7);
        unsigned int slot = transit_hash_cell(lat_cell, lon_cell) & (capacity - 1U);
        while (grid->slots[slot].used && (grid->slots[slot].lat_cell != lat_cell || grid->slots[slot].lon_cell != lon_cell)) slot = (slot + 1U) & (capacity - 1U);
        grid->entries[index].lat_cell = lat_cell;
        grid->entries[index].lon_cell = lon_cell;
        grid->entries[index].stop_index = index;
        if (!grid->slots[slot].used) {
            grid->slots[slot].used = 1;
            grid->slots[slot].lat_cell = lat_cell;
            grid->slots[slot].lon_cell = lon_cell;
            grid->slots[slot].first_entry = RTE_TRANSIT_NO_INDEX;
        }
        grid->entries[index].next_entry = grid->slots[slot].first_entry;
        grid->slots[slot].first_entry = index;
    }
    grid->entry_count = store->stop_count;
    return 0;
}

static void transit_stop_grid_destroy(RteTransitStopGrid *grid) {
    rt_free(grid->slots);
    rt_free(grid->entries);
    rt_memset(grid, 0, sizeof(*grid));
}

static const RteTransitStopGridSlot *transit_stop_grid_find_cell(const RteTransitStopGrid *grid, int lat_cell, int lon_cell) {
    unsigned int slot;
    unsigned int probes = 0U;
    if (grid->slot_capacity == 0U) return 0;
    slot = transit_hash_cell(lat_cell, lon_cell) & (grid->slot_capacity - 1U);
    while (probes < grid->slot_capacity && grid->slots[slot].used) {
        if (grid->slots[slot].lat_cell == lat_cell && grid->slots[slot].lon_cell == lon_cell) return &grid->slots[slot];
        slot = (slot + 1U) & (grid->slot_capacity - 1U);
        probes += 1U;
    }
    return 0;
}

static int transit_push_stop(unsigned int *stops, unsigned char *queued, unsigned int *count, unsigned int capacity, unsigned int stop_index) {
    if (stop_index >= capacity) return -1;
    if (queued[stop_index]) return 0;
    if (*count >= capacity) return -1;
    queued[stop_index] = 1U;
    stops[*count] = stop_index;
    *count += 1U;
    return 0;
}

static int transit_apply_transfer_walks(
    const RteTransitStore *store,
    const RteTransitStopGrid *grid,
    unsigned int *arrival,
    unsigned char *rides_used,
    RteTransitStopState *states,
    const unsigned int *changed_stops,
    unsigned int changed_count
) {
    unsigned int changed_index;

    for (changed_index = 0U; changed_index < changed_count; ++changed_index) {
        unsigned int from_stop_index = changed_stops[changed_index];
        const RteTransitStop *from_stop;
        int lat_cell;
        int lon_cell;
        int dlat;
        if (from_stop_index >= store->stop_count || arrival[from_stop_index] == RTE_TRANSIT_INF_TIME) continue;
        from_stop = &store->stops[from_stop_index];
        lat_cell = transit_cell_for_e7(from_stop->lat_e7);
        lon_cell = transit_cell_for_e7(from_stop->lon_e7);
        for (dlat = -RTE_TRANSIT_TRANSFER_CELL_RADIUS; dlat <= RTE_TRANSIT_TRANSFER_CELL_RADIUS; ++dlat) {
            int dlon;
            for (dlon = -RTE_TRANSIT_TRANSFER_CELL_RADIUS; dlon <= RTE_TRANSIT_TRANSFER_CELL_RADIUS; ++dlon) {
                const RteTransitStopGridSlot *slot = transit_stop_grid_find_cell(grid, lat_cell + dlat, lon_cell + dlon);
                unsigned int entry_index;
                if (slot == 0) continue;
                entry_index = slot->first_entry;
                while (entry_index != RTE_TRANSIT_NO_INDEX && entry_index < grid->entry_count) {
                    unsigned int to_stop_index = grid->entries[entry_index].stop_index;
                    const RteTransitStop *to_stop = &store->stops[to_stop_index];
                    if (to_stop_index != from_stop_index) {
                        unsigned int meters = direct_distance_m(from_stop->lat_e7, from_stop->lon_e7, to_stop->lat_e7, to_stop->lon_e7);
                        if (meters <= RTE_TRANSIT_TRANSFER_WALK_M) {
                            unsigned int walk_sec = walking_seconds_for_meters(meters);
                            if (arrival[from_stop_index] <= RTE_TRANSIT_INF_TIME - walk_sec && arrival[from_stop_index] + walk_sec < arrival[to_stop_index]) {
                                arrival[to_stop_index] = arrival[from_stop_index] + walk_sec;
                                rides_used[to_stop_index] = rides_used[from_stop_index];
                                states[to_stop_index].kind = RTE_TRANSIT_STATE_TRANSFER_WALK;
                                states[to_stop_index].previous_stop_index = from_stop_index;
                                states[to_stop_index].trip_index = RTE_TRANSIT_NO_INDEX;
                                states[to_stop_index].route_index = RTE_TRANSIT_NO_INDEX;
                                states[to_stop_index].mode = 0U;
                                states[to_stop_index].board_stop_index = from_stop_index;
                                states[to_stop_index].alight_stop_index = to_stop_index;
                                states[to_stop_index].departure_sec = arrival[from_stop_index];
                                states[to_stop_index].arrival_sec = arrival[to_stop_index];
                                states[to_stop_index].walk_m = meters;
                            }
                        }
                    }
                    entry_index = grid->entries[entry_index].next_entry;
                }
            }
        }
    }
    return 0;
}

static void transit_reverse_plan_legs(RteTransitPlan *plan, const RteTransitPlanLeg *reverse_legs, unsigned int reverse_count) {
    unsigned int index;
    plan->leg_count = reverse_count;
    plan->transit_leg_count = 0U;
    for (index = 0U; index < reverse_count; ++index) {
        const RteTransitPlanLeg *leg = &reverse_legs[reverse_count - 1U - index];
        plan->legs[index] = *leg;
        if (leg->kind == RTE_TRANSIT_STATE_VEHICLE) {
            if (plan->transit_leg_count == 0U) {
                plan->mode = leg->mode;
                plan->route_index = leg->route_index;
                plan->board_stop_index = leg->board_stop_index;
                plan->alight_stop_index = leg->alight_stop_index;
                plan->board_departure_sec = leg->departure_sec;
                plan->alight_arrival_sec = leg->arrival_sec;
            }
            plan->transit_leg_count += 1U;
        }
    }
}

static int transit_build_plan_from_arrivals(
    const RteTransitStore *store,
    const RteResolvedAddress *from,
    const RteResolvedAddress *to,
    const RteOutput *output,
    const unsigned int *arrival,
    const unsigned char *rides_used,
    const RteTransitStopState *states,
    RteTransitPlan *plan
) {
    unsigned int stop_index;
    unsigned int best_stop_index = RTE_TRANSIT_NO_INDEX;
    unsigned int best_arrival = RTE_TRANSIT_INF_TIME;
    unsigned int best_score = RTE_TRANSIT_INF_TIME;
    unsigned int best_egress_m = 0U;

    for (stop_index = 0U; stop_index < store->stop_count; ++stop_index) {
        unsigned int walk_m = direct_distance_m(to->record.lat_e7, to->record.lon_e7, store->stops[stop_index].lat_e7, store->stops[stop_index].lon_e7);
        if (walk_m <= RTE_TRANSIT_EGRESS_WALK_M) plan->candidate_destination_stops += 1U;
        if (rides_used[stop_index] == 0U || arrival[stop_index] == RTE_TRANSIT_INF_TIME || walk_m > RTE_TRANSIT_EGRESS_WALK_M) continue;
        {
            unsigned int walk_sec = walking_seconds_for_meters(walk_m);
            unsigned int finish_arrival;
            unsigned int finish_score;
            if (arrival[stop_index] > RTE_TRANSIT_INF_TIME - walk_sec) continue;
            finish_arrival = arrival[stop_index] + walk_sec;
            finish_score = transit_finish_score(arrival[stop_index], walk_sec);
            if (finish_score < best_score || (finish_score == best_score && finish_arrival < best_arrival)) {
                best_stop_index = stop_index;
                best_arrival = finish_arrival;
                best_score = finish_score;
                best_egress_m = walk_m;
            }
        }
    }
    if (best_stop_index == RTE_TRANSIT_NO_INDEX) return 0;

    {
        RteTransitPlanLeg reverse_legs[RTE_TRANSIT_MAX_PLAN_LEGS];
        unsigned int reverse_count = 0U;
        unsigned int current_stop = best_stop_index;
        unsigned int guard = 0U;
        unsigned int access_stop_index = best_stop_index;
        while (current_stop != RTE_TRANSIT_NO_INDEX && current_stop < store->stop_count && guard < store->stop_count) {
            const RteTransitStopState *state = &states[current_stop];
            guard += 1U;
            if (state->kind == RTE_TRANSIT_STATE_VEHICLE) {
                if (reverse_count >= RTE_TRANSIT_MAX_PLAN_LEGS) return 0;
                reverse_legs[reverse_count].kind = RTE_TRANSIT_STATE_VEHICLE;
                reverse_legs[reverse_count].mode = state->mode;
                reverse_legs[reverse_count].route_index = state->route_index;
                reverse_legs[reverse_count].board_stop_index = state->board_stop_index;
                reverse_legs[reverse_count].alight_stop_index = state->alight_stop_index;
                reverse_legs[reverse_count].departure_sec = state->departure_sec;
                reverse_legs[reverse_count].arrival_sec = state->arrival_sec;
                reverse_legs[reverse_count].walk_m = 0U;
                reverse_count += 1U;
                access_stop_index = state->board_stop_index;
                current_stop = state->previous_stop_index;
            } else if (state->kind == RTE_TRANSIT_STATE_TRANSFER_WALK) {
                if (reverse_count >= RTE_TRANSIT_MAX_PLAN_LEGS) return 0;
                reverse_legs[reverse_count].kind = RTE_TRANSIT_STATE_TRANSFER_WALK;
                reverse_legs[reverse_count].mode = 0U;
                reverse_legs[reverse_count].route_index = RTE_TRANSIT_NO_INDEX;
                reverse_legs[reverse_count].board_stop_index = state->board_stop_index;
                reverse_legs[reverse_count].alight_stop_index = state->alight_stop_index;
                reverse_legs[reverse_count].departure_sec = state->departure_sec;
                reverse_legs[reverse_count].arrival_sec = state->arrival_sec;
                reverse_legs[reverse_count].walk_m = state->walk_m;
                reverse_count += 1U;
                access_stop_index = state->previous_stop_index;
                current_stop = state->previous_stop_index;
            } else if (state->kind == RTE_TRANSIT_STATE_ORIGIN_WALK) {
                access_stop_index = current_stop;
                break;
            } else {
                return 0;
            }
        }
        if (reverse_count == 0U || access_stop_index >= store->stop_count) return 0;
        plan->found = 1;
        plan->origin_departure_sec = output->depart_seconds;
        plan->total_sec = best_arrival - output->depart_seconds;
        plan->walk_to_stop_m = direct_distance_m(from->record.lat_e7, from->record.lon_e7, store->stops[access_stop_index].lat_e7, store->stops[access_stop_index].lon_e7);
        plan->walk_from_stop_m = best_egress_m;
        transit_reverse_plan_legs(plan, reverse_legs, reverse_count);
    }
    return 0;
}

static int evaluate_transit_plan(const RteTransitStore *store, const RteResolvedAddress *from, const RteResolvedAddress *to, const RteOutput *output, RteTransitPlan *plan) {
    unsigned int *arrival;
    unsigned int *base_arrival;
    unsigned int *changed_stops;
    unsigned char *rides_used;
    unsigned char *base_rides_used;
    unsigned char *changed_queued;
    unsigned char *trip_active;
    RteTransitStopState *states;
    RteTransitStopGrid grid;
    unsigned int index;
    unsigned int round;
    unsigned int active_trip_count = 0U;

    rt_memset(plan, 0, sizeof(*plan));
    rt_memset(&grid, 0, sizeof(grid));
    if ((!output->have_depart && !output->have_arrive) || store->stop_count == 0U || store->event_count == 0U) return 0;
    if (output->have_arrive) {
        unsigned int event_index;
        unsigned int current_trip = RTE_TRANSIT_NO_INDEX;
        int trip_active = 0;
        int board_found = 0;
        unsigned int board_departure = 0U;
        unsigned int board_sequence = 0U;
        unsigned int board_stop_index = 0U;

        for (event_index = 0U; event_index < store->event_count; ++event_index) {
            const RteTransitEvent *event = &store->events[event_index];
            const RteTransitTrip *trip;
            const RteTransitStop *stop;
            unsigned int walk_from_m;
            unsigned int walk_to_m;
            plan->events_scanned += 1ULL;
            if (event->trip_index >= store->trip_count || event->stop_index >= store->stop_count) continue;
            if (event->trip_index != current_trip) {
                current_trip = event->trip_index;
                trip = &store->trips[current_trip];
                trip_active = transit_service_active(store, trip->service_index, output->arrive_date);
                if (trip_active) plan->active_trips += 1U;
                board_found = 0;
                board_departure = 0U;
                board_sequence = 0U;
                board_stop_index = 0U;
            }
            if (!trip_active) continue;
            stop = &store->stops[event->stop_index];
            walk_from_m = direct_distance_m(from->record.lat_e7, from->record.lon_e7, stop->lat_e7, stop->lon_e7);
            walk_to_m = direct_distance_m(to->record.lat_e7, to->record.lon_e7, stop->lat_e7, stop->lon_e7);
            if (walk_from_m <= RTE_TRANSIT_ACCESS_WALK_M) plan->candidate_origin_stops += 1U;
            if (walk_to_m <= RTE_TRANSIT_EGRESS_WALK_M) plan->candidate_destination_stops += 1U;
            if (walk_from_m <= RTE_TRANSIT_ACCESS_WALK_M && event->departure_sec >= walking_seconds_for_meters(walk_from_m)) {
                board_found = 1;
                board_departure = event->departure_sec;
                board_sequence = event->sequence;
                board_stop_index = event->stop_index;
                plan->board_candidates += 1ULL;
            }
            if (board_found && walk_to_m <= RTE_TRANSIT_EGRESS_WALK_M && event->sequence > board_sequence && event->arrival_sec >= board_departure && event->arrival_sec + walking_seconds_for_meters(walk_to_m) <= output->arrive_seconds) {
                unsigned int origin_walk_m = direct_distance_m(from->record.lat_e7, from->record.lon_e7, store->stops[board_stop_index].lat_e7, store->stops[board_stop_index].lon_e7);
                unsigned int origin_departure = board_departure - walking_seconds_for_meters(origin_walk_m);
                if (!plan->found || origin_departure > plan->origin_departure_sec) {
                    trip = &store->trips[current_trip];
                    plan->found = 1;
                    plan->mode = trip->mode;
                    plan->route_index = trip->route_index;
                    plan->board_stop_index = board_stop_index;
                    plan->alight_stop_index = event->stop_index;
                    plan->board_departure_sec = board_departure;
                    plan->alight_arrival_sec = event->arrival_sec;
                    plan->walk_to_stop_m = origin_walk_m;
                    plan->walk_from_stop_m = walk_to_m;
                    plan->origin_departure_sec = origin_departure;
                    plan->total_sec = output->arrive_seconds - origin_departure;
                    plan->transit_leg_count = 1U;
                    plan->leg_count = 1U;
                    plan->legs[0].kind = RTE_TRANSIT_STATE_VEHICLE;
                    plan->legs[0].mode = trip->mode;
                    plan->legs[0].route_index = trip->route_index;
                    plan->legs[0].board_stop_index = board_stop_index;
                    plan->legs[0].alight_stop_index = event->stop_index;
                    plan->legs[0].departure_sec = board_departure;
                    plan->legs[0].arrival_sec = event->arrival_sec;
                    plan->legs[0].walk_m = 0U;
                }
            }
        }
        return 0;
    }

    arrival = (unsigned int *)rt_malloc(sizeof(*arrival) * (size_t)store->stop_count);
    base_arrival = (unsigned int *)rt_malloc(sizeof(*base_arrival) * (size_t)store->stop_count);
    changed_stops = (unsigned int *)rt_malloc(sizeof(*changed_stops) * (size_t)store->stop_count);
    rides_used = (unsigned char *)rt_malloc(sizeof(*rides_used) * (size_t)store->stop_count);
    base_rides_used = (unsigned char *)rt_malloc(sizeof(*base_rides_used) * (size_t)store->stop_count);
    changed_queued = (unsigned char *)rt_malloc(sizeof(*changed_queued) * (size_t)store->stop_count);
    trip_active = (unsigned char *)rt_malloc(sizeof(*trip_active) * (size_t)store->trip_count);
    states = (RteTransitStopState *)rt_malloc(sizeof(*states) * (size_t)store->stop_count);
    if (arrival == 0 || base_arrival == 0 || changed_stops == 0 || rides_used == 0 || base_rides_used == 0 || changed_queued == 0 || trip_active == 0 || states == 0 || transit_stop_grid_build(store, &grid) != 0) {
        rt_free(arrival); rt_free(base_arrival); rt_free(changed_stops); rt_free(rides_used); rt_free(base_rides_used); rt_free(changed_queued); rt_free(trip_active); rt_free(states); transit_stop_grid_destroy(&grid);
        return -1;
    }

    for (index = 0U; index < store->stop_count; ++index) {
        unsigned int walk_m = direct_distance_m(from->record.lat_e7, from->record.lon_e7, store->stops[index].lat_e7, store->stops[index].lon_e7);
        arrival[index] = RTE_TRANSIT_INF_TIME;
        rides_used[index] = 0U;
        changed_queued[index] = 0U;
        rt_memset(&states[index], 0, sizeof(states[index]));
        states[index].previous_stop_index = RTE_TRANSIT_NO_INDEX;
        states[index].trip_index = RTE_TRANSIT_NO_INDEX;
        states[index].route_index = RTE_TRANSIT_NO_INDEX;
        if (walk_m <= RTE_TRANSIT_ACCESS_WALK_M) {
            unsigned int walk_sec = walking_seconds_for_meters(walk_m);
            if (output->depart_seconds <= RTE_TRANSIT_INF_TIME - walk_sec) {
                arrival[index] = output->depart_seconds + walk_sec;
                states[index].kind = RTE_TRANSIT_STATE_ORIGIN_WALK;
                states[index].previous_stop_index = RTE_TRANSIT_NO_INDEX;
                states[index].board_stop_index = index;
                states[index].alight_stop_index = index;
                states[index].departure_sec = output->depart_seconds;
                states[index].arrival_sec = arrival[index];
                states[index].walk_m = walk_m;
                plan->candidate_origin_stops += 1U;
            }
        }
    }
    for (index = 0U; index < store->trip_count; ++index) {
        trip_active[index] = transit_service_active(store, store->trips[index].service_index, output->depart_date) ? 1U : 0U;
        if (trip_active[index]) active_trip_count += 1U;
    }
    plan->active_trips = active_trip_count;

    for (round = 0U; round < RTE_TRANSIT_MAX_ROUNDS; ++round) {
        unsigned int event_index;
        unsigned int changed_count = 0U;
        unsigned int current_trip = RTE_TRANSIT_NO_INDEX;
        unsigned int board_stop_index = RTE_TRANSIT_NO_INDEX;
        unsigned int board_departure = 0U;
        unsigned int board_sequence = 0U;
        unsigned char board_rides = 0U;
        int current_trip_active = 0;

        memcpy(base_arrival, arrival, sizeof(*arrival) * (size_t)store->stop_count);
        memcpy(base_rides_used, rides_used, sizeof(*rides_used) * (size_t)store->stop_count);

        for (event_index = 0U; event_index < store->event_count; ++event_index) {
            const RteTransitEvent *event = &store->events[event_index];
            const RteTransitTrip *trip;
            plan->events_scanned += 1ULL;
            if (event->trip_index >= store->trip_count || event->stop_index >= store->stop_count) continue;
            if (event->trip_index != current_trip) {
                current_trip = event->trip_index;
                board_stop_index = RTE_TRANSIT_NO_INDEX;
                board_departure = 0U;
                board_sequence = 0U;
                board_rides = 0U;
                current_trip_active = trip_active[current_trip] != 0U;
            }
            if (!current_trip_active) continue;
            trip = &store->trips[current_trip];
            if (board_stop_index != RTE_TRANSIT_NO_INDEX && event->sequence > board_sequence && event->arrival_sec >= board_departure && event->arrival_sec < arrival[event->stop_index]) {
                arrival[event->stop_index] = event->arrival_sec;
                rides_used[event->stop_index] = board_rides;
                states[event->stop_index].kind = RTE_TRANSIT_STATE_VEHICLE;
                states[event->stop_index].previous_stop_index = board_stop_index;
                states[event->stop_index].trip_index = current_trip;
                states[event->stop_index].route_index = trip->route_index;
                states[event->stop_index].mode = trip->mode;
                states[event->stop_index].board_stop_index = board_stop_index;
                states[event->stop_index].alight_stop_index = event->stop_index;
                states[event->stop_index].departure_sec = board_departure;
                states[event->stop_index].arrival_sec = event->arrival_sec;
                states[event->stop_index].walk_m = 0U;
                if (transit_push_stop(changed_stops, changed_queued, &changed_count, store->stop_count, event->stop_index) != 0) break;
            }
            if (base_arrival[event->stop_index] != RTE_TRANSIT_INF_TIME && base_arrival[event->stop_index] <= event->departure_sec && base_rides_used[event->stop_index] < RTE_TRANSIT_MAX_ROUNDS) {
                unsigned char candidate_rides = (unsigned char)(base_rides_used[event->stop_index] + 1U);
                if (candidate_rides == round + 1U && (board_stop_index == RTE_TRANSIT_NO_INDEX || event->sequence < board_sequence)) {
                    board_stop_index = event->stop_index;
                    board_departure = event->departure_sec;
                    board_sequence = event->sequence;
                    board_rides = candidate_rides;
                    plan->board_candidates += 1ULL;
                }
            }
        }
        if (changed_count == 0U) break;
        (void)transit_apply_transfer_walks(store, &grid, arrival, rides_used, states, changed_stops, changed_count);
        for (index = 0U; index < changed_count; ++index) changed_queued[changed_stops[index]] = 0U;
    }

    (void)transit_build_plan_from_arrivals(store, from, to, output, arrival, rides_used, states, plan);
    rt_free(arrival);
    rt_free(base_arrival);
    rt_free(changed_stops);
    rt_free(rides_used);
    rt_free(base_rides_used);
    rt_free(changed_queued);
    rt_free(trip_active);
    rt_free(states);
    transit_stop_grid_destroy(&grid);
    return 0;
}

static unsigned int hash_coord_pair(int lat_e7, int lon_e7) {
    unsigned long long x = (unsigned long long)(unsigned int)lat_e7 ^ (((unsigned long long)(unsigned int)lon_e7) << 32U);
    x ^= x >> 33U;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33U;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33U;
    return (unsigned int)x;
}

static int graph_rehash(RteGraph *graph, unsigned int new_slot_capacity) {
    RteGraphNodeSlot *new_slots = (RteGraphNodeSlot *)rt_malloc(sizeof(*new_slots) * new_slot_capacity);
    unsigned int index;
    if (new_slots == 0) return -1;
    rt_memset(new_slots, 0, sizeof(*new_slots) * new_slot_capacity);
    for (index = 0U; index < graph->node_count; ++index) {
        unsigned int slot = hash_coord_pair(graph->nodes[index].lat_e7, graph->nodes[index].lon_e7) & (new_slot_capacity - 1U);
        while (new_slots[slot].used) slot = (slot + 1U) & (new_slot_capacity - 1U);
        new_slots[slot].used = 1;
        new_slots[slot].lat_e7 = graph->nodes[index].lat_e7;
        new_slots[slot].lon_e7 = graph->nodes[index].lon_e7;
        new_slots[slot].index = index;
    }
    rt_free(graph->slots);
    graph->slots = new_slots;
    graph->slot_capacity = new_slot_capacity;
    return 0;
}

static int graph_reserve_nodes(RteGraph *graph, unsigned int needed_count) {
    unsigned int new_capacity;
    RteGraphNode *new_nodes;
    if (needed_count <= graph->node_capacity) return 0;
    new_capacity = graph->node_capacity == 0U ? 4096U : graph->node_capacity * 2U;
    while (new_capacity < needed_count) new_capacity *= 2U;
    new_nodes = (RteGraphNode *)rt_realloc(graph->nodes, sizeof(*new_nodes) * new_capacity);
    if (new_nodes == 0) return -1;
    graph->nodes = new_nodes;
    graph->node_capacity = new_capacity;
    return 0;
}

static int graph_reserve_edges(RteGraph *graph, unsigned int needed_count) {
    unsigned int new_capacity;
    RteGraphEdge *new_edges;
    if (needed_count <= graph->edge_capacity) return 0;
    new_capacity = graph->edge_capacity == 0U ? 8192U : graph->edge_capacity * 2U;
    while (new_capacity < needed_count) new_capacity *= 2U;
    new_edges = (RteGraphEdge *)rt_realloc(graph->edges, sizeof(*new_edges) * new_capacity);
    if (new_edges == 0) return -1;
    graph->edges = new_edges;
    graph->edge_capacity = new_capacity;
    return 0;
}

static int graph_node_for_coord(RteGraph *graph, int lat_e7, int lon_e7, unsigned int *node_out) {
    unsigned int slot;
    if (graph->slot_capacity == 0U) { if (graph_rehash(graph, 8192U) != 0) return -1; }
    else if ((graph->node_count + 1U) * 2U >= graph->slot_capacity) { if (graph_rehash(graph, graph->slot_capacity * 2U) != 0) return -1; }
    slot = hash_coord_pair(lat_e7, lon_e7) & (graph->slot_capacity - 1U);
    while (graph->slots[slot].used) {
        if (graph->slots[slot].lat_e7 == lat_e7 && graph->slots[slot].lon_e7 == lon_e7) { *node_out = graph->slots[slot].index; return 0; }
        slot = (slot + 1U) & (graph->slot_capacity - 1U);
    }
    if (graph_reserve_nodes(graph, graph->node_count + 1U) != 0) return -1;
    graph->slots[slot].used = 1;
    graph->slots[slot].lat_e7 = lat_e7;
    graph->slots[slot].lon_e7 = lon_e7;
    graph->slots[slot].index = graph->node_count;
    graph->nodes[graph->node_count].lat_e7 = lat_e7;
    graph->nodes[graph->node_count].lon_e7 = lon_e7;
    graph->nodes[graph->node_count].first_edge = 0xffffffffU;
    graph->nodes[graph->node_count].distance = 0xffffffffffffffffULL;
    graph->nodes[graph->node_count].previous = 0xffffffffU;
    graph->nodes[graph->node_count].previous_edge_meters = 0U;
    graph->nodes[graph->node_count].settled = 0U;
    *node_out = graph->node_count;
    graph->node_count += 1U;
    return 0;
}

static int graph_add_edge(RteGraph *graph, unsigned int from, unsigned int to, unsigned int meters) {
    RteGraphEdge *edge;
    if (from == to) return 0;
    if (graph_reserve_edges(graph, graph->edge_count + 1U) != 0) return -1;
    edge = graph->edges + graph->edge_count;
    edge->to = to;
    edge->meters = meters == 0U ? 1U : meters;
    edge->next = graph->nodes[from].first_edge;
    graph->nodes[from].first_edge = graph->edge_count;
    graph->edge_count += 1U;
    return 0;
}

static int load_tile_payload_graph(int fd, const RteTileRecord *tile, RteGraph *graph) {
    unsigned char header[RTE_TILE_PAYLOAD_HEADER_SIZE + 4U * RTE_TILE_PAYLOAD_DIRECTORY_RECORD_SIZE];
    unsigned long long node_offset = 0ULL;
    unsigned long long offset_offset = 0ULL;
    unsigned long long edge_offset = 0ULL;
    unsigned int node_count = 0U;
    unsigned int offset_count = 0U;
    unsigned int edge_count = 0U;
    unsigned char *node_bytes;
    unsigned char *offset_bytes;
    unsigned char *edge_bytes;
    unsigned int *local_to_global;
    unsigned int index;

    if (tile->payload_size < sizeof(header) || tile->local_node_count == 0U || tile->local_directed_edge_count == 0U) return 0;
    if (read_at(fd, tile->payload_offset, header, sizeof(header)) != 0) return -1;
    for (index = 0U; index < 4U; ++index) {
        unsigned char *entry = header + RTE_TILE_PAYLOAD_HEADER_SIZE + index * RTE_TILE_PAYLOAD_DIRECTORY_RECORD_SIZE;
        unsigned int type = read_u32_le(entry + 0U);
        unsigned long long relative_offset = read_u64_le(entry + 8U);
        unsigned long long size = read_u64_le(entry + 16U);
        unsigned int record_count = read_u32_le(entry + 24U);
        if (relative_offset > tile->payload_size || size > tile->payload_size - relative_offset) return -1;
        if (type == RTE_TILE_TYPE_WALKING_NODES) { node_offset = relative_offset; node_count = record_count; }
        else if (type == RTE_TILE_TYPE_WALKING_OFFSETS) { offset_offset = relative_offset; offset_count = record_count; }
        else if (type == RTE_TILE_TYPE_WALKING_EDGES) { edge_offset = relative_offset; edge_count = record_count; }
    }
    if (node_count == 0U || edge_count == 0U || offset_count != node_count + 1U) return 0;
    node_bytes = (unsigned char *)rt_malloc((size_t)node_count * 16U);
    offset_bytes = (unsigned char *)rt_malloc((size_t)offset_count * 4U);
    edge_bytes = (unsigned char *)rt_malloc((size_t)edge_count * 20U);
    local_to_global = (unsigned int *)rt_malloc(sizeof(unsigned int) * node_count);
    if (node_bytes == 0 || offset_bytes == 0 || edge_bytes == 0 || local_to_global == 0) return -1;
    if (read_at(fd, tile->payload_offset + node_offset, node_bytes, (size_t)node_count * 16U) != 0 ||
        read_at(fd, tile->payload_offset + offset_offset, offset_bytes, (size_t)offset_count * 4U) != 0 ||
        read_at(fd, tile->payload_offset + edge_offset, edge_bytes, (size_t)edge_count * 20U) != 0) return -1;
    for (index = 0U; index < node_count; ++index) {
        int lat_e7 = read_i32_le(node_bytes + (size_t)index * 16U + 0U);
        int lon_e7 = read_i32_le(node_bytes + (size_t)index * 16U + 4U);
        if (graph_node_for_coord(graph, lat_e7, lon_e7, local_to_global + index) != 0) return -1;
    }
    for (index = 0U; index < node_count; ++index) {
        unsigned int begin = read_u32_le(offset_bytes + (size_t)index * 4U);
        unsigned int end = read_u32_le(offset_bytes + (size_t)(index + 1U) * 4U);
        unsigned int edge_index;
        if (begin > end || end > edge_count) return -1;
        for (edge_index = begin; edge_index < end; ++edge_index) {
            unsigned int to_local = read_u32_le(edge_bytes + (size_t)edge_index * 20U + 0U);
            unsigned int meters = read_u32_le(edge_bytes + (size_t)edge_index * 20U + 8U);
            if (to_local < node_count && graph_add_edge(graph, local_to_global[index], local_to_global[to_local], meters) != 0) return -1;
        }
    }
    rt_free(node_bytes);
    rt_free(offset_bytes);
    rt_free(edge_bytes);
    rt_free(local_to_global);
    return 0;
}

static int load_neighborhood_graph(int fd, const RteHeader *header, int from_x, int from_y, int to_x, int to_y, RteGraph *graph, unsigned int *tiles_loaded) {
    int min_x = from_x < to_x ? from_x : to_x;
    int max_x = from_x > to_x ? from_x : to_x;
    int min_y = from_y < to_y ? from_y : to_y;
    int max_y = from_y > to_y ? from_y : to_y;
    int x;
    int y;
    *tiles_loaded = 0U;
    min_x -= 1; max_x += 1; min_y -= 1; max_y += 1;
    for (y = min_y; y <= max_y; ++y) {
        for (x = min_x; x <= max_x; ++x) {
            RteTileRecord tile;
            unsigned long long tile_id = route_tile_id(x, y);
            int found = find_tile_by_id(fd, header, tile_id, &tile);
            if (found < 0) return -1;
            if (found > 0) {
                if (load_tile_payload_graph(fd, &tile, graph) != 0) return -1;
                *tiles_loaded += 1U;
            }
        }
    }
    return 0;
}

static unsigned int nearest_graph_node(const RteGraph *graph, int lat_e7, int lon_e7, unsigned int *meters_out) {
    unsigned int best = 0xffffffffU;
    unsigned int best_meters = 0xffffffffU;
    unsigned int index;
    for (index = 0U; index < graph->node_count; ++index) {
        unsigned int meters = direct_distance_m(lat_e7, lon_e7, graph->nodes[index].lat_e7, graph->nodes[index].lon_e7);
        if (best == 0xffffffffU || meters < best_meters) { best = index; best_meters = meters; }
    }
    *meters_out = best_meters;
    return best;
}

static int heap_push(RteHeap *heap, unsigned int node, unsigned long long distance) {
    unsigned int index;
    if (heap->count == heap->capacity) {
        unsigned int new_capacity = heap->capacity == 0U ? 1024U : heap->capacity * 2U;
        RteHeapItem *new_items = (RteHeapItem *)rt_realloc(heap->items, sizeof(*new_items) * new_capacity);
        if (new_items == 0) return -1;
        heap->items = new_items;
        heap->capacity = new_capacity;
    }
    index = heap->count++;
    while (index > 0U) {
        unsigned int parent = (index - 1U) / 2U;
        if (heap->items[parent].distance <= distance) break;
        heap->items[index] = heap->items[parent];
        index = parent;
    }
    heap->items[index].node = node;
    heap->items[index].distance = distance;
    return 0;
}

static int heap_pop(RteHeap *heap, RteHeapItem *out) {
    RteHeapItem item;
    unsigned int index = 0U;
    if (heap->count == 0U) return 0;
    *out = heap->items[0];
    item = heap->items[--heap->count];
    while (index * 2U + 1U < heap->count) {
        unsigned int child = index * 2U + 1U;
        if (child + 1U < heap->count && heap->items[child + 1U].distance < heap->items[child].distance) child += 1U;
        if (heap->items[child].distance >= item.distance) break;
        heap->items[index] = heap->items[child];
        index = child;
    }
    if (heap->count != 0U) heap->items[index] = item;
    return 1;
}

static int dijkstra(RteGraph *graph, unsigned int source, unsigned int target, unsigned long long *distance_out) {
    RteHeap heap;
    RteHeapItem item;
    unsigned int index;
    rt_memset(&heap, 0, sizeof(heap));
    for (index = 0U; index < graph->node_count; ++index) {
        graph->nodes[index].distance = 0xffffffffffffffffULL;
        graph->nodes[index].previous = 0xffffffffU;
        graph->nodes[index].previous_edge_meters = 0U;
        graph->nodes[index].settled = 0U;
    }
    graph->nodes[source].distance = 0ULL;
    if (heap_push(&heap, source, 0ULL) != 0) return -1;
    while (heap_pop(&heap, &item)) {
        RteGraphNode *node;
        unsigned int edge_index;
        if (item.node >= graph->node_count) continue;
        node = graph->nodes + item.node;
        if (node->settled) continue;
        node->settled = 1U;
        if (item.node == target) { *distance_out = node->distance; rt_free(heap.items); return 1; }
        for (edge_index = node->first_edge; edge_index != 0xffffffffU; edge_index = graph->edges[edge_index].next) {
            RteGraphEdge *edge = graph->edges + edge_index;
            unsigned long long next_distance = node->distance + (unsigned long long)edge->meters;
            if (next_distance < graph->nodes[edge->to].distance) {
                graph->nodes[edge->to].distance = next_distance;
                graph->nodes[edge->to].previous = item.node;
                graph->nodes[edge->to].previous_edge_meters = edge->meters;
                if (heap_push(&heap, edge->to, next_distance) != 0) { rt_free(heap.items); return -1; }
            }
        }
    }
    rt_free(heap.items);
    return 0;
}

static int build_route_path(const RteGraph *graph, unsigned int source, unsigned int target, unsigned int **path_out, unsigned int *path_count_out) {
    unsigned int path_count = 0U;
    unsigned int cursor;
    unsigned int index;
    unsigned int *path;

    cursor = target;
    while (cursor != 0xffffffffU) {
        path_count += 1U;
        if (cursor == source) break;
        cursor = graph->nodes[cursor].previous;
    }
    if (path_count == 0U || cursor != source) return -1;
    path = (unsigned int *)rt_malloc(sizeof(unsigned int) * path_count);
    if (path == 0) return -1;
    cursor = target;
    for (index = 0U; index < path_count; ++index) {
        path[path_count - 1U - index] = cursor;
        if (cursor == source) break;
        cursor = graph->nodes[cursor].previous;
    }
    *path_out = path;
    *path_count_out = path_count;
    return 0;
}

static void write_address_inline(const char *strings, unsigned int strings_size, const RteAddressRecord *record) {
    if (address_field_valid(record->street_offset, record->street_size, strings_size)) (void)rt_write_all(1, strings + record->street_offset, record->street_size);
    if (address_field_valid(record->housenumber_offset, record->housenumber_size, strings_size) && record->housenumber_size != 0U) {
        rt_write_char(1, ' ');
        (void)rt_write_all(1, strings + record->housenumber_offset, record->housenumber_size);
    }
    if (address_field_valid(record->city_offset, record->city_size, strings_size) && record->city_size != 0U) {
        rt_write_cstr(1, ", ");
        (void)rt_write_all(1, strings + record->city_offset, record->city_size);
    }
}

static const char *compass_direction(int from_lat_e7, int from_lon_e7, int to_lat_e7, int to_lon_e7) {
    long long dlat = (long long)to_lat_e7 - (long long)from_lat_e7;
    long long dlon = (long long)to_lon_e7 - (long long)from_lon_e7;
    unsigned long long ax = dlon < 0 ? (unsigned long long)(-dlon) : (unsigned long long)dlon;
    unsigned long long ay = dlat < 0 ? (unsigned long long)(-dlat) : (unsigned long long)dlat;
    const char *north_south = dlat >= 0 ? "north" : "south";
    const char *east_west = dlon >= 0 ? "east" : "west";

    if (ax * 2ULL < ay) return north_south;
    if (ay * 2ULL < ax) return east_west;
    if (dlat >= 0 && dlon >= 0) return "north-east";
    if (dlat >= 0 && dlon < 0) return "north-west";
    if (dlat < 0 && dlon >= 0) return "south-east";
    return "south-west";
}

static void write_distance_human(unsigned long long meters) {
    if (meters < 1000ULL) {
        rt_write_uint(1, meters);
        rt_write_cstr(1, " m");
    } else {
        unsigned long long whole = meters / 1000ULL;
        unsigned long long decimal = (meters % 1000ULL + 50ULL) / 100ULL;
        if (decimal == 10ULL) { whole += 1ULL; decimal = 0ULL; }
        rt_write_uint(1, whole);
        rt_write_char(1, '.');
        rt_write_uint(1, decimal);
        rt_write_cstr(1, " km");
    }
}

static void write_step_prefix(const RteOutput *output, unsigned int *step) {
    write_color(output, "\033[1;34m");
    rt_write_uint(1, *step);
    rt_write_cstr(1, ". ");
    write_color_reset(output);
    *step += 1U;
}

static int write_human_route(
    const RteOutput *output,
    const RteGraph *graph,
    unsigned int source,
    unsigned int target,
    const RteResolvedAddress *from,
    const RteResolvedAddress *to,
    const char *strings,
    unsigned int strings_size,
    unsigned int from_snap_m,
    unsigned int to_snap_m,
    unsigned long long graph_distance_m,
    unsigned long long total_distance_m,
    unsigned int tiles_loaded
) {
    unsigned int path_count;
    unsigned int *path;
    unsigned int index;
    unsigned int step = 1U;
    unsigned long long minutes = (total_distance_m * 3ULL + 249ULL) / 250ULL;

    if (build_route_path(graph, source, target, &path, &path_count) != 0) return -1;

    rt_write_char(1, '\n');
    write_colored_cstr(output, "\033[1;35m", "Walking route");
    rt_write_char(1, '\n');
    write_colored_cstr(output, "\033[1;32m", "Found a walking route");
    rt_write_cstr(1, " from ");
    write_colored_cstr(output, "\033[1;36m", "");
    write_color(output, "\033[1;36m");
    write_address_inline(strings, strings_size, &from->record);
    write_color_reset(output);
    rt_write_cstr(1, " to ");
    write_color(output, "\033[1;36m");
    write_address_inline(strings, strings_size, &to->record);
    write_color_reset(output);
    rt_write_cstr(1, ".\n");
    rt_write_cstr(1, "Distance: ");
    write_colored_uint(output, "\033[1;33m", total_distance_m);
    rt_write_cstr(1, " m (");
    write_distance_human(total_distance_m);
    rt_write_cstr(1, "), about ");
    write_colored_uint(output, "\033[1;33m", minutes);
    rt_write_cstr(1, " min on foot.\n");
    rt_write_cstr(1, "Loaded ");
    write_colored_uint(output, "\033[36m", tiles_loaded);
    rt_write_cstr(1, " tiles and ");
    write_colored_uint(output, "\033[36m", graph->node_count);
    rt_write_cstr(1, " route nodes; endpoint snaps are ");
    write_colored_uint(output, "\033[36m", from_snap_m);
    rt_write_cstr(1, " m and ");
    write_colored_uint(output, "\033[36m", to_snap_m);
    rt_write_cstr(1, " m.\n\n");

    write_colored_cstr(output, "\033[1;35m", "Directions");
    rt_write_char(1, '\n');
    write_step_prefix(output, &step);
    rt_write_cstr(1, "Start at ");
    write_color(output, "\033[1;36m");
    write_address_inline(strings, strings_size, &from->record);
    write_color_reset(output);
    rt_write_cstr(1, ".\n");
    if (from_snap_m != 0U) {
        write_step_prefix(output, &step);
        rt_write_cstr(1, "Walk about ");
        write_distance_human(from_snap_m);
        rt_write_cstr(1, " to join the route graph.\n");
    }
    if (path_count > 1U) {
        unsigned int group_start = 0U;
        unsigned long long group_meters = 0ULL;
        for (index = 1U; index < path_count; ++index) {
            group_meters += graph->nodes[path[index]].previous_edge_meters;
            if (group_meters >= 450ULL || index + 1U == path_count) {
                const char *direction = compass_direction(graph->nodes[path[group_start]].lat_e7, graph->nodes[path[group_start]].lon_e7, graph->nodes[path[index]].lat_e7, graph->nodes[path[index]].lon_e7);
                write_step_prefix(output, &step);
                rt_write_cstr(1, "Continue generally ");
                write_colored_cstr(output, "\033[36m", direction);
                rt_write_cstr(1, " for ");
                write_distance_human(group_meters);
                rt_write_cstr(1, ".\n");
                group_start = index;
                group_meters = 0ULL;
            }
        }
    }
    if (to_snap_m != 0U) {
        write_step_prefix(output, &step);
        rt_write_cstr(1, "Leave the route graph and walk about ");
        write_distance_human(to_snap_m);
        rt_write_cstr(1, " to the destination.\n");
    }
    write_step_prefix(output, &step);
    rt_write_cstr(1, "Arrive at ");
    write_color(output, "\033[1;36m");
    write_address_inline(strings, strings_size, &to->record);
    write_color_reset(output);
    rt_write_cstr(1, ".\n");
    rt_write_cstr(1, "\nGraph distance: ");
    write_distance_human(graph_distance_m);
    rt_write_cstr(1, ".\n");

    rt_free(path);
    return 0;
}

static void write_transit_plan_text(const RteOutput *output, const RteTransitStore *store, const RteTransitPlan *plan, unsigned long long walking_total_m) {
    unsigned long long walking_sec = walking_seconds_for_meters((unsigned int)walking_total_m);
    unsigned int leg_index;
    unsigned int step = 1U;
    if (!plan->found || plan->board_stop_index >= store->stop_count || plan->alight_stop_index >= store->stop_count || plan->route_index >= store->route_count) return;
    if (!output->transit_only || output->verbose) rt_write_char(1, '\n');
    write_colored_cstr(output, "\033[1;35m", "Transit option");
    rt_write_char(1, '\n');
    rt_write_uint(1, step++);
    rt_write_cstr(1, ". Walk ");
    write_distance_human(plan->walk_to_stop_m);
    rt_write_cstr(1, " to ");
    write_color(output, "\033[1;36m");
    write_transit_string(store, store->stops[plan->board_stop_index].name_offset, store->stops[plan->board_stop_index].name_size);
    write_color_reset(output);
    rt_write_cstr(1, ".\n");
    for (leg_index = 0U; leg_index < plan->leg_count; ++leg_index) {
        const RteTransitPlanLeg *leg = &plan->legs[leg_index];
        if (leg->kind == RTE_TRANSIT_STATE_VEHICLE && leg->board_stop_index < store->stop_count && leg->alight_stop_index < store->stop_count && leg->route_index < store->route_count) {
            const RteTransitRoute *route = &store->routes[leg->route_index];
            rt_write_uint(1, step++);
            rt_write_cstr(1, ". Take the ");
            write_colored_cstr(output, "\033[1;33m", transit_mode_name(leg->mode));
            rt_write_char(1, ' ');
            write_color(output, "\033[1;33m");
            write_transit_string(store, route->short_name_offset, route->short_name_size);
            write_color_reset(output);
            rt_write_cstr(1, " from ");
            write_color(output, "\033[1;36m");
            write_transit_string(store, store->stops[leg->board_stop_index].name_offset, store->stops[leg->board_stop_index].name_size);
            write_color_reset(output);
            rt_write_cstr(1, " at ");
            write_hhmm(leg->departure_sec);
            rt_write_cstr(1, " to ");
            write_color(output, "\033[1;36m");
            write_transit_string(store, store->stops[leg->alight_stop_index].name_offset, store->stops[leg->alight_stop_index].name_size);
            write_color_reset(output);
            rt_write_cstr(1, ", arriving ");
            write_hhmm(leg->arrival_sec);
            rt_write_cstr(1, ".\n");
        } else if (leg->kind == RTE_TRANSIT_STATE_TRANSFER_WALK && leg->alight_stop_index < store->stop_count) {
            rt_write_uint(1, step++);
            rt_write_cstr(1, ". Walk ");
            write_distance_human(leg->walk_m);
            rt_write_cstr(1, " to ");
            write_color(output, "\033[1;36m");
            write_transit_string(store, store->stops[leg->alight_stop_index].name_offset, store->stops[leg->alight_stop_index].name_size);
            write_color_reset(output);
            rt_write_cstr(1, ".\n");
        }
    }
    rt_write_uint(1, step++);
    rt_write_cstr(1, ". Walk ");
    write_distance_human(plan->walk_from_stop_m);
    rt_write_cstr(1, " to the destination.\n");
    rt_write_cstr(1, "Total: about ");
    write_colored_uint(output, "\033[1;33m", (plan->total_sec + 59U) / 60U);
    rt_write_cstr(1, " min.");
    if (walking_sec > plan->total_sec + 60U) {
        rt_write_cstr(1, " Faster than walking by about ");
        rt_write_uint(1, (walking_sec - plan->total_sec + 59ULL) / 60ULL);
        rt_write_cstr(1, " min.");
    }
    rt_write_char(1, '\n');
}

static void json_write_transit_plan_event(RteOutput *output, const RteTransitStore *store, const RteTransitPlan *plan) {
    const RteTransitStop *board_stop;
    const RteTransitStop *alight_stop;
    const RteTransitRoute *route;
    unsigned int leg_index;
    json_event_begin(output, 1, "transit_plan");
    if (!plan->found || plan->board_stop_index >= store->stop_count || plan->alight_stop_index >= store->stop_count || plan->route_index >= store->route_count) {
        rt_write_cstr(1, ",\"data\":{\"status\":\"unavailable\",\"active_trips\":");
        rt_write_uint(1, plan->active_trips);
        rt_write_cstr(1, ",\"events_scanned\":");
        rt_write_uint(1, plan->events_scanned);
        rt_write_cstr(1, "}}\n");
        return;
    }
    board_stop = &store->stops[plan->board_stop_index];
    alight_stop = &store->stops[plan->alight_stop_index];
    route = &store->routes[plan->route_index];
    rt_write_cstr(1, ",\"data\":{\"status\":\"found\",\"mode\":");
    json_write_cstr_escaped(1, transit_mode_name(plan->mode));
    rt_write_cstr(1, ",\"route_short_name\":");
    json_write_transit_string_or_null(1, store, route->short_name_offset, route->short_name_size);
    rt_write_cstr(1, ",\"route_long_name\":");
    json_write_transit_string_or_null(1, store, route->long_name_offset, route->long_name_size);
    rt_write_cstr(1, ",\"board_stop\":");
    json_write_transit_string_or_null(1, store, board_stop->name_offset, board_stop->name_size);
    rt_write_cstr(1, ",\"alight_stop\":");
    json_write_transit_string_or_null(1, store, alight_stop->name_offset, alight_stop->name_size);
    rt_write_cstr(1, ",\"board_departure_sec\":"); rt_write_uint(1, plan->board_departure_sec);
    rt_write_cstr(1, ",\"alight_arrival_sec\":"); rt_write_uint(1, plan->alight_arrival_sec);
    rt_write_cstr(1, ",\"walk_to_stop_m\":"); rt_write_uint(1, plan->walk_to_stop_m);
    rt_write_cstr(1, ",\"walk_from_stop_m\":"); rt_write_uint(1, plan->walk_from_stop_m);
    rt_write_cstr(1, ",\"total_sec\":"); rt_write_uint(1, plan->total_sec);
    rt_write_cstr(1, ",\"transit_leg_count\":"); rt_write_uint(1, plan->transit_leg_count);
    rt_write_cstr(1, ",\"active_trips\":"); rt_write_uint(1, plan->active_trips);
    rt_write_cstr(1, ",\"events_scanned\":"); rt_write_uint(1, plan->events_scanned);
    json_write_coord_pair(1, "board", board_stop->lat_e7, board_stop->lon_e7);
    json_write_coord_pair(1, "alight", alight_stop->lat_e7, alight_stop->lon_e7);
    rt_write_cstr(1, ",\"legs\":[");
    for (leg_index = 0U; leg_index < plan->leg_count; ++leg_index) {
        const RteTransitPlanLeg *leg = &plan->legs[leg_index];
        if (leg_index != 0U) rt_write_char(1, ',');
        if (leg->kind == RTE_TRANSIT_STATE_VEHICLE && leg->board_stop_index < store->stop_count && leg->alight_stop_index < store->stop_count && leg->route_index < store->route_count) {
            const RteTransitRoute *leg_route = &store->routes[leg->route_index];
            rt_write_cstr(1, "{\"kind\":\"ride\",\"mode\":");
            json_write_cstr_escaped(1, transit_mode_name(leg->mode));
            rt_write_cstr(1, ",\"route_short_name\":");
            json_write_transit_string_or_null(1, store, leg_route->short_name_offset, leg_route->short_name_size);
            rt_write_cstr(1, ",\"route_long_name\":");
            json_write_transit_string_or_null(1, store, leg_route->long_name_offset, leg_route->long_name_size);
            rt_write_cstr(1, ",\"board_stop\":");
            json_write_transit_string_or_null(1, store, store->stops[leg->board_stop_index].name_offset, store->stops[leg->board_stop_index].name_size);
            rt_write_cstr(1, ",\"alight_stop\":");
            json_write_transit_string_or_null(1, store, store->stops[leg->alight_stop_index].name_offset, store->stops[leg->alight_stop_index].name_size);
            rt_write_cstr(1, ",\"departure_sec\":"); rt_write_uint(1, leg->departure_sec);
            rt_write_cstr(1, ",\"arrival_sec\":"); rt_write_uint(1, leg->arrival_sec);
            rt_write_char(1, '}');
        } else if (leg->kind == RTE_TRANSIT_STATE_TRANSFER_WALK && leg->board_stop_index < store->stop_count && leg->alight_stop_index < store->stop_count) {
            rt_write_cstr(1, "{\"kind\":\"transfer_walk\",\"from_stop\":");
            json_write_transit_string_or_null(1, store, store->stops[leg->board_stop_index].name_offset, store->stops[leg->board_stop_index].name_size);
            rt_write_cstr(1, ",\"to_stop\":");
            json_write_transit_string_or_null(1, store, store->stops[leg->alight_stop_index].name_offset, store->stops[leg->alight_stop_index].name_size);
            rt_write_cstr(1, ",\"walk_m\":"); rt_write_uint(1, leg->walk_m);
            rt_write_cstr(1, ",\"departure_sec\":"); rt_write_uint(1, leg->departure_sec);
            rt_write_cstr(1, ",\"arrival_sec\":"); rt_write_uint(1, leg->arrival_sec);
            rt_write_char(1, '}');
        } else {
            rt_write_cstr(1, "{\"kind\":\"unknown\"}");
        }
    }
    rt_write_char(1, ']');
    rt_write_cstr(1, "}}\n");
}

static void json_write_route_summary_event(
    RteOutput *output,
    const RteGraph *graph,
    unsigned int source,
    unsigned int target,
    unsigned int tiles_loaded,
    unsigned int from_snap_m,
    unsigned int to_snap_m,
    unsigned long long graph_distance_m,
    unsigned long long total_distance_m,
    unsigned int direct_distance
) {
    unsigned long long minutes = (total_distance_m * 3ULL + 249ULL) / 250ULL;

    json_event_begin(output, 1, "route");
    rt_write_cstr(1, ",\"data\":{\"status\":\"found\",\"mode\":\"walking\",\"distance_m\":");
    rt_write_uint(1, total_distance_m);
    rt_write_cstr(1, ",\"graph_distance_m\":");
    rt_write_uint(1, graph_distance_m);
    rt_write_cstr(1, ",\"direct_distance_m\":");
    rt_write_uint(1, direct_distance);
    rt_write_cstr(1, ",\"estimated_minutes\":");
    rt_write_uint(1, minutes);
    rt_write_cstr(1, ",\"loaded_tiles\":");
    rt_write_uint(1, tiles_loaded);
    rt_write_cstr(1, ",\"loaded_graph_nodes\":");
    rt_write_uint(1, graph->node_count);
    rt_write_cstr(1, ",\"loaded_graph_edges\":");
    rt_write_uint(1, graph->edge_count);
    rt_write_cstr(1, ",\"from_snap_m\":");
    rt_write_uint(1, from_snap_m);
    rt_write_cstr(1, ",\"to_snap_m\":");
    rt_write_uint(1, to_snap_m);
    json_write_coord_pair(1, "source", graph->nodes[source].lat_e7, graph->nodes[source].lon_e7);
    json_write_coord_pair(1, "target", graph->nodes[target].lat_e7, graph->nodes[target].lon_e7);
    rt_write_cstr(1, "}}\n");
}

static void json_write_route_point_event(RteOutput *output, unsigned int index, const char *kind, int lat_e7, int lon_e7, unsigned long long cumulative_m) {
    json_event_begin(output, 1, "route_point");
    rt_write_cstr(1, ",\"data\":{\"index\":");
    rt_write_uint(1, index);
    rt_write_cstr(1, ",\"kind\":");
    json_write_cstr_escaped(1, kind);
    rt_write_cstr(1, ",\"lat\":");
    json_write_coord_e7(1, lat_e7);
    rt_write_cstr(1, ",\"lon\":");
    json_write_coord_e7(1, lon_e7);
    rt_write_cstr(1, ",\"cumulative_m\":");
    rt_write_uint(1, cumulative_m);
    rt_write_cstr(1, "}}\n");
}

static void json_write_step_base(RteOutput *output, unsigned int step, const char *action) {
    json_event_begin(output, 1, "route_step");
    rt_write_cstr(1, ",\"data\":{\"step\":");
    rt_write_uint(1, step);
    rt_write_cstr(1, ",\"action\":");
    json_write_cstr_escaped(1, action);
}

static int json_write_route_geometry_and_steps(
    RteOutput *output,
    const RteGraph *graph,
    unsigned int source,
    unsigned int target,
    const RteResolvedAddress *from,
    const RteResolvedAddress *to,
    const char *strings,
    unsigned int strings_size,
    unsigned int from_snap_m,
    unsigned int to_snap_m,
    unsigned long long total_distance_m
) {
    unsigned int *path;
    unsigned int path_count;
    unsigned int index;
    unsigned int point_index = 0U;
    unsigned int step = 1U;
    unsigned long long cumulative_m = 0ULL;

    if (build_route_path(graph, source, target, &path, &path_count) != 0) return -1;
    json_write_route_point_event(output, point_index++, "origin", from->record.lat_e7, from->record.lon_e7, 0ULL);
    cumulative_m = from_snap_m;
    for (index = 0U; index < path_count; ++index) {
        if (index != 0U) cumulative_m += graph->nodes[path[index]].previous_edge_meters;
        json_write_route_point_event(output, point_index++, "graph", graph->nodes[path[index]].lat_e7, graph->nodes[path[index]].lon_e7, cumulative_m);
    }
    json_write_route_point_event(output, point_index, "destination", to->record.lat_e7, to->record.lon_e7, total_distance_m);

    json_write_step_base(output, step++, "start");
    json_write_table_string_or_null(1, "street", strings, strings_size, from->record.street_offset, from->record.street_size);
    json_write_table_string_or_null(1, "housenumber", strings, strings_size, from->record.housenumber_offset, from->record.housenumber_size);
    json_write_table_string_or_null(1, "city", strings, strings_size, from->record.city_offset, from->record.city_size);
    json_write_coord_pair(1, "at", from->record.lat_e7, from->record.lon_e7);
    rt_write_cstr(1, "}}\n");
    if (from_snap_m != 0U) {
        json_write_step_base(output, step++, "join_graph");
        rt_write_cstr(1, ",\"distance_m\":");
        rt_write_uint(1, from_snap_m);
        json_write_coord_pair(1, "from", from->record.lat_e7, from->record.lon_e7);
        json_write_coord_pair(1, "to", graph->nodes[source].lat_e7, graph->nodes[source].lon_e7);
        rt_write_cstr(1, "}}\n");
    }
    if (path_count > 1U) {
        unsigned int group_start = 0U;
        unsigned long long group_meters = 0ULL;
        for (index = 1U; index < path_count; ++index) {
            group_meters += graph->nodes[path[index]].previous_edge_meters;
            if (group_meters >= 450ULL || index + 1U == path_count) {
                const char *direction = compass_direction(graph->nodes[path[group_start]].lat_e7, graph->nodes[path[group_start]].lon_e7, graph->nodes[path[index]].lat_e7, graph->nodes[path[index]].lon_e7);
                json_write_step_base(output, step++, "continue");
                rt_write_cstr(1, ",\"direction\":");
                json_write_cstr_escaped(1, direction);
                rt_write_cstr(1, ",\"distance_m\":");
                rt_write_uint(1, group_meters);
                rt_write_cstr(1, ",\"path_start_index\":");
                rt_write_uint(1, group_start);
                rt_write_cstr(1, ",\"path_end_index\":");
                rt_write_uint(1, index);
                json_write_coord_pair(1, "from", graph->nodes[path[group_start]].lat_e7, graph->nodes[path[group_start]].lon_e7);
                json_write_coord_pair(1, "to", graph->nodes[path[index]].lat_e7, graph->nodes[path[index]].lon_e7);
                rt_write_cstr(1, "}}\n");
                group_start = index;
                group_meters = 0ULL;
            }
        }
    }
    if (to_snap_m != 0U) {
        json_write_step_base(output, step++, "leave_graph");
        rt_write_cstr(1, ",\"distance_m\":");
        rt_write_uint(1, to_snap_m);
        json_write_coord_pair(1, "from", graph->nodes[target].lat_e7, graph->nodes[target].lon_e7);
        json_write_coord_pair(1, "to", to->record.lat_e7, to->record.lon_e7);
        rt_write_cstr(1, "}}\n");
    }
    json_write_step_base(output, step, "arrive");
    json_write_table_string_or_null(1, "street", strings, strings_size, to->record.street_offset, to->record.street_size);
    json_write_table_string_or_null(1, "housenumber", strings, strings_size, to->record.housenumber_offset, to->record.housenumber_size);
    json_write_table_string_or_null(1, "city", strings, strings_size, to->record.city_offset, to->record.city_size);
    json_write_coord_pair(1, "at", to->record.lat_e7, to->record.lon_e7);
    rt_write_cstr(1, "}}\n");
    rt_free(path);
    return 0;
}

static void update_bbox_e7(int lat_e7, int lon_e7, int *min_lat, int *min_lon, int *max_lat, int *max_lon) {
    if (lat_e7 < *min_lat) *min_lat = lat_e7;
    if (lat_e7 > *max_lat) *max_lat = lat_e7;
    if (lon_e7 < *min_lon) *min_lon = lon_e7;
    if (lon_e7 > *max_lon) *max_lon = lon_e7;
}

static int write_polyline_coord(int fd, int lat_e7, int lon_e7) {
    char line[96];
    size_t used = 0U;
    if (append_coord_e7(line, sizeof(line), &used, lon_e7) != 0 || append_char(line, sizeof(line), &used, ',') != 0 || append_coord_e7(line, sizeof(line), &used, lat_e7) != 0 || append_char(line, sizeof(line), &used, '\n') != 0) return -1;
    return rt_write_all(fd, line, used);
}

static int copy_address_field(const char *strings, unsigned int strings_size, unsigned int offset, unsigned int size, char *buffer, size_t capacity) {
    if (!address_field_valid(offset, size, strings_size) || size == 0U || (size_t)size >= capacity) return -1;
    memcpy(buffer, strings + offset, size);
    buffer[size] = '\0';
    return 0;
}

static int address_fields_equal(const char *strings, unsigned int strings_size, unsigned int left_offset, unsigned int left_size, unsigned int right_offset, unsigned int right_size) {
    if (!address_field_valid(left_offset, left_size, strings_size) || !address_field_valid(right_offset, right_size, strings_size)) return 0;
    if (left_size == 0U || left_size != right_size) return 0;
    return memcmp(strings + left_offset, strings + right_offset, left_size) == 0;
}

static int build_bbox_arg(int min_lat, int min_lon, int max_lat, int max_lon, char *buffer, size_t capacity) {
    int lat_padding = (max_lat - min_lat) / 10;
    int lon_padding = (max_lon - min_lon) / 10;
    size_t used = 0U;

    if (lat_padding < 50000) lat_padding = 50000;
    if (lon_padding < 50000) lon_padding = 50000;
    min_lat -= lat_padding;
    max_lat += lat_padding;
    min_lon -= lon_padding;
    max_lon += lon_padding;
    if (append_coord_e7(buffer, capacity, &used, min_lon) != 0 || append_char(buffer, capacity, &used, ',') != 0 ||
        append_coord_e7(buffer, capacity, &used, min_lat) != 0 || append_char(buffer, capacity, &used, ',') != 0 ||
        append_coord_e7(buffer, capacity, &used, max_lon) != 0 || append_char(buffer, capacity, &used, ',') != 0 ||
        append_coord_e7(buffer, capacity, &used, max_lat) != 0) return -1;
    return 0;
}

static int render_route_map(
    const char *program,
    const char *rte_path,
    RteOutput *output,
    const RteGraph *graph,
    unsigned int source,
    unsigned int target,
    const RteResolvedAddress *from,
    const RteResolvedAddress *to,
    const char *strings,
    unsigned int strings_size
) {
    unsigned int *path;
    unsigned int path_count;
    unsigned int index;
    int min_lat = from->record.lat_e7;
    int max_lat = from->record.lat_e7;
    int min_lon = from->record.lon_e7;
    int max_lon = from->record.lon_e7;
    char renderer_path[512];
    char inferred_rpack_path[512];
    char temp_path[512];
    char render_log_path[512];
    char bbox_arg[160];
    char city_arg[256];
    const char *rpack_path = output->rpack_path;
    const char *renderer_output_path = 0;
    const char *render_width_arg = 0;
    const char *render_height_arg = 0;
    int use_city_view = 0;
    int fd;
    int pid;
    int exit_status;
    char *argv[15];
    unsigned int argv_index = 0U;

    if (output->map_path == 0) return 0;
    if (build_route_path(graph, source, target, &path, &path_count) != 0) return -1;
    update_bbox_e7(to->record.lat_e7, to->record.lon_e7, &min_lat, &min_lon, &max_lat, &max_lon);
    for (index = 0U; index < path_count; ++index) update_bbox_e7(graph->nodes[path[index]].lat_e7, graph->nodes[path[index]].lon_e7, &min_lat, &min_lon, &max_lat, &max_lon);
    if (build_bbox_arg(min_lat, min_lon, max_lat, max_lon, bbox_arg, sizeof(bbox_arg)) != 0 ||
        path_join_sibling_tool(program, "rpack-render", renderer_path, sizeof(renderer_path)) != 0 ||
        derive_temp_polyline_path(output->map_path, temp_path, sizeof(temp_path)) != 0 ||
        (output->json && derive_temp_render_log_path(output->map_path, render_log_path, sizeof(render_log_path)) != 0)) {
        rt_free(path);
        return -1;
    }
    if (rpack_path == 0) rpack_path = infer_rpack_path(rte_path, inferred_rpack_path, sizeof(inferred_rpack_path));
    if (rpack_path == 0) {
        rt_free(path);
        if (output->json) json_diagnostic(output, "error", "--map needs --rpack FILE.rpack; no default render pack was found", 0);
        else rt_write_cstr(2, "rte-route: --map needs --rpack FILE.rpack; no default render pack was found\n");
        return -1;
    }
    if (address_fields_equal(strings, strings_size, from->record.city_offset, from->record.city_size, to->record.city_offset, to->record.city_size) &&
        copy_address_field(strings, strings_size, from->record.city_offset, from->record.city_size, city_arg, sizeof(city_arg)) == 0) {
        use_city_view = 1;
    }
    fd = platform_open_write(temp_path, 0644U);
    if (fd < 0) { rt_free(path); return -1; }
    if (write_polyline_coord(fd, from->record.lat_e7, from->record.lon_e7) != 0) { (void)platform_close(fd); rt_free(path); return -1; }
    for (index = 0U; index < path_count; ++index) {
        if (write_polyline_coord(fd, graph->nodes[path[index]].lat_e7, graph->nodes[path[index]].lon_e7) != 0) { (void)platform_close(fd); rt_free(path); return -1; }
    }
    if (write_polyline_coord(fd, to->record.lat_e7, to->record.lon_e7) != 0 || platform_close(fd) != 0) { rt_free(path); return -1; }
    rt_free(path);

    argv[argv_index++] = renderer_path;
    argv[argv_index++] = (char *)rpack_path;
    argv[argv_index++] = (char *)output->map_path;
    if (use_city_view) {
        argv[argv_index++] = "--city";
        argv[argv_index++] = city_arg;
    } else {
        argv[argv_index++] = "--bbox";
        argv[argv_index++] = bbox_arg;
    }
    if (output->map_width_arg != 0) {
        argv[argv_index++] = "--width";
        render_width_arg = output->map_width_arg;
        argv[argv_index++] = (char *)render_width_arg;
    } else if (!use_city_view && output->map_height_arg == 0) {
        argv[argv_index++] = "--width";
        render_width_arg = "1600";
        argv[argv_index++] = (char *)render_width_arg;
    } else if (use_city_view && output->map_height_arg == 0) {
        argv[argv_index++] = "--width";
        render_width_arg = "1600";
        argv[argv_index++] = (char *)render_width_arg;
    }
    if (output->map_height_arg != 0) {
        argv[argv_index++] = "--height";
        render_height_arg = output->map_height_arg;
        argv[argv_index++] = (char *)render_height_arg;
    } else if (use_city_view && output->map_width_arg == 0) {
        argv[argv_index++] = "--height";
        render_height_arg = "1200";
        argv[argv_index++] = (char *)render_height_arg;
    }
    argv[argv_index++] = "--route-polyline";
    argv[argv_index++] = temp_path;
    argv[argv_index] = 0;
    if (output->json) renderer_output_path = render_log_path;
    if (platform_spawn_process(argv, -1, -1, 0, renderer_output_path, 0, &pid) != 0 || platform_wait_process(pid, &exit_status) != 0 || exit_status != 0) {
        (void)platform_remove_file(temp_path);
        if (output->json) {
            (void)platform_remove_file(render_log_path);
            json_diagnostic(output, "error", "map renderer failed", output->map_path);
            json_event_begin(output, 1, "map");
            rt_write_cstr(1, ",\"data\":{\"status\":\"failed\",\"output\":");
            json_write_cstr_escaped(1, output->map_path);
            rt_write_cstr(1, "}}\n");
        } else {
            rt_write_cstr(2, "rte-route: map renderer failed\n");
        }
        return -1;
    }
    (void)platform_remove_file(temp_path);
    if (output->json) {
        (void)platform_remove_file(render_log_path);
        json_event_begin(output, 1, "map");
        rt_write_cstr(1, ",\"data\":{\"status\":\"written\",\"output\":");
        json_write_cstr_escaped(1, output->map_path);
        rt_write_cstr(1, ",\"rpack\":");
        json_write_cstr_escaped(1, rpack_path);
        rt_write_cstr(1, ",\"bbox\":");
        json_write_cstr_escaped(1, bbox_arg);
        rt_write_cstr(1, ",\"view\":");
        json_write_cstr_escaped(1, use_city_view ? "city" : "bbox");
        rt_write_cstr(1, ",\"city\":");
        if (use_city_view) json_write_cstr_escaped(1, city_arg);
        else rt_write_cstr(1, "null");
        rt_write_cstr(1, ",\"width_arg\":");
        if (render_width_arg != 0) json_write_cstr_escaped(1, render_width_arg);
        else rt_write_cstr(1, "null");
        rt_write_cstr(1, ",\"height_arg\":");
        if (render_height_arg != 0) json_write_cstr_escaped(1, render_height_arg);
        else rt_write_cstr(1, "null");
        rt_write_cstr(1, "}}\n");
    } else {
        rt_write_cstr(1, "map_output: ");
        rt_write_cstr(1, output->map_path);
        rt_write_char(1, '\n');
        rt_write_cstr(1, "map_rpack: ");
        rt_write_cstr(1, rpack_path);
        rt_write_char(1, '\n');
        rt_write_cstr(1, "map_bbox: ");
        rt_write_cstr(1, bbox_arg);
        rt_write_char(1, '\n');
        if (use_city_view) {
            rt_write_cstr(1, "map_view: city\n");
            rt_write_cstr(1, "map_city: ");
            rt_write_cstr(1, city_arg);
            rt_write_char(1, '\n');
        } else {
            rt_write_cstr(1, "map_view: bbox\n");
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "rte-route";
    const char *path;
    const char *from_query;
    const char *to_query;
    unsigned char header_bytes[RTE_HEADER_SIZE];
    RteHeader header;
    RteAddressSection address_section;
    RteTransitSection transit_section;
    RteTransitStore transit_store;
    RteResolvedAddress from;
    RteResolvedAddress to;
    char *strings;
    unsigned int strings_size;
    RteOutput output;
    int fd;
    int argi;

    if (argc == 2 && (rt_strcmp(argv[1], "-h") == 0 || rt_strcmp(argv[1], "--help") == 0)) { write_usage(program); return 0; }
    if (argc < 4) { write_usage(program); return 1; }
    rt_memset(&output, 0, sizeof(output));
    rt_memset(&transit_store, 0, sizeof(transit_store));
    output.use_color = 1;
    path = argv[1];
    from_query = argv[2];
    to_query = argv[3];
    for (argi = 4; argi < argc; ++argi) {
        if (rt_strcmp(argv[argi], "--no-color") == 0) output.use_color = 0;
        else if (rt_strcmp(argv[argi], "--color") == 0) output.use_color = 1;
        else if (rt_strcmp(argv[argi], "--json") == 0) output.json = 1;
        else if (rt_strcmp(argv[argi], "--transit") == 0) output.transit_only = 1;
        else if (rt_strcmp(argv[argi], "--verbose") == 0) output.verbose = 1;
        else if (rt_strcmp(argv[argi], "--depart") == 0) {
            argi += 1;
            if (argi >= argc || parse_datetime_arg(argv[argi], &output.depart_date, &output.depart_seconds) != 0) { if (output.json) json_diagnostic(&output, "error", "invalid or missing --depart value", 0); else write_usage(program); return 1; }
            output.have_depart = 1;
        } else if (rt_strcmp(argv[argi], "--arrive") == 0) {
            argi += 1;
            if (argi >= argc || parse_datetime_arg(argv[argi], &output.arrive_date, &output.arrive_seconds) != 0) { if (output.json) json_diagnostic(&output, "error", "invalid or missing --arrive value", 0); else write_usage(program); return 1; }
            output.have_arrive = 1;
        }
        else if (rt_strcmp(argv[argi], "--map") == 0) {
            argi += 1;
            if (argi >= argc) { if (output.json) json_diagnostic(&output, "error", "missing value for --map", 0); else write_usage(program); return 1; }
            output.map_path = argv[argi];
        } else if (rt_strcmp(argv[argi], "--rpack") == 0) {
            argi += 1;
            if (argi >= argc) { if (output.json) json_diagnostic(&output, "error", "missing value for --rpack", 0); else write_usage(program); return 1; }
            output.rpack_path = argv[argi];
        } else if (rt_strcmp(argv[argi], "--width") == 0) {
            argi += 1;
            if (argi >= argc || parse_dimension_arg(argv[argi]) != 0) { if (output.json) json_diagnostic(&output, "error", "invalid or missing --width value", 0); else write_usage(program); return 1; }
            output.map_width_arg = argv[argi];
        } else if (rt_strcmp(argv[argi], "--height") == 0) {
            argi += 1;
            if (argi >= argc || parse_dimension_arg(argv[argi]) != 0) { if (output.json) json_diagnostic(&output, "error", "invalid or missing --height value", 0); else write_usage(program); return 1; }
            output.map_height_arg = argv[argi];
        }
        else { if (output.json) json_diagnostic(&output, "error", "unknown option", argv[argi]); else write_usage(program); return 1; }
    }
    if (output.have_depart && output.have_arrive) { if (output.json) json_diagnostic(&output, "error", "use either --depart or --arrive, not both", 0); else write_usage(program); return 1; }
    if (output.json) output.use_color = 0;

    fd = platform_open_read(path);
    if (fd < 0) { if (output.json) json_diagnostic(&output, "error", "could not open route pack", path); else rt_write_cstr(2, "rte-route: could not open route pack\n"); return 1; }
    if (read_exact(fd, header_bytes, sizeof(header_bytes)) != 0 || parse_header(header_bytes, &header) != 0 || read_sections(fd, &header, &address_section, &transit_section) != 0) {
        (void)platform_close(fd);
        if (output.json) json_diagnostic(&output, "error", "invalid or unsupported route pack", path);
        else rt_write_cstr(2, "rte-route: invalid or unsupported route pack\n");
        return 1;
    }
    if (!output.have_depart && !output.have_arrive && transit_section.present && set_depart_now(&output) != 0) {
        if (output.json) json_diagnostic(&output, "warning", "could not determine current departure time", 0);
        else rt_write_cstr(2, "rte-route: could not determine current departure time; transit suggestions unavailable\n");
    }
    if (!address_section.present || read_address_strings(fd, &address_section, &strings, &strings_size) != 0) {
        (void)platform_close(fd);
        if (output.json) json_diagnostic(&output, "error", "route pack has no readable address section", path);
        else rt_write_cstr(2, "rte-route: route pack has no readable address section\n");
        return 1;
    }
    if ((output.have_depart || output.have_arrive) && (!transit_section.present || read_transit_section(fd, &transit_section, &transit_store) != 0)) {
        if (output.json) json_diagnostic(&output, "warning", "route pack has no readable GTFS transit section", path);
        else rt_write_cstr(2, "rte-route: route pack has no readable GTFS transit section; transit suggestions unavailable\n");
    }
    if (resolve_query(fd, &header, &address_section, from_query, strings, strings_size, &from) != 0 || resolve_query(fd, &header, &address_section, to_query, strings, strings_size, &to) != 0) {
        rt_free(strings);
        (void)platform_close(fd);
        if (output.json) json_diagnostic(&output, "error", "address lookup failed", 0);
        else rt_write_cstr(2, "rte-route: address lookup failed\n");
        return 1;
    }

    if (output.json) {
        json_write_metadata_event(&output, from_query, to_query);
        json_write_address_event(&output, "from", from_query, &from, strings, strings_size);
        json_write_address_event(&output, "to", to_query, &to, strings, strings_size);
    } else if (output.verbose) {
        rt_write_cstr(1, "format: OSMRTE01\n");
        rt_write_cstr(1, output.transit_only ? "profile: transit\n" : "profile: walking\n");
        rt_write_cstr(1, "from_query: "); rt_write_cstr(1, from_query); rt_write_char(1, '\n');
        write_address_summary("from", &from, strings, strings_size);
        rt_write_cstr(1, "to_query: "); rt_write_cstr(1, to_query); rt_write_char(1, '\n');
        write_address_summary("to", &to, strings, strings_size);
    }

    if (output.transit_only) {
        if (!from.found || !to.found) {
            if (output.json) json_write_route_status_event(&output, "unavailable", "address_not_found");
            else rt_write_cstr(1, "transit_status: unavailable\ntransit_status_reason: address_not_found\n");
        } else if ((from.record.flags & 1U) == 0U || (to.record.flags & 1U) == 0U) {
            if (output.json) json_write_route_status_event(&output, "unavailable", "address record has no coordinate yet; way/relation address centroids are not embedded in this route pack");
            else rt_write_cstr(1, "transit_status: unavailable\ntransit_status_reason: address record has no coordinate yet\n");
        } else if (transit_store.event_count != 0U) {
            RteTransitPlan transit_plan;
            (void)evaluate_transit_plan(&transit_store, &from, &to, &output, &transit_plan);
            if (output.json) json_write_transit_plan_event(&output, &transit_store, &transit_plan);
            else if (transit_plan.found) write_transit_plan_text(&output, &transit_store, &transit_plan, 0ULL);
            else rt_write_cstr(1, "transit_status: unavailable\ntransit_status_reason: no GTFS transit option found for this time\n");
        } else {
            if (output.json) {
                RteTransitPlan empty_plan;
                rt_memset(&empty_plan, 0, sizeof(empty_plan));
                json_write_transit_plan_event(&output, &transit_store, &empty_plan);
            } else {
                rt_write_cstr(1, "transit_status: unavailable\ntransit_status_reason: no GTFS data in route pack\n");
            }
        }
        rt_free(transit_store.stops);
        rt_free(transit_store.routes);
        rt_free(transit_store.services);
        rt_free(transit_store.exceptions);
        rt_free(transit_store.trips);
        rt_free(transit_store.events);
        rt_free(transit_store.strings);
        rt_free(strings);
        (void)platform_close(fd);
        return 0;
    }

    if (!from.found || !to.found) {
        if (output.json) json_write_route_status_event(&output, "unavailable", "address_not_found");
        else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: address_not_found\n");
    } else if ((from.record.flags & 1U) == 0U || (to.record.flags & 1U) == 0U) {
        if (output.json) json_write_route_status_event(&output, "unavailable", "address record has no coordinate yet; way/relation address centroids are not embedded in this route pack");
        else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: address record has no coordinate yet; way/relation address centroids are not embedded in this route pack\n");
    } else {
        RteTileRecord from_tile;
        RteTileRecord to_tile;
        int from_tile_x;
        int from_tile_y;
        int to_tile_x;
        int to_tile_y;
        unsigned long long from_tile_id;
        unsigned long long to_tile_id;
        int from_tile_found;
        int to_tile_found;
        unsigned int straight_distance;
        lat_lon_to_tile(&header, from.record.lat_e7, from.record.lon_e7, &from_tile_x, &from_tile_y, &from_tile_id);
        lat_lon_to_tile(&header, to.record.lat_e7, to.record.lon_e7, &to_tile_x, &to_tile_y, &to_tile_id);
        from_tile_found = find_tile_by_id(fd, &header, from_tile_id, &from_tile);
        to_tile_found = find_tile_by_id(fd, &header, to_tile_id, &to_tile);
        straight_distance = direct_distance_m(from.record.lat_e7, from.record.lon_e7, to.record.lat_e7, to.record.lon_e7);
        if (output.json) {
            json_event_begin(&output, 1, "tile_context");
            rt_write_cstr(1, ",\"data\":{\"from_tile_x\":"); rt_write_int(1, from_tile_x);
            rt_write_cstr(1, ",\"from_tile_y\":"); rt_write_int(1, from_tile_y);
            rt_write_cstr(1, ",\"from_tile_id\":"); json_write_hex_u64_string(1, from_tile_id);
            rt_write_cstr(1, ",\"to_tile_x\":"); rt_write_int(1, to_tile_x);
            rt_write_cstr(1, ",\"to_tile_y\":"); rt_write_int(1, to_tile_y);
            rt_write_cstr(1, ",\"to_tile_id\":"); json_write_hex_u64_string(1, to_tile_id);
            rt_write_cstr(1, ",\"from_tile_found\":"); rt_write_cstr(1, from_tile_found > 0 ? "true" : "false");
            rt_write_cstr(1, ",\"to_tile_found\":"); rt_write_cstr(1, to_tile_found > 0 ? "true" : "false");
            rt_write_cstr(1, ",\"direct_distance_m\":"); rt_write_uint(1, straight_distance);
            rt_write_cstr(1, "}}\n");
        } else if (output.verbose) {
            rt_write_cstr(1, "from_tile_x: "); rt_write_int(1, from_tile_x); rt_write_char(1, '\n');
            rt_write_cstr(1, "from_tile_y: "); rt_write_int(1, from_tile_y); rt_write_char(1, '\n');
            rt_write_cstr(1, "to_tile_x: "); rt_write_int(1, to_tile_x); rt_write_char(1, '\n');
            rt_write_cstr(1, "to_tile_y: "); rt_write_int(1, to_tile_y); rt_write_char(1, '\n');
            rt_write_cstr(1, "direct_distance_m: "); rt_write_uint(1, straight_distance); rt_write_char(1, '\n');
        }
        if (from_tile_found <= 0 || to_tile_found <= 0) {
            if (output.json) json_write_route_status_event(&output, "unavailable", "endpoint tile not found");
            else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: endpoint tile not found\n");
        } else if (from_tile.local_node_count == 0U || to_tile.local_node_count == 0U || from_tile.local_directed_edge_count == 0U || to_tile.local_directed_edge_count == 0U) {
            if (output.json) json_write_route_status_event(&output, "unavailable", "walking graph payloads are empty in this route pack");
            else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: walking graph payloads are empty in this route pack\n");
        } else {
            RteGraph graph;
            unsigned int tiles_loaded;
            unsigned int from_snap_m;
            unsigned int to_snap_m;
            unsigned int source;
            unsigned int target;
            unsigned long long route_distance;
            int route_status;
            rt_memset(&graph, 0, sizeof(graph));
            if (load_neighborhood_graph(fd, &header, from_tile_x, from_tile_y, to_tile_x, to_tile_y, &graph, &tiles_loaded) != 0) {
                if (output.json) json_write_route_status_event(&output, "unavailable", "failed to load walking graph payloads");
                else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: failed to load walking graph payloads\n");
            } else if (graph.node_count == 0U || graph.edge_count == 0U) {
                if (output.json) json_write_route_status_event(&output, "unavailable", "loaded walking graph is empty");
                else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: loaded walking graph is empty\n");
            } else {
                source = nearest_graph_node(&graph, from.record.lat_e7, from.record.lon_e7, &from_snap_m);
                target = nearest_graph_node(&graph, to.record.lat_e7, to.record.lon_e7, &to_snap_m);
                if (output.json) {
                    json_event_begin(&output, 1, "graph_loaded");
                    rt_write_cstr(1, ",\"data\":{\"loaded_tiles\":"); rt_write_uint(1, tiles_loaded);
                    rt_write_cstr(1, ",\"nodes\":"); rt_write_uint(1, graph.node_count);
                    rt_write_cstr(1, ",\"directed_edges\":"); rt_write_uint(1, graph.edge_count);
                    rt_write_cstr(1, ",\"source_node_index\":"); rt_write_uint(1, source);
                    rt_write_cstr(1, ",\"target_node_index\":"); rt_write_uint(1, target);
                    rt_write_cstr(1, ",\"from_snap_m\":"); rt_write_uint(1, from_snap_m);
                    rt_write_cstr(1, ",\"to_snap_m\":"); rt_write_uint(1, to_snap_m);
                    json_write_coord_pair(1, "source", graph.nodes[source].lat_e7, graph.nodes[source].lon_e7);
                    json_write_coord_pair(1, "target", graph.nodes[target].lat_e7, graph.nodes[target].lon_e7);
                    rt_write_cstr(1, "}}\n");
                } else if (output.verbose) {
                    rt_write_cstr(1, "loaded_tiles: "); rt_write_uint(1, tiles_loaded); rt_write_char(1, '\n');
                    rt_write_cstr(1, "loaded_graph_nodes: "); rt_write_uint(1, graph.node_count); rt_write_char(1, '\n');
                    rt_write_cstr(1, "loaded_graph_edges: "); rt_write_uint(1, graph.edge_count); rt_write_char(1, '\n');
                    rt_write_cstr(1, "from_snap_m: "); rt_write_uint(1, from_snap_m); rt_write_char(1, '\n');
                    rt_write_cstr(1, "to_snap_m: "); rt_write_uint(1, to_snap_m); rt_write_char(1, '\n');
                }
                route_status = dijkstra(&graph, source, target, &route_distance);
                if (route_status < 0) {
                    if (output.json) json_write_route_status_event(&output, "unavailable", "dijkstra_failed");
                    else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: dijkstra_failed\n");
                } else if (route_status == 0) {
                    if (output.json) json_write_route_status_event(&output, "unavailable", "no path found in loaded tile neighborhood");
                    else rt_write_cstr(1, "route_status: unavailable\nroute_status_reason: no path found in loaded tile neighborhood\n");
                } else {
                    unsigned long long total_distance = route_distance + (unsigned long long)from_snap_m + (unsigned long long)to_snap_m;
                    if (output.json) {
                        json_write_route_status_event(&output, "found", 0);
                        json_write_route_summary_event(&output, &graph, source, target, tiles_loaded, from_snap_m, to_snap_m, route_distance, total_distance, straight_distance);
                        if (json_write_route_geometry_and_steps(&output, &graph, source, target, &from, &to, strings, strings_size, from_snap_m, to_snap_m, total_distance) != 0) json_diagnostic(&output, "error", "failed to emit route geometry", 0);
                    } else {
                        if (output.verbose) {
                            rt_write_cstr(1, "route_status: found\n");
                            rt_write_cstr(1, "route_distance_m: "); rt_write_uint(1, total_distance); rt_write_char(1, '\n');
                            rt_write_cstr(1, "graph_distance_m: "); rt_write_uint(1, route_distance); rt_write_char(1, '\n');
                        }
                        (void)write_human_route(&output, &graph, source, target, &from, &to, strings, strings_size, from_snap_m, to_snap_m, route_distance, total_distance, tiles_loaded);
                    }
                    if ((output.have_depart || output.have_arrive) && transit_store.event_count != 0U) {
                        RteTransitPlan transit_plan;
                        (void)evaluate_transit_plan(&transit_store, &from, &to, &output, &transit_plan);
                        if (output.json) json_write_transit_plan_event(&output, &transit_store, &transit_plan);
                        else if (transit_plan.found) write_transit_plan_text(&output, &transit_store, &transit_plan, total_distance);
                        else rt_write_cstr(1, "transit_status: unavailable\ntransit_status_reason: no single-leg GTFS option found for this time\n");
                    } else if (output.have_depart || output.have_arrive) {
                        if (output.json) {
                            RteTransitPlan empty_plan;
                            rt_memset(&empty_plan, 0, sizeof(empty_plan));
                            json_write_transit_plan_event(&output, &transit_store, &empty_plan);
                        } else {
                            rt_write_cstr(1, "transit_status: unavailable\ntransit_status_reason: no GTFS data in route pack\n");
                        }
                    }
                    if (output.map_path != 0 && render_route_map(program, path, &output, &graph, source, target, &from, &to, strings, strings_size) != 0) {
                        if (!output.json) rt_write_cstr(1, "map_status: failed\n");
                    }
                }
            }
            rt_free(graph.nodes);
            rt_free(graph.slots);
            rt_free(graph.edges);
        }
    }

    rt_free(transit_store.stops);
    rt_free(transit_store.routes);
    rt_free(transit_store.services);
    rt_free(transit_store.exceptions);
    rt_free(transit_store.trips);
    rt_free(transit_store.events);
    rt_free(transit_store.strings);
    rt_free(strings);
    (void)platform_close(fd);
    return 0;
}