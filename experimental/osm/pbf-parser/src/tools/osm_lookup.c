#include "pbf.h"
#include "osm_index.h"
#include "runtime.h"

#define OSMLOOKUP_TYPE_NODE 1U
#define OSMLOOKUP_TYPE_WAY 2U
#define OSMLOOKUP_TYPE_RELATION 4U
#define OSMLOOKUP_TYPE_ALL (OSMLOOKUP_TYPE_NODE | OSMLOOKUP_TYPE_WAY | OSMLOOKUP_TYPE_RELATION)
#define OSMLOOKUP_MAX_TAG_FILTERS 16U

typedef struct {
    const char *key;
    size_t key_size;
    const char *value;
    size_t value_size;
    int has_value;
} OsmLookupTagFilter;

typedef struct {
    unsigned int types;
    OsmLookupTagFilter tag_filters[OSMLOOKUP_MAX_TAG_FILTERS];
    unsigned int tag_filter_count;
    int has_id;
    unsigned int id_type;
    long long id;
    unsigned long long limit;
    unsigned long long matches;
    int has_bbox;
    long long min_lon_nano;
    long long min_lat_nano;
    long long max_lon_nano;
    long long max_lat_nano;
    int show_geometry;
    const char *node_index_path;
    OsmNodeIndex node_index;
    int node_index_open;
    int failed;
    char error[OSM_INDEX_ERROR_CAPACITY];
} OsmLookupContext;

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.osm.pbf [--type node|way|relation|all] [--tag KEY[=VALUE]] [--name VALUE] [--id TYPE:ID] [--bbox MINLON,MINLAT,MAXLON,MAXLAT] [--node-index FILE] [--geometry] [--limit N]\n");
}

static int parse_uint_arg(const char *text, unsigned long long *value_out) {
    unsigned long long value = 0ULL;
    size_t index = 0U;

    if (text == 0 || text[0] == '\0') {
        return -1;
    }
    while (text[index] != '\0') {
        unsigned long long previous;
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        previous = value;
        value = value * 10ULL + (unsigned long long)(text[index] - '0');
        if (value < previous) {
            return -1;
        }
        index += 1U;
    }
    *value_out = value;
    return 0;
}

static int parse_signed_id(const char *text, long long *value_out) {
    unsigned long long value = 0ULL;
    size_t index = 0U;
    int negative = 0;

    if (text == 0 || text[0] == '\0') {
        return -1;
    }
    if (text[0] == '-') {
        negative = 1;
        index = 1U;
    }
    if (text[index] == '\0') {
        return -1;
    }
    while (text[index] != '\0') {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10ULL + (unsigned long long)(text[index] - '0');
        if (value > 9223372036854775807ULL) {
            return -1;
        }
        index += 1U;
    }
    *value_out = negative ? -((long long)value) : (long long)value;
    return 0;
}

static int parse_coord_part(const char *text, size_t size, long long *value_out) {
    size_t index = 0U;
    unsigned long long whole = 0ULL;
    unsigned long long fraction = 0ULL;
    unsigned int fraction_digits = 0U;
    int negative = 0;

    if (size == 0U) {
        return -1;
    }
    if (text[index] == '-') {
        negative = 1;
        index += 1U;
    }
    if (index >= size || text[index] < '0' || text[index] > '9') {
        return -1;
    }
    while (index < size && text[index] >= '0' && text[index] <= '9') {
        whole = whole * 10ULL + (unsigned long long)(text[index] - '0');
        if (whole > 180ULL) {
            return -1;
        }
        index += 1U;
    }
    if (index < size && text[index] == '.') {
        index += 1U;
        while (index < size && text[index] >= '0' && text[index] <= '9') {
            if (fraction_digits >= 9U) {
                return -1;
            }
            fraction = fraction * 10ULL + (unsigned long long)(text[index] - '0');
            fraction_digits += 1U;
            index += 1U;
        }
    }
    if (index != size) {
        return -1;
    }
    while (fraction_digits < 9U) {
        fraction *= 10ULL;
        fraction_digits += 1U;
    }
    *value_out = (long long)(whole * 1000000000ULL + fraction);
    if (negative) {
        *value_out = -*value_out;
    }
    return 0;
}

