#include "osmrpack.h"

#include "platform.h"
#include "runtime.h"

static const unsigned char osmrpack_magic[8] = { 'O', 'S', 'M', 'R', 'P', 'K', '0', '1' };

static void osmrpack_set_error(char *error, size_t error_capacity, const char *message) {
    if (error != 0 && error_capacity != 0U) rt_copy_string(error, error_capacity, message);
}

static void osmrpack_write_u32(unsigned char *out, unsigned int value) {
    out[0] = (unsigned char)(value & 0xffU);
    out[1] = (unsigned char)((value >> 8U) & 0xffU);
    out[2] = (unsigned char)((value >> 16U) & 0xffU);
    out[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static void osmrpack_write_u64(unsigned char *out, unsigned long long value) {
    unsigned int index;

    for (index = 0U; index < 8U; ++index) out[index] = (unsigned char)((value >> (index * 8U)) & 0xffU);
}

static void osmrpack_write_i64(unsigned char *out, long long value) {
    osmrpack_write_u64(out, (unsigned long long)value);
}

static unsigned int osmrpack_read_u32(const unsigned char *in) {
    return (unsigned int)in[0] |
           ((unsigned int)in[1] << 8U) |
           ((unsigned int)in[2] << 16U) |
           ((unsigned int)in[3] << 24U);
}

static unsigned long long osmrpack_read_u64(const unsigned char *in) {
    unsigned long long value = 0ULL;
    unsigned int index;

    for (index = 0U; index < 8U; ++index) value |= ((unsigned long long)in[index]) << (index * 8U);
    return value;
}

static long long osmrpack_read_i64(const unsigned char *in) {
    return (long long)osmrpack_read_u64(in);
}

static int osmrpack_read_exact(int fd, void *buffer, size_t count) {
    unsigned char *cursor = (unsigned char *)buffer;
    size_t offset = 0U;

    while (offset < count) {
        long bytes = platform_read(fd, cursor + offset, count - offset);
        if (bytes <= 0) return -1;
        offset += (size_t)bytes;
    }
    return 0;
}

static void osmrpack_header_to_bytes(const OsmrPackHeader *header, unsigned char out[OSMRPACK_HEADER_SIZE]) {
    rt_memset(out, 0, OSMRPACK_HEADER_SIZE);
    memcpy(out, osmrpack_magic, sizeof(osmrpack_magic));
    osmrpack_write_u32(out + 8U, header->version);
    osmrpack_write_u32(out + 12U, header->header_size);
    osmrpack_write_u32(out + 16U, header->tile_record_size);
    osmrpack_write_u32(out + 20U, header->tile_zoom);
    osmrpack_write_u32(out + 24U, header->flags);
    osmrpack_write_u32(out + 28U, header->layer_count);
    osmrpack_write_u64(out + 32U, header->tile_count);
    osmrpack_write_u64(out + 40U, header->tile_directory_offset);
    osmrpack_write_u64(out + 48U, header->feature_data_offset);
    osmrpack_write_u64(out + 56U, header->feature_data_size);
    osmrpack_write_u64(out + 64U, header->string_table_offset);
    osmrpack_write_u64(out + 72U, header->string_table_size);
    osmrpack_write_u64(out + 80U, header->source_fileblocks);
    osmrpack_write_u64(out + 88U, header->source_data_blocks);
    osmrpack_write_u64(out + 96U, header->source_nodes);
    osmrpack_write_u64(out + 104U, header->source_ways);
    osmrpack_write_u64(out + 112U, header->source_relations);
    osmrpack_write_u64(out + 120U, header->source_compressed_bytes);
    osmrpack_write_u64(out + 128U, header->source_uncompressed_bytes);
}

static void osmrpack_tile_record_to_bytes(const OsmrPackTileRecord *record, unsigned char out[OSMRPACK_TILE_RECORD_SIZE]) {
    rt_memset(out, 0, OSMRPACK_TILE_RECORD_SIZE);
    osmrpack_write_u64(out + 0U, record->tile_id);
    osmrpack_write_u32(out + 8U, record->z);
    osmrpack_write_u32(out + 12U, record->x);
    osmrpack_write_u32(out + 16U, record->y);
    osmrpack_write_u32(out + 20U, record->feature_count);
    osmrpack_write_u32(out + 24U, record->layer_mask);
    osmrpack_write_u64(out + 32U, record->payload_offset);
    osmrpack_write_u64(out + 40U, record->payload_size);
    osmrpack_write_i64(out + 48U, record->min_lon_nano);
    osmrpack_write_i64(out + 56U, record->min_lat_nano);
    osmrpack_write_i64(out + 64U, record->max_lon_nano);
    osmrpack_write_i64(out + 72U, record->max_lat_nano);
}

static void osmrpack_tile_record_from_bytes(const unsigned char in[OSMRPACK_TILE_RECORD_SIZE], OsmrPackTileRecord *record) {
    rt_memset(record, 0, sizeof(*record));
    record->tile_id = osmrpack_read_u64(in + 0U);
    record->z = osmrpack_read_u32(in + 8U);
    record->x = osmrpack_read_u32(in + 12U);
    record->y = osmrpack_read_u32(in + 16U);
    record->feature_count = osmrpack_read_u32(in + 20U);
    record->layer_mask = osmrpack_read_u32(in + 24U);
    record->payload_offset = osmrpack_read_u64(in + 32U);
    record->payload_size = osmrpack_read_u64(in + 40U);
    record->min_lon_nano = osmrpack_read_i64(in + 48U);
    record->min_lat_nano = osmrpack_read_i64(in + 56U);
    record->max_lon_nano = osmrpack_read_i64(in + 64U);
    record->max_lat_nano = osmrpack_read_i64(in + 72U);
}

static void osmrpack_header_from_bytes(const unsigned char in[OSMRPACK_HEADER_SIZE], OsmrPackHeader *header) {
    rt_memset(header, 0, sizeof(*header));
    header->version = osmrpack_read_u32(in + 8U);
    header->header_size = osmrpack_read_u32(in + 12U);
    header->tile_record_size = osmrpack_read_u32(in + 16U);
    header->tile_zoom = osmrpack_read_u32(in + 20U);
    header->flags = osmrpack_read_u32(in + 24U);
    header->layer_count = osmrpack_read_u32(in + 28U);
    header->tile_count = osmrpack_read_u64(in + 32U);
    header->tile_directory_offset = osmrpack_read_u64(in + 40U);
    header->feature_data_offset = osmrpack_read_u64(in + 48U);
    header->feature_data_size = osmrpack_read_u64(in + 56U);
    header->string_table_offset = osmrpack_read_u64(in + 64U);
    header->string_table_size = osmrpack_read_u64(in + 72U);
    header->source_fileblocks = osmrpack_read_u64(in + 80U);
    header->source_data_blocks = osmrpack_read_u64(in + 88U);
    header->source_nodes = osmrpack_read_u64(in + 96U);
    header->source_ways = osmrpack_read_u64(in + 104U);
    header->source_relations = osmrpack_read_u64(in + 112U);
    header->source_compressed_bytes = osmrpack_read_u64(in + 120U);
    header->source_uncompressed_bytes = osmrpack_read_u64(in + 128U);
}

void osmrpack_header_init(OsmrPackHeader *header, unsigned int tile_zoom, const PbfSummary *summary) {
    rt_memset(header, 0, sizeof(*header));
    header->version = OSMRPACK_VERSION;
    header->header_size = OSMRPACK_HEADER_SIZE;
    header->tile_record_size = OSMRPACK_TILE_RECORD_SIZE;
    header->tile_zoom = tile_zoom;
    header->flags = OSMRPACK_FLAG_EMPTY_GEOMETRY;
    header->layer_count = 0U;
    header->tile_count = 0ULL;
    header->tile_directory_offset = OSMRPACK_HEADER_SIZE;
    header->feature_data_offset = OSMRPACK_HEADER_SIZE;
    header->feature_data_size = 0ULL;
    header->string_table_offset = OSMRPACK_HEADER_SIZE;
    header->string_table_size = 0ULL;
    if (summary != 0) {
        header->source_fileblocks = summary->fileblocks;
        header->source_data_blocks = summary->data_blocks;
        header->source_nodes = summary->nodes;
        header->source_ways = summary->ways;
        header->source_relations = summary->relations;
        header->source_compressed_bytes = summary->compressed_bytes;
        header->source_uncompressed_bytes = summary->uncompressed_bytes;
    }
}

int osmrpack_validate_header(const OsmrPackHeader *header, char *error, size_t error_capacity) {
    if (header->version != OSMRPACK_VERSION) {
        osmrpack_set_error(error, error_capacity, "unsupported osmrpack version");
        return -1;
    }
    if (header->header_size != OSMRPACK_HEADER_SIZE || header->tile_record_size != OSMRPACK_TILE_RECORD_SIZE) {
        osmrpack_set_error(error, error_capacity, "unsupported osmrpack record layout");
        return -1;
    }
    if (header->tile_zoom > OSMRPACK_MAX_TILE_ZOOM) {
        osmrpack_set_error(error, error_capacity, "invalid osmrpack tile zoom");
        return -1;
    }
    return 0;
}

int osmrpack_write_empty(const char *path, unsigned int tile_zoom, const PbfSummary *summary, char *error, size_t error_capacity) {
    OsmrPackHeader header;
    unsigned char data[OSMRPACK_HEADER_SIZE];
    int fd;

    if (tile_zoom > OSMRPACK_MAX_TILE_ZOOM) {
        osmrpack_set_error(error, error_capacity, "tile zoom is too large");
        return -1;
    }
    osmrpack_header_init(&header, tile_zoom, summary);
    osmrpack_header_to_bytes(&header, data);
    fd = platform_open_write(path, 0644U);
    if (fd < 0) {
        osmrpack_set_error(error, error_capacity, "could not open osmrpack output");
        return -1;
    }
    if (rt_write_all(fd, data, sizeof(data)) != 0) {
        (void)platform_close(fd);
        osmrpack_set_error(error, error_capacity, "could not write osmrpack header");
        return -1;
    }
    if (platform_close(fd) != 0) {
        osmrpack_set_error(error, error_capacity, "could not close osmrpack output");
        return -1;
    }
    return 0;
}

int osmrpack_write_header_fd(int fd, const OsmrPackHeader *header) {
    unsigned char data[OSMRPACK_HEADER_SIZE];

    osmrpack_header_to_bytes(header, data);
    return rt_write_all(fd, data, sizeof(data));
}

int osmrpack_write_tile_record_fd(int fd, const OsmrPackTileRecord *record) {
    unsigned char data[OSMRPACK_TILE_RECORD_SIZE];

    osmrpack_tile_record_to_bytes(record, data);
    return rt_write_all(fd, data, sizeof(data));
}

int osmrpack_read_tile_record_fd(int fd, OsmrPackTileRecord *record) {
    unsigned char data[OSMRPACK_TILE_RECORD_SIZE];

    if (osmrpack_read_exact(fd, data, sizeof(data)) != 0) return -1;
    osmrpack_tile_record_from_bytes(data, record);
    return 0;
}

int osmrpack_read_header(const char *path, OsmrPackHeader *header_out, char *error, size_t error_capacity) {
    unsigned char data[OSMRPACK_HEADER_SIZE];
    int fd;

    fd = platform_open_read(path);
    if (fd < 0) {
        osmrpack_set_error(error, error_capacity, "could not open osmrpack file");
        return -1;
    }
    if (osmrpack_read_exact(fd, data, sizeof(data)) != 0) {
        (void)platform_close(fd);
        osmrpack_set_error(error, error_capacity, "could not read osmrpack header");
        return -1;
    }
    (void)platform_close(fd);
    if (memcmp(data, osmrpack_magic, sizeof(osmrpack_magic)) != 0) {
        osmrpack_set_error(error, error_capacity, "invalid osmrpack magic");
        return -1;
    }
    osmrpack_header_from_bytes(data, header_out);
    return osmrpack_validate_header(header_out, error, error_capacity);
}

void osmrpack_write_header_text(int fd, const OsmrPackHeader *header) {
    rt_write_cstr(fd, "version: ");
    rt_write_uint(fd, header->version);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "tile_zoom: ");
    rt_write_uint(fd, header->tile_zoom);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "tile_count: ");
    rt_write_uint(fd, header->tile_count);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "layer_count: ");
    rt_write_uint(fd, header->layer_count);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "flags: ");
    rt_write_uint(fd, header->flags);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "feature_data_size: ");
    rt_write_uint(fd, header->feature_data_size);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "string_table_size: ");
    rt_write_uint(fd, header->string_table_size);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "source_fileblocks: ");
    rt_write_uint(fd, header->source_fileblocks);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "source_data_blocks: ");
    rt_write_uint(fd, header->source_data_blocks);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "source_nodes: ");
    rt_write_uint(fd, header->source_nodes);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "source_ways: ");
    rt_write_uint(fd, header->source_ways);
    rt_write_char(fd, '\n');
    rt_write_cstr(fd, "source_relations: ");
    rt_write_uint(fd, header->source_relations);
    rt_write_char(fd, '\n');
}
