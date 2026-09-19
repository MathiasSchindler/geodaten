#include "pbf.h"
#include "platform.h"
#include "runtime.h"

#define OSM_ADDRESSES_BUFFER_SIZE 131072U

typedef struct {
    PbfText state;
    PbfText city;
    PbfText suburb;
    PbfText street;
    PbfText housenumber;
    PbfText postcode;
    int has_state;
    int has_city;
    int has_suburb;
    int has_street;
    int has_housenumber;
    int has_postcode;
} OsmAddressFields;

typedef struct {
    int fd;
    unsigned char *data;
    size_t capacity;
    size_t used;
} OsmAddressWriter;

typedef struct {
    OsmAddressWriter writer;
    int include_incomplete;
    int write_header;
    unsigned long long limit;
    unsigned long long written;
    unsigned long long node_addresses;
    unsigned long long way_addresses;
    unsigned long long relation_addresses;
    int failed;
} OsmAddressesContext;

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.osm.pbf OUT.tsv [--include-incomplete] [--no-header] [--limit N]\n");
}

static int parse_uint_arg(const char *text, unsigned long long *value_out) {
    unsigned long long value = 0ULL;
    size_t index = 0U;

    if (text == 0 || text[0] == '\0') return -1;
    while (text[index] != '\0') {
        unsigned long long previous;
        if (text[index] < '0' || text[index] > '9') return -1;
        previous = value;
        value = value * 10ULL + (unsigned long long)(text[index] - '0');
        if (value < previous) return -1;
        index += 1U;
    }
    *value_out = value;
    return 0;
}

static int text_equals_cstr(PbfText text, const char *value) {
    size_t value_size = rt_strlen(value);

    return text.size == value_size && memcmp(text.data, value, value_size) == 0;
}

static void find_address_fields(const PbfTag *tags, unsigned int tag_count, OsmAddressFields *fields) {
    unsigned int index;

    rt_memset(fields, 0, sizeof(*fields));
    for (index = 0U; index < tag_count; ++index) {
        if (!fields->has_state && text_equals_cstr(tags[index].key, "addr:state")) {
            fields->state = tags[index].value;
            fields->has_state = 1;
        } else if (!fields->has_city && text_equals_cstr(tags[index].key, "addr:city")) {
            fields->city = tags[index].value;
            fields->has_city = 1;
        } else if (!fields->has_suburb && text_equals_cstr(tags[index].key, "addr:suburb")) {
            fields->suburb = tags[index].value;
            fields->has_suburb = 1;
        } else if (!fields->has_street && text_equals_cstr(tags[index].key, "addr:street")) {
            fields->street = tags[index].value;
            fields->has_street = 1;
        } else if (!fields->has_housenumber && text_equals_cstr(tags[index].key, "addr:housenumber")) {
            fields->housenumber = tags[index].value;
            fields->has_housenumber = 1;
        } else if (!fields->has_postcode && text_equals_cstr(tags[index].key, "addr:postcode")) {
            fields->postcode = tags[index].value;
            fields->has_postcode = 1;
        }
    }
}

static int address_fields_match(const OsmAddressesContext *context, const OsmAddressFields *fields) {
    if (context->include_incomplete) {
        return fields->has_state || fields->has_city || fields->has_suburb || fields->has_street || fields->has_housenumber || fields->has_postcode;
    }
    return fields->has_street && fields->has_housenumber && fields->has_postcode;
}

static int writer_flush(OsmAddressWriter *writer) {
    if (writer->used == 0U) return 0;
    if (rt_write_all(writer->fd, writer->data, writer->used) != 0) return -1;
    writer->used = 0U;
    return 0;
}

static int writer_write(OsmAddressWriter *writer, const void *data, size_t size) {
    if (size == 0U) return 0;
    if (size > writer->capacity) {
        if (writer_flush(writer) != 0) return -1;
        return rt_write_all(writer->fd, data, size);
    }
    if (writer->used + size > writer->capacity && writer_flush(writer) != 0) return -1;
    memcpy(writer->data + writer->used, data, size);
    writer->used += size;
    return 0;
}