static int parse_bbox_arg(const char *text, OsmLookupContext *context) {
    const char *parts[4];
    size_t sizes[4];
    size_t start = 0U;
    size_t index = 0U;
    unsigned int part = 0U;
    long long min_lon;
    long long min_lat;
    long long max_lon;
    long long max_lat;

    for (;;) {
        if (text[index] == ',' || text[index] == '\0') {
            if (part >= 4U) {
                return -1;
            }
            parts[part] = text + start;
            sizes[part] = index - start;
            part += 1U;
            if (text[index] == '\0') {
                break;
            }
            start = index + 1U;
        }
        index += 1U;
    }
    if (part != 4U) {
        return -1;
    }
    if (parse_coord_part(parts[0], sizes[0], &min_lon) != 0 ||
        parse_coord_part(parts[1], sizes[1], &min_lat) != 0 ||
        parse_coord_part(parts[2], sizes[2], &max_lon) != 0 ||
        parse_coord_part(parts[3], sizes[3], &max_lat) != 0) {
        return -1;
    }
    if (min_lon > max_lon || min_lat > max_lat) {
        return -1;
    }
    context->has_bbox = 1;
    context->min_lon_nano = min_lon;
    context->min_lat_nano = min_lat;
    context->max_lon_nano = max_lon;
    context->max_lat_nano = max_lat;
    return 0;
}

static int parse_type(const char *text, unsigned int *type_out) {
    if (rt_strcmp(text, "node") == 0) {
        *type_out = OSMLOOKUP_TYPE_NODE;
        return 0;
    }
    if (rt_strcmp(text, "way") == 0) {
        *type_out = OSMLOOKUP_TYPE_WAY;
        return 0;
    }
    if (rt_strcmp(text, "relation") == 0) {
        *type_out = OSMLOOKUP_TYPE_RELATION;
        return 0;
    }
    if (rt_strcmp(text, "all") == 0) {
        *type_out = OSMLOOKUP_TYPE_ALL;
        return 0;
    }
    return -1;
}

static int parse_id_filter(const char *text, unsigned int *type_out, long long *id_out) {
    size_t index = 0U;
    char type_text[16];

    while (text[index] != '\0' && text[index] != ':') {
        if (index + 1U >= sizeof(type_text)) {
            return -1;
        }
        type_text[index] = text[index];
        index += 1U;
    }
    if (text[index] != ':') {
        return -1;
    }
    type_text[index] = '\0';
    if (parse_type(type_text, type_out) != 0 || *type_out == OSMLOOKUP_TYPE_ALL) {
        return -1;
    }
    return parse_signed_id(text + index + 1U, id_out);
}

static int add_tag_filter(OsmLookupContext *context, const char *text) {
    size_t index = 0U;
    OsmLookupTagFilter *filter;

    if (context->tag_filter_count >= OSMLOOKUP_MAX_TAG_FILTERS || text == 0 || text[0] == '\0') {
        return -1;
    }
    filter = &context->tag_filters[context->tag_filter_count];
    filter->key = text;
    while (text[index] != '\0' && text[index] != '=') {
        index += 1U;
    }
    if (index == 0U) {
        return -1;
    }
    filter->key_size = index;
    if (text[index] == '=') {
        filter->has_value = 1;
        filter->value = text + index + 1U;
        filter->value_size = rt_strlen(filter->value);
    } else {
        filter->has_value = 0;
        filter->value = 0;
        filter->value_size = 0U;
    }
    context->tag_filter_count += 1U;
    return 0;
}

static int text_equals_cstr(PbfText text, const char *value, size_t value_size) {
    return text.size == value_size && memcmp(text.data, value, value_size) == 0;
}

static int tags_match(const PbfTag *tags, unsigned int tag_count, const OsmLookupContext *context) {
    unsigned int filter_index;

    for (filter_index = 0U; filter_index < context->tag_filter_count; ++filter_index) {
        const OsmLookupTagFilter *filter = &context->tag_filters[filter_index];
        unsigned int tag_index;
        int matched = 0;
        for (tag_index = 0U; tag_index < tag_count; ++tag_index) {
            if (text_equals_cstr(tags[tag_index].key, filter->key, filter->key_size) &&
                (!filter->has_value || text_equals_cstr(tags[tag_index].value, filter->value, filter->value_size))) {
                matched = 1;
                break;
            }
        }
        if (!matched) {
            return 0;
        }
    }
    return 1;
}

static int coord_in_bbox(const OsmLookupContext *context, long long lat_nano, long long lon_nano) {
    return !context->has_bbox ||
           (lon_nano >= context->min_lon_nano && lon_nano <= context->max_lon_nano &&
            lat_nano >= context->min_lat_nano && lat_nano <= context->max_lat_nano);
}

static void write_text(PbfText text) {
    if (text.size != 0U) {
        rt_write_all(1, text.data, text.size);
    }
}

