#include "pbf.h"
#include "runtime.h"

static void write_label_uint(const char *label, unsigned long long value) {
    rt_write_cstr(1, label);
    rt_write_uint(1, value);
    rt_write_char(1, '\n');
}

static void write_label_text(const char *label, const char *value) {
    if (value[0] == '\0') {
        return;
    }
    rt_write_cstr(1, label);
    rt_write_cstr(1, value);
    rt_write_char(1, '\n');
}

static void write_feature_list(const char *label, const PbfFeature *features, unsigned int count) {
    unsigned int index;

    rt_write_cstr(1, label);
    if (count == 0U) {
        rt_write_cstr(1, " none\n");
        return;
    }
    rt_write_char(1, '\n');
    for (index = 0U; index < count; ++index) {
        rt_write_cstr(1, "  - ");
        rt_write_cstr(1, features[index].text);
        rt_write_char(1, '\n');
    }
}

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " [--threads N] FILE.osm.pbf\n");
}

static int parse_uint_arg(const char *text, unsigned int *value_out) {
    unsigned long long value = 0ULL;
    size_t index = 0U;

    if (text == 0 || text[0] == '\0') {
        return -1;
    }
    while (text[index] != '\0') {
        if (text[index] < '0' || text[index] > '9') {
            return -1;
        }
        value = value * 10ULL + (unsigned long long)(text[index] - '0');
        if (value > 4294967295ULL) {
            return -1;
        }
        index += 1U;
    }
    *value_out = (unsigned int)value;
    return 0;
}

int main(int argc, char **argv) {
    PbfSummary summary;
    char error[PBF_ERROR_CAPACITY];
    const char *program = argc > 0 ? argv[0] : "pbf-info";
    const char *path;
    unsigned int threads = 1U;
    int argi = 1;

    while (argi < argc && argv[argi][0] == '-') {
        if (rt_strcmp(argv[argi], "--threads") == 0) {
            argi += 1;
            if (argi >= argc || parse_uint_arg(argv[argi], &threads) != 0 || threads == 0U) {
                write_usage(program);
                return 1;
            }
            argi += 1;
        } else if (rt_strcmp(argv[argi], "-h") == 0 || rt_strcmp(argv[argi], "--help") == 0) {
            write_usage(program);
            return 0;
        } else {
            write_usage(program);
            return 1;
        }
    }
    if (argi + 1 != argc) {
        write_usage(program);
        return 1;
    }
    path = argv[argi];

    error[0] = '\0';
    if (pbf_read_summary_parallel(path, threads, &summary, error, sizeof(error)) != 0) {
        rt_write_cstr(2, "pbf-info: ");
        rt_write_cstr(2, error[0] == '\0' ? "failed to read PBF" : error);
        rt_write_char(2, '\n');
        return 1;
    }

    write_label_text("writing_program: ", summary.writing_program);
    write_label_text("source: ", summary.source);
    write_feature_list("required_features:", summary.required_features, summary.required_feature_count);
    write_feature_list("optional_features:", summary.optional_features, summary.optional_feature_count);
    write_label_uint("fileblocks: ", summary.fileblocks);
    write_label_uint("header_blocks: ", summary.header_blocks);
    write_label_uint("data_blocks: ", summary.data_blocks);
    write_label_uint("other_blocks: ", summary.other_blocks);
    write_label_uint("raw_blobs: ", summary.raw_blobs);
    write_label_uint("zlib_blobs: ", summary.zlib_blobs);
    write_label_uint("compressed_blob_bytes: ", summary.compressed_bytes);
    write_label_uint("uncompressed_blob_bytes: ", summary.uncompressed_bytes);
    write_label_uint("primitive_blocks: ", summary.primitive_blocks);
    write_label_uint("primitive_groups: ", summary.primitive_groups);
    write_label_uint("dense_node_groups: ", summary.dense_node_groups);
    write_label_uint("nodes: ", summary.nodes);
    write_label_uint("ways: ", summary.ways);
    write_label_uint("relations: ", summary.relations);
    return 0;
}