static int writer_char(OsmAddressWriter *writer, char ch) {
    return writer_write(writer, &ch, 1U);
}

static int writer_cstr(OsmAddressWriter *writer, const char *text) {
    return writer_write(writer, text, rt_strlen(text));
}

static int writer_uint(OsmAddressWriter *writer, unsigned long long value) {
    char buffer[32];

    rt_unsigned_to_string(value, buffer, sizeof(buffer));
    return writer_cstr(writer, buffer);
}

static int writer_tsv_text(OsmAddressWriter *writer, PbfText text) {
    size_t start = 0U;
    size_t index;

    for (index = 0U; index < text.size; ++index) {
        char ch = text.data[index];
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            if (writer_write(writer, text.data + start, index - start) != 0 || writer_char(writer, ' ') != 0) return -1;
            start = index + 1U;
        }
    }
    return writer_write(writer, text.data + start, text.size - start);
}

static int writer_coord(OsmAddressWriter *writer, long long nano) {
    unsigned long long value;
    unsigned long long whole;
    unsigned long long fraction;
    unsigned long long divisor = 100000000ULL;

    if (nano < 0) {
        if (writer_char(writer, '-') != 0) return -1;
        value = (unsigned long long)(-nano);
    } else {
        value = (unsigned long long)nano;
    }
    whole = value / 1000000000ULL;
    fraction = value % 1000000000ULL;
    if (writer_uint(writer, whole) != 0 || writer_char(writer, '.') != 0) return -1;
    while (divisor != 0ULL) {
        if (writer_char(writer, (char)('0' + (fraction / divisor) % 10ULL)) != 0) return -1;
        divisor /= 10ULL;
    }
    return 0;
}

static int writer_entity_id(OsmAddressWriter *writer, long long id) {
    if (id < 0) {
        return writer_char(writer, '-') != 0 || writer_uint(writer, (unsigned long long)(-id)) != 0 ? -1 : 0;
    }
    return writer_uint(writer, (unsigned long long)id);
}

static int write_header(OsmAddressWriter *writer) {
    return writer_cstr(writer, "type\tid\tlat\tlon\tstate\tcity\tsuburb\tstreet\thousenumber\tpostcode\n");
}

static int emit_address(OsmAddressesContext *context, const char *type, long long id, int has_coord, long long lat_nano, long long lon_nano, const OsmAddressFields *fields) {
    OsmAddressWriter *writer = &context->writer;

    if (!address_fields_match(context, fields)) return 0;
    if (context->limit != 0ULL && context->written >= context->limit) return 1;
    if (writer_cstr(writer, type) != 0 || writer_char(writer, '\t') != 0 || writer_entity_id(writer, id) != 0 || writer_char(writer, '\t') != 0) goto fail;
    if (has_coord && writer_coord(writer, lat_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0) goto fail;
    if (has_coord && writer_coord(writer, lon_nano) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, fields->state) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, fields->city) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, fields->suburb) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, fields->street) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, fields->housenumber) != 0) goto fail;
    if (writer_char(writer, '\t') != 0 || writer_tsv_text(writer, fields->postcode) != 0 || writer_char(writer, '\n') != 0) goto fail;
    context->written += 1ULL;
    return context->limit != 0ULL && context->written >= context->limit;

fail:
    context->failed = 1;
    return 1;
}

static int on_node(void *user, const PbfNode *node) {
    OsmAddressesContext *context = (OsmAddressesContext *)user;
    OsmAddressFields fields;
    unsigned long long previous = context->written;
    int stop;

    find_address_fields(node->tags, node->tag_count, &fields);
    stop = emit_address(context, "node", node->id, 1, node->lat_nano, node->lon_nano, &fields);
    if (context->written != previous) context->node_addresses += 1ULL;
    return stop;
}

static int on_way_tags(void *user, long long id, const PbfTag *tags, unsigned int tag_count) {
    OsmAddressesContext *context = (OsmAddressesContext *)user;
    OsmAddressFields fields;
    unsigned long long previous = context->written;

    find_address_fields(tags, tag_count, &fields);
    (void)emit_address(context, "way", id, 0, 0, 0, &fields);
    if (context->written != previous) context->way_addresses += 1ULL;
    return 0;
}