static void write_signed(long long value) {
    if (value < 0) {
        rt_write_char(1, '-');
        rt_write_uint(1, (unsigned long long)(-value));
    } else {
        rt_write_uint(1, (unsigned long long)value);
    }
}

static void write_coord(long long nano) {
    unsigned long long value;
    unsigned long long whole;
    unsigned long long fraction;
    unsigned long long divisor = 100000000ULL;

    if (nano < 0) {
        rt_write_char(1, '-');
        value = (unsigned long long)(-nano);
    } else {
        value = (unsigned long long)nano;
    }
    whole = value / 1000000000ULL;
    fraction = value % 1000000000ULL;
    rt_write_uint(1, whole);
    rt_write_char(1, '.');
    while (divisor != 0ULL) {
        rt_write_char(1, (char)('0' + (fraction / divisor) % 10ULL));
        divisor /= 10ULL;
    }
}

static void write_tags(const PbfTag *tags, unsigned int tag_count) {
    unsigned int index;

    for (index = 0U; index < tag_count; ++index) {
        rt_write_char(1, ' ');
        write_text(tags[index].key);
        rt_write_char(1, '=');
        write_text(tags[index].value);
    }
}

static int lookup_node_record(OsmLookupContext *context, long long id, OsmNodeIndexRecord *record_out) {
    int result = osm_node_index_find(&context->node_index, id, record_out, context->error, sizeof(context->error));

    if (result < 0) {
        context->failed = 1;
    }
    return result;
}

static int way_intersects_bbox(OsmLookupContext *context, const PbfWay *way) {
    unsigned int index;

    if (!context->has_bbox) {
        return 1;
    }
    for (index = 0U; index < way->ref_count; ++index) {
        OsmNodeIndexRecord record;
        int result = lookup_node_record(context, way->refs[index], &record);
        if (result < 0) return 0;
        if (result > 0 && coord_in_bbox(context, record.lat_nano, record.lon_nano)) {
            return 1;
        }
    }
    return 0;
}

static int write_way_geometry(OsmLookupContext *context, const PbfWay *way) {
    unsigned int index;

    rt_write_cstr(1, " geometry=");
    for (index = 0U; index < way->ref_count; ++index) {
        OsmNodeIndexRecord record;
        int result;
        if (index != 0U) {
            rt_write_char(1, ';');
        }
        result = lookup_node_record(context, way->refs[index], &record);
        if (result < 0) return -1;
        if (result == 0) {
            rt_write_char(1, '?');
        } else {
            write_coord(record.lat_nano);
            rt_write_char(1, ',');
            write_coord(record.lon_nano);
        }
    }
    return 0;
}

static int record_match(OsmLookupContext *context) {
    context->matches += 1ULL;
    return context->limit != 0ULL && context->matches >= context->limit;
}

static int on_node(void *user, const PbfNode *node) {
    OsmLookupContext *context = (OsmLookupContext *)user;

    if ((context->types & OSMLOOKUP_TYPE_NODE) == 0U) return 0;
    if (context->has_id && (context->id_type != OSMLOOKUP_TYPE_NODE || context->id != node->id)) return 0;
    if (!coord_in_bbox(context, node->lat_nano, node->lon_nano)) return 0;
    if (!tags_match(node->tags, node->tag_count, context)) return 0;
    rt_write_cstr(1, "node ");
    write_signed(node->id);
    rt_write_cstr(1, " lat=");
    write_coord(node->lat_nano);
    rt_write_cstr(1, " lon=");
    write_coord(node->lon_nano);
    write_tags(node->tags, node->tag_count);
    rt_write_char(1, '\n');
    return record_match(context);
}

static int on_way(void *user, const PbfWay *way) {
    OsmLookupContext *context = (OsmLookupContext *)user;

    if ((context->types & OSMLOOKUP_TYPE_WAY) == 0U) return 0;
    if (context->has_id && (context->id_type != OSMLOOKUP_TYPE_WAY || context->id != way->id)) return 0;
    if (!tags_match(way->tags, way->tag_count, context)) return 0;
    if (!way_intersects_bbox(context, way)) return context->failed ? 1 : 0;
    rt_write_cstr(1, "way ");
    write_signed(way->id);
    rt_write_cstr(1, " refs=");
    rt_write_uint(1, way->ref_count);
    if (context->show_geometry && write_way_geometry(context, way) != 0) {
        return 1;
    }
    write_tags(way->tags, way->tag_count);
    rt_write_char(1, '\n');
    return record_match(context);
}

