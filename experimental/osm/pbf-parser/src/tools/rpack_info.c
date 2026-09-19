#include "osmrpack.h"

#include "platform.h"
#include "runtime.h"

#define OSMRPACK_V2_HEADER_SIZE 256U

static const unsigned char osmrpack_v2_magic[8] = { 'O', 'S', 'M', 'R', 'P', 'K', '0', '2' };

static void write_usage(const char *program) {
    rt_write_cstr(2, "Usage: ");
    rt_write_cstr(2, program);
    rt_write_cstr(2, " FILE.rpack\n");
}

static unsigned int read_u32_le(const unsigned char *in) {
    return (unsigned int)in[0] |
           ((unsigned int)in[1] << 8U) |
           ((unsigned int)in[2] << 16U) |
           ((unsigned int)in[3] << 24U);
}

static unsigned long long read_u64_le(const unsigned char *in) {
    unsigned long long value = 0ULL;
    unsigned int index;
    for (index = 0U; index < 8U; ++index) value |= ((unsigned long long)in[index]) << (index * 8U);
    return value;
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

static void write_field_u32(const char *name, unsigned int value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    rt_write_uint(1, value);
    rt_write_char(1, '\n');
}

static void write_field_u64(const char *name, unsigned long long value) {
    rt_write_cstr(1, name);
    rt_write_cstr(1, ": ");
    rt_write_uint(1, value);
    rt_write_char(1, '\n');
}

static int try_write_v2_info(const char *path, int *handled) {
    unsigned char header[OSMRPACK_V2_HEADER_SIZE];
    int fd;
    *handled = 0;
    fd = platform_open_read(path);
    if (fd < 0) return -1;
    if (read_exact(fd, header, sizeof(header)) != 0) {
        (void)platform_close(fd);
        return -1;
    }
    (void)platform_close(fd);
    if (memcmp(header, osmrpack_v2_magic, sizeof(osmrpack_v2_magic)) != 0) return 0;
    *handled = 1;
    rt_write_cstr(1, "magic: OSMRPK02\n");
    write_field_u32("version", read_u32_le(header + 8U));
    write_field_u32("header_size", read_u32_le(header + 12U));
    write_field_u32("flags", read_u32_le(header + 16U));
    write_field_u32("tile_zoom", read_u32_le(header + 20U));
    write_field_u32("tile_halo", read_u32_le(header + 24U));
    write_field_u32("layer_count", read_u32_le(header + 28U));
    write_field_u32("place_record_size", read_u32_le(header + 32U));
    write_field_u32("tile_record_size", read_u32_le(header + 36U));
    write_field_u32("feature_record_size", read_u32_le(header + 40U));
    write_field_u64("place_count", read_u64_le(header + 48U));
    write_field_u64("tile_count", read_u64_le(header + 56U));
    write_field_u64("place_directory_offset", read_u64_le(header + 64U));
    write_field_u64("place_directory_size", read_u64_le(header + 72U));
    write_field_u64("tile_directory_offset", read_u64_le(header + 80U));
    write_field_u64("tile_directory_size", read_u64_le(header + 88U));
    write_field_u64("tile_range_index_offset", read_u64_le(header + 96U));
    write_field_u64("tile_range_index_size", read_u64_le(header + 104U));
    write_field_u64("tile_payload_offset", read_u64_le(header + 112U));
    write_field_u64("tile_payload_size", read_u64_le(header + 120U));
    write_field_u64("string_table_offset", read_u64_le(header + 128U));
    write_field_u64("string_table_size", read_u64_le(header + 136U));
    write_field_u64("source_nodes", read_u64_le(header + 144U));
    write_field_u64("source_ways", read_u64_le(header + 152U));
    write_field_u64("source_relations", read_u64_le(header + 160U));
    return 0;
}

int main(int argc, char **argv) {
    const char *program = argc > 0 ? argv[0] : "rpack-info";
    OsmrPackHeader header;
    char error[OSMRPACK_ERROR_CAPACITY];
    int handled_v2 = 0;

    if (argc == 2 && (rt_strcmp(argv[1], "-h") == 0 || rt_strcmp(argv[1], "--help") == 0)) {
        write_usage(program);
        return 0;
    }
    if (argc != 2) {
        write_usage(program);
        return 1;
    }
    if (try_write_v2_info(argv[1], &handled_v2) == 0 && handled_v2) return 0;
    error[0] = '\0';
    if (osmrpack_read_header(argv[1], &header, error, sizeof(error)) != 0) {
        rt_write_cstr(2, "rpack-info: ");
        rt_write_cstr(2, error[0] == '\0' ? "could not read pack" : error);
        rt_write_char(2, '\n');
        return 1;
    }
    osmrpack_write_header_text(1, &header);
    return 0;
}