static int on_relation_tags(void *user, long long id, const PbfTag *tags, unsigned int tag_count) {
    OsmAddressesContext *context = (OsmAddressesContext *)user;
    OsmAddressFields fields;
    unsigned long long previous = context->written;

    find_address_fields(tags, tag_count, &fields);
    (void)emit_address(context, "relation", id, 0, 0, 0, &fields);
    if (context->written != previous) context->relation_addresses += 1ULL;
    return 0;
}

static int output_is_stdout(const char *path) {
    return path[0] == '-' && path[1] == '\0';
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "osm-addresses";
    const char *pbf_path;
    const char *out_path;
    OsmAddressesContext context;
    PbfStreamCallbacks callbacks;
    char error[PBF_ERROR_CAPACITY];
    int argi;
    int output_stdout;
    int stats_fd;

    if (argc < 3 || rt_strcmp(argv[1], "-h") == 0 || rt_strcmp(argv[1], "--help") == 0) {
        write_usage(program);
        return argc == 2 ? 0 : 1;
    }
    pbf_path = argv[1];
    out_path = argv[2];
    rt_memset(&context, 0, sizeof(context));
    context.write_header = 1;
    argi = 3;
    while (argi < argc) {
        if (rt_strcmp(argv[argi], "--include-incomplete") == 0) {
            context.include_incomplete = 1;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--no-header") == 0) {
            context.write_header = 0;
            argi += 1;
        } else if (rt_strcmp(argv[argi], "--limit") == 0) {
            argi += 1;
            if (argi >= argc || parse_uint_arg(argv[argi], &context.limit) != 0) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else {
            write_usage(program);
            return 1;
        }
    }
    output_stdout = output_is_stdout(out_path);
    context.writer.fd = output_stdout ? 1 : platform_open_write(out_path, 0644U);
    if (context.writer.fd < 0) {
        rt_write_cstr(2, "osm-addresses: could not open output file\n");
        return 1;
    }
    context.writer.capacity = OSM_ADDRESSES_BUFFER_SIZE;
    context.writer.data = (unsigned char *)rt_malloc(context.writer.capacity);
    if (context.writer.data == 0) {
        rt_write_cstr(2, "osm-addresses: out of memory\n");
        if (!output_stdout) (void)platform_close(context.writer.fd);
        return 1;
    }
    if (context.write_header && write_header(&context.writer) != 0) {
        rt_write_cstr(2, "osm-addresses: could not write output header\n");
        rt_free(context.writer.data);
        if (!output_stdout) (void)platform_close(context.writer.fd);
        return 1;
    }
    rt_memset(&callbacks, 0, sizeof(callbacks));
    callbacks.node = on_node;
    callbacks.way_tags = on_way_tags;
    callbacks.relation_tags = on_relation_tags;
    error[0] = '\0';
    if (pbf_stream_entities(pbf_path, &callbacks, &context, error, sizeof(error)) != 0 || context.failed || writer_flush(&context.writer) != 0) {
        rt_write_cstr(2, "osm-addresses: ");
        rt_write_cstr(2, context.failed ? "could not write output" : (error[0] == '\0' ? "failed to parse PBF" : error));
        rt_write_char(2, '\n');
        rt_free(context.writer.data);
        if (!output_stdout) (void)platform_close(context.writer.fd);
        return 1;
    }
    rt_free(context.writer.data);
    if (!output_stdout && platform_close(context.writer.fd) != 0) {
        rt_write_cstr(2, "osm-addresses: could not close output file\n");
        return 1;
    }
    stats_fd = output_stdout ? 2 : 1;
    rt_write_cstr(stats_fd, "addresses_written: ");
    rt_write_uint(stats_fd, context.written);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "node_addresses: ");
    rt_write_uint(stats_fd, context.node_addresses);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "way_addresses: ");
    rt_write_uint(stats_fd, context.way_addresses);
    rt_write_char(stats_fd, '\n');
    rt_write_cstr(stats_fd, "relation_addresses: ");
    rt_write_uint(stats_fd, context.relation_addresses);
    rt_write_char(stats_fd, '\n');
    return 0;
}