static int on_relation(void *user, const PbfRelation *relation) {
    OsmLookupContext *context = (OsmLookupContext *)user;

    if ((context->types & OSMLOOKUP_TYPE_RELATION) == 0U) return 0;
    if (context->has_bbox) return 0;
    if (context->has_id && (context->id_type != OSMLOOKUP_TYPE_RELATION || context->id != relation->id)) return 0;
    if (!tags_match(relation->tags, relation->tag_count, context)) return 0;
    rt_write_cstr(1, "relation ");
    write_signed(relation->id);
    rt_write_cstr(1, " members=");
    rt_write_uint(1, relation->member_count);
    write_tags(relation->tags, relation->tag_count);
    rt_write_char(1, '\n');
    return record_match(context);
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "osm-lookup";
    const char *path;
    OsmLookupContext context;
    PbfStreamCallbacks callbacks;
    char error[PBF_ERROR_CAPACITY];
    int argi;

    if (argc < 2) {
        write_usage(program);
        return 1;
    }
    path = argv[1];
    rt_memset(&context, 0, sizeof(context));
    context.types = OSMLOOKUP_TYPE_ALL;
    context.limit = 20ULL;
    argi = 2;
    while (argi < argc) {
        if (rt_strcmp(argv[argi], "--type") == 0) {
            argi += 1;
            if (argi >= argc || parse_type(argv[argi], &context.types) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--tag") == 0) {
            argi += 1;
            if (argi >= argc || add_tag_filter(&context, argv[argi]) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--name") == 0) {
            argi += 1;
            if (argi >= argc || add_tag_filter(&context, "name") != 0) {
                write_usage(program);
                return 1;
            }
            context.tag_filters[context.tag_filter_count - 1U].has_value = 1;
            context.tag_filters[context.tag_filter_count - 1U].value = argv[argi];
            context.tag_filters[context.tag_filter_count - 1U].value_size = rt_strlen(argv[argi]);
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--id") == 0) {
            argi += 1;
            if (argi >= argc || parse_id_filter(argv[argi], &context.id_type, &context.id) != 0) {
                write_usage(program);
                return 1;
            }
            context.has_id = 1;
            context.types = context.id_type;
            context.limit = 1ULL;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--limit") == 0) {
            argi += 1;
            if (argi >= argc || parse_uint_arg(argv[argi], &context.limit) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--bbox") == 0) {
            argi += 1;
            if (argi >= argc || parse_bbox_arg(argv[argi], &context) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--node-index") == 0) {
            argi += 1;
            if (argi >= argc) {
                write_usage(program);
                return 1;
            }
            context.node_index_path = argv[argi];
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--geometry") == 0) {
            context.show_geometry = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "-h") == 0 || rt_strcmp(argv[argi], "--help") == 0) {
            write_usage(program);
            return 0;
        } else {
            write_usage(program);
            return 1;
        }
    }

    if (((context.show_geometry || context.has_bbox) && (context.types & OSMLOOKUP_TYPE_WAY) != 0U) && context.node_index_path == 0) {
        rt_write_cstr(2, "osm-lookup: way bbox and geometry output require --node-index\n");
        return 1;
    }
    if (context.node_index_path != 0) {
        if (osm_node_index_open(&context.node_index, context.node_index_path, context.error, sizeof(context.error)) != 0) {
            rt_write_cstr(2, "osm-lookup: ");
            rt_write_cstr(2, context.error[0] == '\0' ? "could not open node index" : context.error);
            rt_write_char(2, '\n');
            return 1;
        }
        context.node_index_open = 1;
    }
    rt_memset(&callbacks, 0, sizeof(callbacks));
    callbacks.node = (context.types & OSMLOOKUP_TYPE_NODE) != 0U ? on_node : 0;
    callbacks.way = (context.types & OSMLOOKUP_TYPE_WAY) != 0U ? on_way : 0;
    callbacks.relation = (context.types & OSMLOOKUP_TYPE_RELATION) != 0U ? on_relation : 0;
    error[0] = '\0';
    if (pbf_stream_entities(path, &callbacks, &context, error, sizeof(error)) != 0) {
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        rt_write_cstr(2, "osm-lookup: ");
        rt_write_cstr(2, error[0] == '\0' ? "failed to read PBF" : error);
        rt_write_char(2, '\n');
        return 1;
    }
    if (context.failed) {
        if (context.node_index_open) osm_node_index_close(&context.node_index);
        rt_write_cstr(2, "osm-lookup: ");
        rt_write_cstr(2, context.error[0] == '\0' ? "node index lookup failed" : context.error);
        rt_write_char(2, '\n');
        return 1;
    }
    if (context.node_index_open) osm_node_index_close(&context.node_index);
    return 0;
}