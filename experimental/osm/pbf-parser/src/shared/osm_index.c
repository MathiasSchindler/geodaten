#include "osm_index.h"

#include "pbf.h"
#include "platform.h"
#include "runtime.h"

#define OSM_NODE_INDEX_HEADER_SIZE 32U
#define OSM_NODE_INDEX_RECORD_SIZE 24U
#define OSM_WAY_INDEX_HEADER_SIZE 40U
#define OSM_WAY_INDEX_RECORD_SIZE 24U
#define OSM_RELATION_INDEX_HEADER_SIZE 48U
#define OSM_RELATION_INDEX_RECORD_SIZE 40U
#define OSM_SPATIAL_INDEX_HEADER_SIZE 32U
#define OSM_SPATIAL_INDEX_RECORD_SIZE 40U
#define OSM_INDEX_WRITE_BUFFER_SIZE (1024U * 1024U)
#define OSM_NODE_INDEX_PROGRESS_INTERVAL 10000000ULL
#define OSM_WAY_INDEX_PROGRESS_INTERVAL 1000000ULL
#define OSM_RELATION_INDEX_PROGRESS_INTERVAL 100000ULL

static const unsigned char osm_node_index_magic[8] = { 'O', 'S', 'M', 'N', 'I', 'D', 'X', '1' };
static const unsigned char osm_way_index_magic[8] = { 'O', 'S', 'M', 'W', 'I', 'D', 'X', '1' };
static const unsigned char osm_relation_index_magic[8] = { 'O', 'S', 'M', 'R', 'I', 'D', 'X', '1' };
static const unsigned char osm_spatial_index_magic[8] = { 'O', 'S', 'M', 'S', 'I', 'D', 'X', '1' };

typedef struct {
    int fd;
    unsigned char *buffer;
    size_t used;
    size_t capacity;
    int failed;
} OsmIndexWriter;

typedef struct {
    OsmIndexWriter writer;
    unsigned long long count;
    long long last_id;
    int has_last_id;
    int failed;
    unsigned int flags;
    char *error;
    size_t error_capacity;
} OsmNodeIndexBuildContext;

typedef struct {
    OsmIndexWriter record_writer;
    OsmIndexWriter ref_writer;
    unsigned long long count;
    unsigned long long ref_count;
    long long last_id;
    int has_last_id;
    int failed;
    unsigned int flags;
    char *error;
    size_t error_capacity;
} OsmWayIndexBuildContext;

typedef struct {
    OsmNodeIndexBuildContext node;
    OsmWayIndexBuildContext way;
} OsmIndexBuildContext;

typedef struct {
    OsmRelationIndexRecord *records;
    long long *members;
    char *names;
    unsigned long long count;
    unsigned long long member_count;
    unsigned long long name_size;
    unsigned long long record_capacity;
    unsigned long long member_capacity;
    unsigned long long name_capacity;
    unsigned int flags;
    int failed;
    char *error;
    size_t error_capacity;
} OsmRelationIndexBuildContext;

static void osm_index_set_error(char *error, size_t error_capacity, const char *message) {
    if (error != 0 && error_capacity != 0U) {
        rt_copy_string(error, error_capacity, message);
    }
}

static void osm_index_write_progress_label(const char *label, unsigned long long count) {
    rt_write_cstr(2, label);
    rt_write_uint(2, count);
    rt_write_char(2, '\n');
}

static int osm_index_writer_open(OsmIndexWriter *writer, const char *path, unsigned int mode, char *error, size_t error_capacity, const char *message) {
    rt_memset(writer, 0, sizeof(*writer));
    writer->fd = platform_open_write(path, mode);
    if (writer->fd < 0) {
        osm_index_set_error(error, error_capacity, message);
        return -1;
    }
    writer->capacity = OSM_INDEX_WRITE_BUFFER_SIZE;
    writer->buffer = (unsigned char *)rt_malloc(writer->capacity);
    if (writer->buffer == 0) {
        (void)platform_close(writer->fd);
        writer->fd = -1;
        osm_index_set_error(error, error_capacity, "out of memory while preparing index writer");
        return -1;
    }
    return 0;
}

static int osm_index_writer_flush(OsmIndexWriter *writer) {
    if (writer->failed) return -1;
    if (writer->used != 0U) {
        if (rt_write_all(writer->fd, writer->buffer, writer->used) != 0) {
            writer->failed = 1;
            return -1;
        }
        writer->used = 0U;
    }
    return 0;
}

static int osm_index_writer_write(OsmIndexWriter *writer, const void *data, size_t size) {
    const unsigned char *cursor = (const unsigned char *)data;
    size_t remaining = size;

    if (writer->failed) return -1;
    while (remaining != 0U) {
        size_t available = writer->capacity - writer->used;
        size_t chunk_size;

        if (available == 0U) {
            if (osm_index_writer_flush(writer) != 0) return -1;
            available = writer->capacity;
        }
        if (writer->used == 0U && remaining >= writer->capacity) {
            if (rt_write_all(writer->fd, cursor, remaining) != 0) {
                writer->failed = 1;
                return -1;
            }
            return 0;
        }
        chunk_size = remaining < available ? remaining : available;
        memcpy(writer->buffer + writer->used, cursor, chunk_size);
        writer->used += chunk_size;
        cursor += chunk_size;
        remaining -= chunk_size;
    }
    return 0;
}

static int osm_index_writer_seek(OsmIndexWriter *writer, long long offset, int whence) {
    if (osm_index_writer_flush(writer) != 0) return -1;
    return platform_seek(writer->fd, offset, whence) == offset ? 0 : -1;
}

static int osm_index_writer_close(OsmIndexWriter *writer) {
    int result = 0;

    if (writer->fd >= 0) {
        if (osm_index_writer_flush(writer) != 0) result = -1;
        if (platform_close(writer->fd) != 0) result = -1;
    }
    rt_free(writer->buffer);
    writer->buffer = 0;
    writer->used = 0U;
    writer->capacity = 0U;
    writer->fd = -1;
    return result;
}

static void osm_index_writer_discard(OsmIndexWriter *writer) {
    if (writer->fd >= 0) {
        (void)platform_close(writer->fd);
    }
    rt_free(writer->buffer);
    writer->buffer = 0;
    writer->used = 0U;
    writer->capacity = 0U;
    writer->fd = -1;
    writer->failed = 0;
}

static void osm_index_write_u32(unsigned char *out, unsigned int value) {
    out[0] = (unsigned char)(value & 0xffU);
    out[1] = (unsigned char)((value >> 8U) & 0xffU);
    out[2] = (unsigned char)((value >> 16U) & 0xffU);
    out[3] = (unsigned char)((value >> 24U) & 0xffU);
}

static void osm_index_write_u64(unsigned char *out, unsigned long long value) {
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        out[index] = (unsigned char)((value >> (index * 8U)) & 0xffU);
    }
}

static unsigned int osm_index_read_u32(const unsigned char *in) {
    return (unsigned int)in[0] |
           ((unsigned int)in[1] << 8U) |
           ((unsigned int)in[2] << 16U) |
           ((unsigned int)in[3] << 24U);
}

static unsigned long long osm_index_read_u64(const unsigned char *in) {
    unsigned long long value = 0ULL;
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        value |= ((unsigned long long)in[index]) << (index * 8U);
    }
    return value;
}

static int osm_index_read_exact(int fd, void *buffer, size_t count) {
    unsigned char *cursor = (unsigned char *)buffer;
    size_t offset = 0U;

    while (offset < count) {
        long bytes = platform_read(fd, cursor + offset, count - offset);
        if (bytes <= 0) {
            return -1;
        }
        offset += (size_t)bytes;
    }
    return 0;
}

static void osm_node_index_make_header(unsigned char header[OSM_NODE_INDEX_HEADER_SIZE], unsigned long long count) {
    rt_memset(header, 0, OSM_NODE_INDEX_HEADER_SIZE);
    memcpy(header, osm_node_index_magic, sizeof(osm_node_index_magic));
    osm_index_write_u32(header + 8U, 1U);
    osm_index_write_u32(header + 12U, OSM_NODE_INDEX_RECORD_SIZE);
    osm_index_write_u64(header + 16U, count);
}

static void osm_way_index_make_header(unsigned char header[OSM_WAY_INDEX_HEADER_SIZE], unsigned long long count, unsigned long long ref_count) {
    rt_memset(header, 0, OSM_WAY_INDEX_HEADER_SIZE);
    memcpy(header, osm_way_index_magic, sizeof(osm_way_index_magic));
    osm_index_write_u32(header + 8U, 1U);
    osm_index_write_u32(header + 12U, OSM_WAY_INDEX_RECORD_SIZE);
    osm_index_write_u64(header + 16U, count);
    osm_index_write_u64(header + 24U, ref_count);
}

static void osm_relation_index_make_header(unsigned char header[OSM_RELATION_INDEX_HEADER_SIZE], unsigned long long count, unsigned long long member_count, unsigned long long name_size) {
    rt_memset(header, 0, OSM_RELATION_INDEX_HEADER_SIZE);
    memcpy(header, osm_relation_index_magic, sizeof(osm_relation_index_magic));
    osm_index_write_u32(header + 8U, 1U);
    osm_index_write_u32(header + 12U, OSM_RELATION_INDEX_RECORD_SIZE);
    osm_index_write_u64(header + 16U, count);
    osm_index_write_u64(header + 24U, member_count);
    osm_index_write_u64(header + 32U, name_size);
}

static void osm_spatial_index_make_header(unsigned char header[OSM_SPATIAL_INDEX_HEADER_SIZE], unsigned long long count) {
    rt_memset(header, 0, OSM_SPATIAL_INDEX_HEADER_SIZE);
    memcpy(header, osm_spatial_index_magic, sizeof(osm_spatial_index_magic));
    osm_index_write_u32(header + 8U, 1U);
    osm_index_write_u32(header + 12U, OSM_SPATIAL_INDEX_RECORD_SIZE);
    osm_index_write_u64(header + 16U, count);
}

static int osm_way_index_write_header(int fd, unsigned long long count, unsigned long long ref_count) {
    unsigned char header[OSM_WAY_INDEX_HEADER_SIZE];

    osm_way_index_make_header(header, count, ref_count);
    return rt_write_all(fd, header, sizeof(header));
}

static int osm_way_index_write_header_buffered(OsmIndexWriter *writer, unsigned long long count, unsigned long long ref_count) {
    unsigned char header[OSM_WAY_INDEX_HEADER_SIZE];

    osm_way_index_make_header(header, count, ref_count);
    return osm_index_writer_write(writer, header, sizeof(header));
}

static int osm_way_index_write_record(OsmIndexWriter *writer, const OsmWayIndexRecord *record) {
    unsigned char data[OSM_WAY_INDEX_RECORD_SIZE];

    rt_memset(data, 0, sizeof(data));
    osm_index_write_u64(data, (unsigned long long)record->id);
    osm_index_write_u64(data + 8U, record->ref_offset);
    osm_index_write_u32(data + 16U, record->ref_count);
    return osm_index_writer_write(writer, data, sizeof(data));
}

static void osm_way_index_read_record_data(const unsigned char data[OSM_WAY_INDEX_RECORD_SIZE], OsmWayIndexRecord *record) {
    record->id = (long long)osm_index_read_u64(data);
    record->ref_offset = osm_index_read_u64(data + 8U);
    record->ref_count = osm_index_read_u32(data + 16U);
}

static void osm_relation_index_write_record_data(unsigned char data[OSM_RELATION_INDEX_RECORD_SIZE], const OsmRelationIndexRecord *record) {
    rt_memset(data, 0, OSM_RELATION_INDEX_RECORD_SIZE);
    osm_index_write_u64(data, (unsigned long long)record->id);
    osm_index_write_u64(data + 8U, record->member_offset);
    osm_index_write_u32(data + 16U, record->member_count);
    osm_index_write_u32(data + 20U, record->admin_level);
    osm_index_write_u64(data + 24U, record->name_offset);
    osm_index_write_u32(data + 32U, record->name_size);
    osm_index_write_u32(data + 36U, record->score);
}

static void osm_relation_index_read_record_data(const unsigned char data[OSM_RELATION_INDEX_RECORD_SIZE], OsmRelationIndexRecord *record) {
    record->id = (long long)osm_index_read_u64(data);
    record->member_offset = osm_index_read_u64(data + 8U);
    record->member_count = osm_index_read_u32(data + 16U);
    record->admin_level = osm_index_read_u32(data + 20U);
    record->name_offset = osm_index_read_u64(data + 24U);
    record->name_size = osm_index_read_u32(data + 32U);
    record->score = osm_index_read_u32(data + 36U);
}

static void osm_spatial_index_write_record_data(unsigned char data[OSM_SPATIAL_INDEX_RECORD_SIZE], const OsmSpatialIndexRecord *record) {
    osm_index_write_u64(data, (unsigned long long)record->id);
    osm_index_write_u64(data + 8U, (unsigned long long)record->min_lon_nano);
    osm_index_write_u64(data + 16U, (unsigned long long)record->min_lat_nano);
    osm_index_write_u64(data + 24U, (unsigned long long)record->max_lon_nano);
    osm_index_write_u64(data + 32U, (unsigned long long)record->max_lat_nano);
}

static void osm_spatial_index_read_record_data(const unsigned char data[OSM_SPATIAL_INDEX_RECORD_SIZE], OsmSpatialIndexRecord *record) {
    record->id = (long long)osm_index_read_u64(data);
    record->min_lon_nano = (long long)osm_index_read_u64(data + 8U);
    record->min_lat_nano = (long long)osm_index_read_u64(data + 16U);
    record->max_lon_nano = (long long)osm_index_read_u64(data + 24U);
    record->max_lat_nano = (long long)osm_index_read_u64(data + 32U);
}

static int osm_way_index_write_ref(OsmIndexWriter *writer, long long ref) {
    unsigned char data[8];

    osm_index_write_u64(data, (unsigned long long)ref);
    return osm_index_writer_write(writer, data, sizeof(data));
}

static int osm_node_index_write_header(int fd, unsigned long long count) {
    unsigned char header[OSM_NODE_INDEX_HEADER_SIZE];

    osm_node_index_make_header(header, count);
    return rt_write_all(fd, header, sizeof(header));
}

static int osm_node_index_write_header_buffered(OsmIndexWriter *writer, unsigned long long count) {
    unsigned char header[OSM_NODE_INDEX_HEADER_SIZE];

    osm_node_index_make_header(header, count);
    return osm_index_writer_write(writer, header, sizeof(header));
}

static int osm_node_index_write_record(OsmIndexWriter *writer, const OsmNodeIndexRecord *record) {
    unsigned char data[OSM_NODE_INDEX_RECORD_SIZE];

    osm_index_write_u64(data, (unsigned long long)record->id);
    osm_index_write_u64(data + 8U, (unsigned long long)record->lat_nano);
    osm_index_write_u64(data + 16U, (unsigned long long)record->lon_nano);
    return osm_index_writer_write(writer, data, sizeof(data));
}

static void osm_node_index_read_record_data(const unsigned char data[OSM_NODE_INDEX_RECORD_SIZE], OsmNodeIndexRecord *record) {
    record->id = (long long)osm_index_read_u64(data);
    record->lat_nano = (long long)osm_index_read_u64(data + 8U);
    record->lon_nano = (long long)osm_index_read_u64(data + 16U);
}

static int osm_node_index_on_node(void *user, const PbfNode *node) {
    OsmNodeIndexBuildContext *context = (OsmNodeIndexBuildContext *)user;
    OsmNodeIndexRecord record;

    if (context->has_last_id && node->id < context->last_id) {
        context->failed = 1;
        osm_index_set_error(context->error, context->error_capacity, "nodes are not sorted by ID");
        return 1;
    }
    record.id = node->id;
    record.lat_nano = node->lat_nano;
    record.lon_nano = node->lon_nano;
    if (osm_node_index_write_record(&context->writer, &record) != 0) {
        context->failed = 1;
        osm_index_set_error(context->error, context->error_capacity, "could not write node index record");
        return 1;
    }
    context->last_id = node->id;
    context->has_last_id = 1;
    context->count += 1ULL;
    if ((context->flags & OSM_INDEX_BUILD_PROGRESS) != 0U && context->count % OSM_NODE_INDEX_PROGRESS_INTERVAL == 0ULL) {
        osm_index_write_progress_label("nodes_indexed: ", context->count);
    }
    return 0;
}

static int osm_way_index_on_way(void *user, const PbfWay *way) {
    OsmWayIndexBuildContext *context = (OsmWayIndexBuildContext *)user;
    OsmWayIndexRecord record;
    unsigned int index;

    if (context->has_last_id && way->id < context->last_id) {
        context->failed = 1;
        osm_index_set_error(context->error, context->error_capacity, "ways are not sorted by ID");
        return 1;
    }
    if (way->ref_count > 0xffffffffU || context->ref_count > 0xffffffffffffffffULL - (unsigned long long)way->ref_count) {
        context->failed = 1;
        osm_index_set_error(context->error, context->error_capacity, "way index is too large");
        return 1;
    }
    record.id = way->id;
    record.ref_offset = context->ref_count;
    record.ref_count = way->ref_count;
    if (osm_way_index_write_record(&context->record_writer, &record) != 0) {
        context->failed = 1;
        osm_index_set_error(context->error, context->error_capacity, "could not write way index record");
        return 1;
    }
    for (index = 0U; index < way->ref_count; ++index) {
        if (osm_way_index_write_ref(&context->ref_writer, way->refs[index]) != 0) {
            context->failed = 1;
            osm_index_set_error(context->error, context->error_capacity, "could not write way index refs");
            return 1;
        }
    }
    context->last_id = way->id;
    context->has_last_id = 1;
    context->count += 1ULL;
    context->ref_count += (unsigned long long)way->ref_count;
    if ((context->flags & OSM_INDEX_BUILD_PROGRESS) != 0U && context->count % OSM_WAY_INDEX_PROGRESS_INTERVAL == 0ULL) {
        osm_index_write_progress_label("ways_indexed: ", context->count);
    }
    return 0;
}

int osm_node_index_build(const char *pbf_path, const char *index_path, unsigned long long *count_out, char *error, size_t error_capacity) {
    return osm_node_index_build_ex(pbf_path, index_path, count_out, 0U, error, error_capacity);
}

int osm_node_index_build_ex(const char *pbf_path, const char *index_path, unsigned long long *count_out, unsigned int flags, char *error, size_t error_capacity) {
    OsmNodeIndexBuildContext context;
    PbfStreamCallbacks callbacks;
    int stream_result;

    rt_memset(&context, 0, sizeof(context));
    rt_memset(&callbacks, 0, sizeof(callbacks));
    callbacks.flags = PBF_STREAM_SKIP_NODE_TAGS;
    context.error = error;
    context.error_capacity = error_capacity;
    context.flags = flags;
    context.writer.fd = -1;
    if (osm_index_writer_open(&context.writer, index_path, 0644U, error, error_capacity, "could not open index output file") != 0) {
        return -1;
    }
    if (osm_node_index_write_header_buffered(&context.writer, 0ULL) != 0) {
        osm_index_writer_discard(&context.writer);
        osm_index_set_error(error, error_capacity, "could not write node index header");
        return -1;
    }
    callbacks.node = osm_node_index_on_node;
    stream_result = pbf_stream_entities(pbf_path, &callbacks, &context, error, error_capacity);
    if (stream_result != 0 || context.failed) {
        osm_index_writer_discard(&context.writer);
        return -1;
    }
    if (osm_index_writer_seek(&context.writer, 0, PLATFORM_SEEK_SET) != 0 || osm_node_index_write_header(context.writer.fd, context.count) != 0) {
        osm_index_writer_discard(&context.writer);
        osm_index_set_error(error, error_capacity, "could not finalize node index header");
        return -1;
    }
    if (osm_index_writer_close(&context.writer) != 0) {
        osm_index_set_error(error, error_capacity, "could not close node index");
        return -1;
    }
    if (count_out != 0) {
        *count_out = context.count;
    }
    return 0;
}

static int osm_way_index_append_refs(OsmIndexWriter *index_writer, const char *ref_temp_path, char *error, size_t error_capacity) {
    int ref_read_fd;
    unsigned char buffer[65536U];

    ref_read_fd = platform_open_read(ref_temp_path);
    if (ref_read_fd < 0) {
        osm_index_set_error(error, error_capacity, "could not reopen way ref data");
        return -1;
    }
    for (;;) {
        long bytes = platform_read(ref_read_fd, buffer, sizeof(buffer));
        if (bytes < 0) {
            (void)platform_close(ref_read_fd);
            osm_index_set_error(error, error_capacity, "could not read way ref data");
            return -1;
        }
        if (bytes == 0) break;
        if (osm_index_writer_write(index_writer, buffer, (size_t)bytes) != 0) {
            (void)platform_close(ref_read_fd);
            osm_index_set_error(error, error_capacity, "could not append way ref data");
            return -1;
        }
    }
    if (platform_close(ref_read_fd) != 0) {
        osm_index_set_error(error, error_capacity, "could not close way ref data");
        return -1;
    }
    return 0;
}

int osm_way_index_build(const char *pbf_path, const char *index_path, unsigned long long *way_count_out, unsigned long long *ref_count_out, char *error, size_t error_capacity) {
    return osm_way_index_build_ex(pbf_path, index_path, way_count_out, ref_count_out, 0U, error, error_capacity);
}

int osm_way_index_build_ex(const char *pbf_path, const char *index_path, unsigned long long *way_count_out, unsigned long long *ref_count_out, unsigned int flags, char *error, size_t error_capacity) {
    OsmWayIndexBuildContext context;
    PbfStreamCallbacks callbacks;
    static const char ref_temp_suffix[] = ".refs.tmp";
    char *ref_temp_path;
    size_t index_path_size;
    size_t ref_temp_suffix_size;
    int stream_result;

    rt_memset(&context, 0, sizeof(context));
    rt_memset(&callbacks, 0, sizeof(callbacks));
    index_path_size = rt_strlen(index_path);
    ref_temp_suffix_size = sizeof(ref_temp_suffix) - 1U;
    ref_temp_path = (char *)rt_malloc(index_path_size + ref_temp_suffix_size + 1U);
    if (ref_temp_path == 0) {
        osm_index_set_error(error, error_capacity, "out of memory while preparing way index");
        return -1;
    }
    memcpy(ref_temp_path, index_path, index_path_size);
    memcpy(ref_temp_path + index_path_size, ref_temp_suffix, ref_temp_suffix_size + 1U);
    context.record_writer.fd = -1;
    context.ref_writer.fd = -1;
    context.error = error;
    context.error_capacity = error_capacity;
    context.flags = flags;
    if (osm_index_writer_open(&context.record_writer, index_path, 0644U, error, error_capacity, "could not open way index output file") != 0) {
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_index_writer_open(&context.ref_writer, ref_temp_path, 0600U, error, error_capacity, "could not create temporary way ref file") != 0) {
        osm_index_writer_discard(&context.record_writer);
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_way_index_write_header_buffered(&context.record_writer, 0ULL, 0ULL) != 0) {
        osm_index_writer_discard(&context.record_writer);
        osm_index_writer_discard(&context.ref_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        osm_index_set_error(error, error_capacity, "could not write way index header");
        return -1;
    }
    callbacks.flags = PBF_STREAM_SKIP_NODE_TAGS | PBF_STREAM_SKIP_WAY_TAGS | PBF_STREAM_SKIP_RELATION_ROLES;
    callbacks.way = osm_way_index_on_way;
    stream_result = pbf_stream_entities(pbf_path, &callbacks, &context, error, error_capacity);
    if (osm_index_writer_close(&context.ref_writer) != 0) {
        osm_index_writer_discard(&context.record_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        osm_index_set_error(error, error_capacity, "could not close temporary way ref file");
        return -1;
    }
    if (stream_result != 0 || context.failed) {
        osm_index_writer_discard(&context.record_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_way_index_append_refs(&context.record_writer, ref_temp_path, error, error_capacity) != 0) {
        osm_index_writer_discard(&context.record_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        return -1;
    }
    (void)platform_remove_file(ref_temp_path);
    rt_free(ref_temp_path);
    if (osm_index_writer_seek(&context.record_writer, 0, PLATFORM_SEEK_SET) != 0 || osm_way_index_write_header(context.record_writer.fd, context.count, context.ref_count) != 0) {
        osm_index_writer_discard(&context.record_writer);
        osm_index_set_error(error, error_capacity, "could not finalize way index header");
        return -1;
    }
    if (osm_index_writer_close(&context.record_writer) != 0) {
        osm_index_set_error(error, error_capacity, "could not close way index");
        return -1;
    }
    if (way_count_out != 0) *way_count_out = context.count;
    if (ref_count_out != 0) *ref_count_out = context.ref_count;
    return 0;
}

static int osm_index_on_node(void *user, const PbfNode *node) {
    OsmIndexBuildContext *context = (OsmIndexBuildContext *)user;

    return osm_node_index_on_node(&context->node, node);
}

static int osm_index_on_way(void *user, const PbfWay *way) {
    OsmIndexBuildContext *context = (OsmIndexBuildContext *)user;

    return osm_way_index_on_way(&context->way, way);
}

static int osm_index_prepare_way_temp_path(const char *way_index_path, char **ref_temp_path_out, char *error, size_t error_capacity) {
    static const char ref_temp_suffix[] = ".refs.tmp";
    char *ref_temp_path;
    size_t index_path_size = rt_strlen(way_index_path);
    size_t ref_temp_suffix_size = sizeof(ref_temp_suffix) - 1U;

    ref_temp_path = (char *)rt_malloc(index_path_size + ref_temp_suffix_size + 1U);
    if (ref_temp_path == 0) {
        osm_index_set_error(error, error_capacity, "out of memory while preparing way index");
        return -1;
    }
    memcpy(ref_temp_path, way_index_path, index_path_size);
    memcpy(ref_temp_path + index_path_size, ref_temp_suffix, ref_temp_suffix_size + 1U);
    *ref_temp_path_out = ref_temp_path;
    return 0;
}

int osm_index_build_ex(const char *pbf_path, const char *node_index_path, const char *way_index_path, unsigned long long *node_count_out, unsigned long long *way_count_out, unsigned long long *ref_count_out, unsigned int flags, char *error, size_t error_capacity) {
    OsmIndexBuildContext context;
    PbfStreamCallbacks callbacks;
    char *ref_temp_path;
    int stream_result;

    rt_memset(&context, 0, sizeof(context));
    rt_memset(&callbacks, 0, sizeof(callbacks));
    ref_temp_path = 0;
    context.node.writer.fd = -1;
    context.way.record_writer.fd = -1;
    context.way.ref_writer.fd = -1;
    context.node.error = error;
    context.node.error_capacity = error_capacity;
    context.node.flags = flags;
    context.way.error = error;
    context.way.error_capacity = error_capacity;
    context.way.flags = flags;

    if (osm_index_prepare_way_temp_path(way_index_path, &ref_temp_path, error, error_capacity) != 0) return -1;
    if (osm_index_writer_open(&context.node.writer, node_index_path, 0644U, error, error_capacity, "could not open index output file") != 0) {
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_index_writer_open(&context.way.record_writer, way_index_path, 0644U, error, error_capacity, "could not open way index output file") != 0) {
        osm_index_writer_discard(&context.node.writer);
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_index_writer_open(&context.way.ref_writer, ref_temp_path, 0600U, error, error_capacity, "could not create temporary way ref file") != 0) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_node_index_write_header_buffered(&context.node.writer, 0ULL) != 0 || osm_way_index_write_header_buffered(&context.way.record_writer, 0ULL, 0ULL) != 0) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        osm_index_writer_discard(&context.way.ref_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        osm_index_set_error(error, error_capacity, "could not write index header");
        return -1;
    }

    callbacks.flags = PBF_STREAM_SKIP_NODE_TAGS | PBF_STREAM_SKIP_WAY_TAGS | PBF_STREAM_SKIP_RELATION_ROLES;
    callbacks.node = osm_index_on_node;
    callbacks.way = osm_index_on_way;
    stream_result = pbf_stream_entities(pbf_path, &callbacks, &context, error, error_capacity);
    if (osm_index_writer_close(&context.way.ref_writer) != 0) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        osm_index_set_error(error, error_capacity, "could not close temporary way ref file");
        return -1;
    }
    if (stream_result != 0 || context.node.failed || context.way.failed) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        return -1;
    }
    if (osm_way_index_append_refs(&context.way.record_writer, ref_temp_path, error, error_capacity) != 0) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        (void)platform_remove_file(ref_temp_path);
        rt_free(ref_temp_path);
        return -1;
    }
    (void)platform_remove_file(ref_temp_path);
    rt_free(ref_temp_path);

    if (osm_index_writer_seek(&context.node.writer, 0, PLATFORM_SEEK_SET) != 0 || osm_node_index_write_header(context.node.writer.fd, context.node.count) != 0) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        osm_index_set_error(error, error_capacity, "could not finalize node index header");
        return -1;
    }
    if (osm_index_writer_seek(&context.way.record_writer, 0, PLATFORM_SEEK_SET) != 0 || osm_way_index_write_header(context.way.record_writer.fd, context.way.count, context.way.ref_count) != 0) {
        osm_index_writer_discard(&context.node.writer);
        osm_index_writer_discard(&context.way.record_writer);
        osm_index_set_error(error, error_capacity, "could not finalize way index header");
        return -1;
    }
    if (osm_index_writer_close(&context.node.writer) != 0) {
        osm_index_writer_discard(&context.way.record_writer);
        osm_index_set_error(error, error_capacity, "could not close node index");
        return -1;
    }
    if (osm_index_writer_close(&context.way.record_writer) != 0) {
        osm_index_set_error(error, error_capacity, "could not close way index");
        return -1;
    }
    if (node_count_out != 0) *node_count_out = context.node.count;
    if (way_count_out != 0) *way_count_out = context.way.count;
    if (ref_count_out != 0) *ref_count_out = context.way.ref_count;
    return 0;
}

static int osm_index_text_equals(PbfText text, const char *value) {
    size_t value_size = rt_strlen(value);
    return text.size == value_size && memcmp(text.data, value, value_size) == 0;
}

static const PbfText *osm_index_find_tag_value(const PbfTag *tags, unsigned int tag_count, const char *key) {
    unsigned int index;

    for (index = 0U; index < tag_count; ++index) {
        if (osm_index_text_equals(tags[index].key, key)) return &tags[index].value;
    }
    return 0;
}

static int osm_index_tag_value_equals(const PbfText *value, const char *expected) {
    return value != 0 && osm_index_text_equals(*value, expected);
}

static unsigned int osm_relation_admin_level_value(const PbfText *value) {
    unsigned int level = 0U;
    size_t index;

    if (value == 0 || value->size == 0U) return 0U;
    for (index = 0U; index < value->size; ++index) {
        if (value->data[index] < '0' || value->data[index] > '9') return 0U;
        level = level * 10U + (unsigned int)(value->data[index] - '0');
        if (level > 20U) return 0U;
    }
    return level;
}

static unsigned int osm_relation_city_level_score(unsigned int level) {
    if (level == 6U) return 100U;
    if (level == 8U) return 90U;
    if (level == 4U) return 80U;
    if (level == 5U || level == 7U) return 60U;
    if (level == 9U || level == 10U) return 20U;
    return 0U;
}

static int osm_relation_index_grow_records(OsmRelationIndexBuildContext *context, unsigned long long needed) {
    unsigned long long capacity = context->record_capacity == 0ULL ? 1024ULL : context->record_capacity;
    OsmRelationIndexRecord *records;

    while (capacity < needed) {
        if (capacity > (unsigned long long)(((size_t)-1) / (sizeof(OsmRelationIndexRecord) * 2U))) return -1;
        capacity *= 2ULL;
    }
    if (capacity == context->record_capacity) return 0;
    records = (OsmRelationIndexRecord *)rt_realloc(context->records, sizeof(OsmRelationIndexRecord) * (size_t)capacity);
    if (records == 0) return -1;
    context->records = records;
    context->record_capacity = capacity;
    return 0;
}

static int osm_relation_index_grow_members(OsmRelationIndexBuildContext *context, unsigned long long needed) {
    unsigned long long capacity = context->member_capacity == 0ULL ? 4096ULL : context->member_capacity;
    long long *members;

    while (capacity < needed) {
        if (capacity > (unsigned long long)(((size_t)-1) / (sizeof(long long) * 2U))) return -1;
        capacity *= 2ULL;
    }
    if (capacity == context->member_capacity) return 0;
    members = (long long *)rt_realloc(context->members, sizeof(long long) * (size_t)capacity);
    if (members == 0) return -1;
    context->members = members;
    context->member_capacity = capacity;
    return 0;
}

static int osm_relation_index_grow_names(OsmRelationIndexBuildContext *context, unsigned long long needed) {
    unsigned long long capacity = context->name_capacity == 0ULL ? 4096ULL : context->name_capacity;
    char *names;

    while (capacity < needed) {
        if (capacity > (unsigned long long)(((size_t)-1) / 2U)) return -1;
        capacity *= 2ULL;
    }
    if (capacity == context->name_capacity) return 0;
    names = (char *)rt_realloc(context->names, (size_t)capacity);
    if (names == 0) return -1;
    context->names = names;
    context->name_capacity = capacity;
    return 0;
}

static int osm_relation_index_on_relation_tags(void *user, long long id, const PbfTag *tags, unsigned int tag_count) {
    const PbfText *name = osm_index_find_tag_value(tags, tag_count, "name");
    const PbfText *boundary = osm_index_find_tag_value(tags, tag_count, "boundary");
    const PbfText *type = osm_index_find_tag_value(tags, tag_count, "type");
    unsigned int level = osm_relation_admin_level_value(osm_index_find_tag_value(tags, tag_count, "admin_level"));

    (void)user;
    (void)id;
    if (name == 0 || name->size == 0U) return 0;
    if (!osm_index_tag_value_equals(boundary, "administrative")) return 0;
    if (type != 0 && !osm_index_tag_value_equals(type, "boundary") && !osm_index_tag_value_equals(type, "multipolygon")) return 0;
    return osm_relation_city_level_score(level) != 0U;
}

static int osm_relation_index_on_relation(void *user, const PbfRelation *relation) {
    OsmRelationIndexBuildContext *context = (OsmRelationIndexBuildContext *)user;
    const PbfText *name = osm_index_find_tag_value(relation->tags, relation->tag_count, "name");
    unsigned int level = osm_relation_admin_level_value(osm_index_find_tag_value(relation->tags, relation->tag_count, "admin_level"));
    unsigned int score = osm_relation_city_level_score(level);
    unsigned int member_index;
    unsigned int way_member_count = 0U;
    unsigned long long member_offset = context->member_count;
    unsigned long long name_offset = context->name_size;
    OsmRelationIndexRecord *record;

    if (!osm_relation_index_on_relation_tags(user, relation->id, relation->tags, relation->tag_count)) return 0;
    for (member_index = 0U; member_index < relation->member_count; ++member_index) {
        if (relation->members[member_index].type == PBF_RELATION_MEMBER_WAY) way_member_count += 1U;
    }
    if (way_member_count == 0U) return 0;
    if (osm_relation_index_grow_records(context, context->count + 1ULL) != 0 ||
        osm_relation_index_grow_members(context, context->member_count + (unsigned long long)way_member_count) != 0 ||
        osm_relation_index_grow_names(context, context->name_size + (unsigned long long)name->size + 1ULL) != 0) {
        context->failed = 1;
        osm_index_set_error(context->error, context->error_capacity, "out of memory while building relation index");
        return 1;
    }
    for (member_index = 0U; member_index < relation->member_count; ++member_index) {
        if (relation->members[member_index].type == PBF_RELATION_MEMBER_WAY) {
            context->members[context->member_count] = relation->members[member_index].id;
            context->member_count += 1ULL;
        }
    }
    memcpy(context->names + context->name_size, name->data, name->size);
    context->name_size += (unsigned long long)name->size;
    context->names[context->name_size] = '\0';
    context->name_size += 1ULL;
    record = &context->records[context->count];
    record->id = relation->id;
    record->member_offset = member_offset;
    record->member_count = way_member_count;
    record->admin_level = level;
    record->name_offset = name_offset;
    record->name_size = (unsigned int)name->size;
    record->score = score * 1000U + (way_member_count > 999U ? 999U : way_member_count);
    context->count += 1ULL;
    if ((context->flags & OSM_INDEX_BUILD_PROGRESS) != 0U && context->count % OSM_RELATION_INDEX_PROGRESS_INTERVAL == 0ULL) {
        osm_index_write_progress_label("relations_indexed: ", context->count);
    }
    return 0;
}

int osm_relation_index_build(const char *pbf_path, const char *index_path, unsigned long long *relation_count_out, unsigned long long *member_count_out, unsigned int flags, char *error, size_t error_capacity) {
    OsmRelationIndexBuildContext context;
    PbfStreamCallbacks callbacks;
    OsmIndexWriter writer;
    unsigned char header[OSM_RELATION_INDEX_HEADER_SIZE];
    unsigned char record_data[OSM_RELATION_INDEX_RECORD_SIZE];
    unsigned char member_data[8];
    unsigned long long index;
    int stream_result;

    rt_memset(&context, 0, sizeof(context));
    rt_memset(&callbacks, 0, sizeof(callbacks));
    writer.fd = -1;
    context.flags = flags;
    context.error = error;
    context.error_capacity = error_capacity;
    callbacks.flags = PBF_STREAM_SKIP_NODE_TAGS | PBF_STREAM_SKIP_WAY_TAGS | PBF_STREAM_SKIP_RELATION_ROLES;
    callbacks.relation_tags = osm_relation_index_on_relation_tags;
    callbacks.relation = osm_relation_index_on_relation;
    stream_result = pbf_stream_entities(pbf_path, &callbacks, &context, error, error_capacity);
    if (stream_result != 0 || context.failed) {
        rt_free(context.records);
        rt_free(context.members);
        rt_free(context.names);
        return -1;
    }
    if (osm_index_writer_open(&writer, index_path, 0644U, error, error_capacity, "could not open relation index output file") != 0) {
        rt_free(context.records);
        rt_free(context.members);
        rt_free(context.names);
        return -1;
    }
    osm_relation_index_make_header(header, context.count, context.member_count, context.name_size);
    if (osm_index_writer_write(&writer, header, sizeof(header)) != 0) goto write_fail;
    for (index = 0ULL; index < context.count; ++index) {
        osm_relation_index_write_record_data(record_data, &context.records[index]);
        if (osm_index_writer_write(&writer, record_data, sizeof(record_data)) != 0) goto write_fail;
    }
    if (context.name_size != 0ULL && osm_index_writer_write(&writer, context.names, (size_t)context.name_size) != 0) goto write_fail;
    for (index = 0ULL; index < context.member_count; ++index) {
        osm_index_write_u64(member_data, (unsigned long long)context.members[index]);
        if (osm_index_writer_write(&writer, member_data, sizeof(member_data)) != 0) goto write_fail;
    }
    if (osm_index_writer_close(&writer) != 0) {
        osm_index_set_error(error, error_capacity, "could not close relation index");
        rt_free(context.records);
        rt_free(context.members);
        rt_free(context.names);
        return -1;
    }
    if (relation_count_out != 0) *relation_count_out = context.count;
    if (member_count_out != 0) *member_count_out = context.member_count;
    rt_free(context.records);
    rt_free(context.members);
    rt_free(context.names);
    return 0;

write_fail:
    osm_index_writer_discard(&writer);
    rt_free(context.records);
    rt_free(context.members);
    rt_free(context.names);
    osm_index_set_error(error, error_capacity, "could not write relation index");
    return -1;
}

int osm_node_index_open(OsmNodeIndex *index, const char *path, char *error, size_t error_capacity) {
    unsigned char header[OSM_NODE_INDEX_HEADER_SIZE];
    unsigned int version;
    unsigned int record_size;

    rt_memset(index, 0, sizeof(*index));
    index->fd = -1;
    index->fd = platform_open_read(path);
    if (index->fd < 0) {
        osm_index_set_error(error, error_capacity, "could not open node index");
        return -1;
    }
    if (osm_index_read_exact(index->fd, header, sizeof(header)) != 0 || memcmp(header, osm_node_index_magic, sizeof(osm_node_index_magic)) != 0) {
        osm_node_index_close(index);
        osm_index_set_error(error, error_capacity, "invalid node index header");
        return -1;
    }
    version = osm_index_read_u32(header + 8U);
    record_size = osm_index_read_u32(header + 12U);
    if (version != 1U || record_size != OSM_NODE_INDEX_RECORD_SIZE) {
        osm_node_index_close(index);
        osm_index_set_error(error, error_capacity, "unsupported node index version");
        return -1;
    }
    index->count = osm_index_read_u64(header + 16U);
    return 0;
}

void osm_node_index_close(OsmNodeIndex *index) {
    if (index != 0 && index->fd >= 0) {
        (void)platform_close(index->fd);
        index->fd = -1;
        index->count = 0ULL;
    }
}

int osm_way_index_open(OsmWayIndex *index, const char *path, char *error, size_t error_capacity) {
    unsigned char header[OSM_WAY_INDEX_HEADER_SIZE];
    unsigned int version;
    unsigned int record_size;

    rt_memset(index, 0, sizeof(*index));
    index->fd = -1;
    index->fd = platform_open_read(path);
    if (index->fd < 0) {
        osm_index_set_error(error, error_capacity, "could not open way index");
        return -1;
    }
    if (osm_index_read_exact(index->fd, header, sizeof(header)) != 0 || memcmp(header, osm_way_index_magic, sizeof(osm_way_index_magic)) != 0) {
        osm_way_index_close(index);
        osm_index_set_error(error, error_capacity, "invalid way index header");
        return -1;
    }
    version = osm_index_read_u32(header + 8U);
    record_size = osm_index_read_u32(header + 12U);
    if (version != 1U || record_size != OSM_WAY_INDEX_RECORD_SIZE) {
        osm_way_index_close(index);
        osm_index_set_error(error, error_capacity, "unsupported way index version");
        return -1;
    }
    index->count = osm_index_read_u64(header + 16U);
    index->ref_count = osm_index_read_u64(header + 24U);
    return 0;
}

void osm_way_index_close(OsmWayIndex *index) {
    if (index != 0 && index->fd >= 0) {
        (void)platform_close(index->fd);
        index->fd = -1;
        index->count = 0ULL;
        index->ref_count = 0ULL;
    }
}

int osm_node_index_find(OsmNodeIndex *index, long long id, OsmNodeIndexRecord *record_out, char *error, size_t error_capacity) {
    unsigned long long left = 0ULL;
    unsigned long long right = index->count;
    unsigned char data[OSM_NODE_INDEX_RECORD_SIZE];

    while (left < right) {
        unsigned long long mid = left + (right - left) / 2ULL;
        long long offset = (long long)OSM_NODE_INDEX_HEADER_SIZE + (long long)(mid * OSM_NODE_INDEX_RECORD_SIZE);
        OsmNodeIndexRecord record;

        if (platform_seek(index->fd, offset, PLATFORM_SEEK_SET) != offset || osm_index_read_exact(index->fd, data, sizeof(data)) != 0) {
            osm_index_set_error(error, error_capacity, "could not read node index record");
            return -1;
        }
        osm_node_index_read_record_data(data, &record);
        if (record.id == id) {
            if (record_out != 0) {
                *record_out = record;
            }
            return 1;
        }
        if (record.id < id) {
            left = mid + 1ULL;
        } else {
            right = mid;
        }
    }
    return 0;
}

int osm_way_index_find(OsmWayIndex *index, long long id, OsmWayIndexRecord *record_out, char *error, size_t error_capacity) {
    unsigned long long left = 0ULL;
    unsigned long long right = index->count;
    unsigned char data[OSM_WAY_INDEX_RECORD_SIZE];

    while (left < right) {
        unsigned long long mid = left + (right - left) / 2ULL;
        long long offset = (long long)OSM_WAY_INDEX_HEADER_SIZE + (long long)(mid * OSM_WAY_INDEX_RECORD_SIZE);
        OsmWayIndexRecord record;

        if (platform_seek(index->fd, offset, PLATFORM_SEEK_SET) != offset || osm_index_read_exact(index->fd, data, sizeof(data)) != 0) {
            osm_index_set_error(error, error_capacity, "could not read way index record");
            return -1;
        }
        osm_way_index_read_record_data(data, &record);
        if (record.id == id) {
            if (record_out != 0) *record_out = record;
            return 1;
        }
        if (record.id < id) left = mid + 1ULL;
        else right = mid;
    }
    return 0;
}

int osm_way_index_read_refs(OsmWayIndex *index, const OsmWayIndexRecord *record, long long **refs_out, char *error, size_t error_capacity) {
    unsigned char *data;
    long long *refs;
    size_t data_size;
    unsigned int ref_index;
    long long offset;

    *refs_out = 0;
    if (record->ref_count == 0U) return 0;
    if (record->ref_offset > index->ref_count || (unsigned long long)record->ref_count > index->ref_count - record->ref_offset) {
        osm_index_set_error(error, error_capacity, "way ref range is outside index");
        return -1;
    }
    if (record->ref_count > (unsigned int)(((size_t)-1) / sizeof(long long)) || record->ref_count > (unsigned int)(((size_t)-1) / 8U)) {
        osm_index_set_error(error, error_capacity, "way has too many refs");
        return -1;
    }
    refs = (long long *)rt_malloc(sizeof(long long) * (size_t)record->ref_count);
    data_size = (size_t)record->ref_count * 8U;
    data = (unsigned char *)rt_malloc(data_size);
    if (refs == 0 || data == 0) {
        rt_free(refs);
        rt_free(data);
        osm_index_set_error(error, error_capacity, "out of memory while reading way refs");
        return -1;
    }
    offset = (long long)OSM_WAY_INDEX_HEADER_SIZE + (long long)(index->count * OSM_WAY_INDEX_RECORD_SIZE) + (long long)(record->ref_offset * 8ULL);
    if (platform_seek(index->fd, offset, PLATFORM_SEEK_SET) != offset || osm_index_read_exact(index->fd, data, data_size) != 0) {
        rt_free(refs);
        rt_free(data);
        osm_index_set_error(error, error_capacity, "could not read way refs");
        return -1;
    }
    for (ref_index = 0U; ref_index < record->ref_count; ++ref_index) {
        refs[ref_index] = (long long)osm_index_read_u64(data + (size_t)ref_index * 8U);
    }
    rt_free(data);
    *refs_out = refs;
    return 0;
}

int osm_node_index_load_records(OsmNodeIndex *index, OsmNodeIndexRecord **records_out, unsigned long long *count_out, char *error, size_t error_capacity) {
    OsmNodeIndexRecord *records;
    unsigned char data[OSM_NODE_INDEX_RECORD_SIZE * 4096U];
    unsigned long long record_index;

    *records_out = 0;
    if (count_out != 0) *count_out = 0ULL;
    if (index->count > (unsigned long long)(((size_t)-1) / sizeof(OsmNodeIndexRecord))) {
        osm_index_set_error(error, error_capacity, "node index is too large to load");
        return -1;
    }
    records = (OsmNodeIndexRecord *)rt_malloc(sizeof(OsmNodeIndexRecord) * (size_t)index->count);
    if (records == 0 && index->count != 0ULL) {
        osm_index_set_error(error, error_capacity, "out of memory while loading node index");
        return -1;
    }
    if (platform_seek(index->fd, OSM_NODE_INDEX_HEADER_SIZE, PLATFORM_SEEK_SET) != (long long)OSM_NODE_INDEX_HEADER_SIZE) {
        rt_free(records);
        osm_index_set_error(error, error_capacity, "could not seek node index records");
        return -1;
    }
    record_index = 0ULL;
    while (record_index < index->count) {
        unsigned int batch_count = 0U;
        unsigned int batch_index;
        unsigned long long remaining = index->count - record_index;
        size_t batch_bytes;

        batch_count = remaining > 4096ULL ? 4096U : (unsigned int)remaining;
        batch_bytes = (size_t)batch_count * OSM_NODE_INDEX_RECORD_SIZE;
        if (osm_index_read_exact(index->fd, data, batch_bytes) != 0) {
            rt_free(records);
            osm_index_set_error(error, error_capacity, "could not read node index records");
            return -1;
        }
        for (batch_index = 0U; batch_index < batch_count; ++batch_index) {
            osm_node_index_read_record_data(data + (size_t)batch_index * OSM_NODE_INDEX_RECORD_SIZE, &records[record_index + (unsigned long long)batch_index]);
        }
        record_index += (unsigned long long)batch_count;
    }
    *records_out = records;
    if (count_out != 0) *count_out = index->count;
    return 0;
}

int osm_relation_index_open(OsmRelationIndex *index, const char *path, char *error, size_t error_capacity) {
    unsigned char header[OSM_RELATION_INDEX_HEADER_SIZE];
    unsigned char record_data[OSM_RELATION_INDEX_RECORD_SIZE * 1024U];
    unsigned char member_data[8 * 4096U];
    unsigned int version;
    unsigned int record_size;
    unsigned long long record_index;
    unsigned long long member_index;
    long long names_offset;
    long long members_offset;

    rt_memset(index, 0, sizeof(*index));
    index->fd = -1;
    index->fd = platform_open_read(path);
    if (index->fd < 0) {
        osm_index_set_error(error, error_capacity, "could not open relation index");
        return -1;
    }
    if (osm_index_read_exact(index->fd, header, sizeof(header)) != 0 || memcmp(header, osm_relation_index_magic, sizeof(osm_relation_index_magic)) != 0) {
        osm_relation_index_close(index);
        osm_index_set_error(error, error_capacity, "invalid relation index header");
        return -1;
    }
    version = osm_index_read_u32(header + 8U);
    record_size = osm_index_read_u32(header + 12U);
    if (version != 1U || record_size != OSM_RELATION_INDEX_RECORD_SIZE) {
        osm_relation_index_close(index);
        osm_index_set_error(error, error_capacity, "unsupported relation index version");
        return -1;
    }
    index->count = osm_index_read_u64(header + 16U);
    index->member_count = osm_index_read_u64(header + 24U);
    index->name_size = osm_index_read_u64(header + 32U);
    if (index->count > (unsigned long long)(((size_t)-1) / sizeof(OsmRelationIndexRecord)) ||
        index->member_count > (unsigned long long)(((size_t)-1) / sizeof(long long)) ||
        index->name_size > (unsigned long long)((size_t)-1)) {
        osm_relation_index_close(index);
        osm_index_set_error(error, error_capacity, "relation index is too large to load");
        return -1;
    }
    index->records = (OsmRelationIndexRecord *)rt_malloc(sizeof(OsmRelationIndexRecord) * (size_t)index->count);
    index->members = (long long *)rt_malloc(sizeof(long long) * (size_t)index->member_count);
    index->names = (char *)rt_malloc((size_t)index->name_size + 1U);
    if ((index->records == 0 && index->count != 0ULL) || (index->members == 0 && index->member_count != 0ULL) || (index->names == 0 && index->name_size != 0ULL)) {
        osm_relation_index_close(index);
        osm_index_set_error(error, error_capacity, "out of memory while loading relation index");
        return -1;
    }
    record_index = 0ULL;
    while (record_index < index->count) {
        unsigned long long remaining = index->count - record_index;
        unsigned int batch_count = remaining > 1024ULL ? 1024U : (unsigned int)remaining;
        unsigned int batch_index;
        size_t batch_bytes = (size_t)batch_count * OSM_RELATION_INDEX_RECORD_SIZE;

        if (osm_index_read_exact(index->fd, record_data, batch_bytes) != 0) {
            osm_relation_index_close(index);
            osm_index_set_error(error, error_capacity, "could not read relation index records");
            return -1;
        }
        for (batch_index = 0U; batch_index < batch_count; ++batch_index) {
            osm_relation_index_read_record_data(record_data + (size_t)batch_index * OSM_RELATION_INDEX_RECORD_SIZE, &index->records[record_index + (unsigned long long)batch_index]);
        }
        record_index += (unsigned long long)batch_count;
    }
    names_offset = (long long)OSM_RELATION_INDEX_HEADER_SIZE + (long long)(index->count * OSM_RELATION_INDEX_RECORD_SIZE);
    if (platform_seek(index->fd, names_offset, PLATFORM_SEEK_SET) != names_offset || (index->name_size != 0ULL && osm_index_read_exact(index->fd, index->names, (size_t)index->name_size) != 0)) {
        osm_relation_index_close(index);
        osm_index_set_error(error, error_capacity, "could not read relation index names");
        return -1;
    }
    index->names[index->name_size] = '\0';
    members_offset = names_offset + (long long)index->name_size;
    if (platform_seek(index->fd, members_offset, PLATFORM_SEEK_SET) != members_offset) {
        osm_relation_index_close(index);
        osm_index_set_error(error, error_capacity, "could not seek relation index members");
        return -1;
    }
    member_index = 0ULL;
    while (member_index < index->member_count) {
        unsigned long long remaining = index->member_count - member_index;
        unsigned int batch_count = remaining > 4096ULL ? 4096U : (unsigned int)remaining;
        unsigned int batch_index;
        size_t batch_bytes = (size_t)batch_count * 8U;

        if (osm_index_read_exact(index->fd, member_data, batch_bytes) != 0) {
            osm_relation_index_close(index);
            osm_index_set_error(error, error_capacity, "could not read relation index members");
            return -1;
        }
        for (batch_index = 0U; batch_index < batch_count; ++batch_index) {
            index->members[member_index + (unsigned long long)batch_index] = (long long)osm_index_read_u64(member_data + (size_t)batch_index * 8U);
        }
        member_index += (unsigned long long)batch_count;
    }
    return 0;
}

void osm_relation_index_close(OsmRelationIndex *index) {
    if (index == 0) return;
    if (index->fd >= 0) (void)platform_close(index->fd);
    rt_free(index->records);
    rt_free(index->members);
    rt_free(index->names);
    index->fd = -1;
    index->count = 0ULL;
    index->member_count = 0ULL;
    index->name_size = 0ULL;
    index->records = 0;
    index->members = 0;
    index->names = 0;
}

int osm_relation_index_find_city(OsmRelationIndex *index, const char *name, OsmRelationIndexRecord *record_out) {
    size_t name_size = rt_strlen(name);
    unsigned long long record_index;
    unsigned int best_score = 0U;
    int found = 0;

    for (record_index = 0ULL; record_index < index->count; ++record_index) {
        const OsmRelationIndexRecord *record = &index->records[record_index];
        if (record->name_offset > index->name_size || (unsigned long long)record->name_size > index->name_size - record->name_offset) continue;
        if ((size_t)record->name_size != name_size) continue;
        if (memcmp(index->names + record->name_offset, name, name_size) != 0) continue;
        if (!found || record->score > best_score) {
            *record_out = *record;
            best_score = record->score;
            found = 1;
        }
    }
    return found;
}

static int osm_spatial_find_loaded_node(const OsmNodeIndexRecord *records, unsigned long long count, long long id, OsmNodeIndexRecord *record_out) {
    unsigned long long left = 0ULL;
    unsigned long long right = count;

    while (left < right) {
        unsigned long long mid = left + (right - left) / 2ULL;
        const OsmNodeIndexRecord *record = &records[mid];
        if (record->id == id) {
            *record_out = *record;
            return 1;
        }
        if (record->id < id) left = mid + 1ULL;
        else right = mid;
    }
    return 0;
}

int osm_spatial_index_build(const char *node_index_path, const char *way_index_path, const char *spatial_index_path, unsigned long long *way_count_out, unsigned int flags, char *error, size_t error_capacity) {
    OsmNodeIndex node_index;
    OsmWayIndex way_index;
    OsmNodeIndexRecord *node_records;
    unsigned long long node_count;
    OsmIndexWriter writer;
    unsigned char header[OSM_SPATIAL_INDEX_HEADER_SIZE];
    unsigned char way_data[OSM_WAY_INDEX_RECORD_SIZE];
    unsigned char spatial_data[OSM_SPATIAL_INDEX_RECORD_SIZE];
    unsigned long long way_index_position;
    unsigned long long written_count = 0ULL;
    char index_error[OSM_INDEX_ERROR_CAPACITY];

    rt_memset(&node_index, 0, sizeof(node_index));
    rt_memset(&way_index, 0, sizeof(way_index));
    writer.fd = -1;
    node_records = 0;
    node_count = 0ULL;
    index_error[0] = '\0';
    if (osm_node_index_open(&node_index, node_index_path, index_error, sizeof(index_error)) != 0) {
        osm_index_set_error(error, error_capacity, index_error[0] == '\0' ? "could not open node index" : index_error);
        return -1;
    }
    if (osm_node_index_load_records(&node_index, &node_records, &node_count, index_error, sizeof(index_error)) != 0) {
        osm_node_index_close(&node_index);
        osm_index_set_error(error, error_capacity, index_error[0] == '\0' ? "could not load node index" : index_error);
        return -1;
    }
    if (osm_way_index_open(&way_index, way_index_path, index_error, sizeof(index_error)) != 0) {
        rt_free(node_records);
        osm_node_index_close(&node_index);
        osm_index_set_error(error, error_capacity, index_error[0] == '\0' ? "could not open way index" : index_error);
        return -1;
    }
    if (osm_index_writer_open(&writer, spatial_index_path, 0644U, error, error_capacity, "could not open spatial index output file") != 0) {
        osm_way_index_close(&way_index);
        rt_free(node_records);
        osm_node_index_close(&node_index);
        return -1;
    }
    osm_spatial_index_make_header(header, 0ULL);
    if (osm_index_writer_write(&writer, header, sizeof(header)) != 0) goto fail_write;
    for (way_index_position = 0ULL; way_index_position < way_index.count; ++way_index_position) {
        OsmWayIndexRecord way_record;
        OsmSpatialIndexRecord spatial_record;
        long long *refs = 0;
        unsigned int ref_index;
        int has_node = 0;
        long long record_offset = (long long)OSM_WAY_INDEX_HEADER_SIZE + (long long)(way_index_position * OSM_WAY_INDEX_RECORD_SIZE);

        if (platform_seek(way_index.fd, record_offset, PLATFORM_SEEK_SET) != record_offset) goto fail_read;
        if (osm_index_read_exact(way_index.fd, way_data, sizeof(way_data)) != 0) goto fail_read;
        osm_way_index_read_record_data(way_data, &way_record);
        if (osm_way_index_read_refs(&way_index, &way_record, &refs, index_error, sizeof(index_error)) != 0) goto fail_read;
        spatial_record.id = way_record.id;
        spatial_record.min_lon_nano = 9223372036854775807LL;
        spatial_record.min_lat_nano = 9223372036854775807LL;
        spatial_record.max_lon_nano = -9223372036854775807LL;
        spatial_record.max_lat_nano = -9223372036854775807LL;
        for (ref_index = 0U; ref_index < way_record.ref_count; ++ref_index) {
            OsmNodeIndexRecord node_record;
            if (!osm_spatial_find_loaded_node(node_records, node_count, refs[ref_index], &node_record)) continue;
            if (node_record.lon_nano < spatial_record.min_lon_nano) spatial_record.min_lon_nano = node_record.lon_nano;
            if (node_record.lon_nano > spatial_record.max_lon_nano) spatial_record.max_lon_nano = node_record.lon_nano;
            if (node_record.lat_nano < spatial_record.min_lat_nano) spatial_record.min_lat_nano = node_record.lat_nano;
            if (node_record.lat_nano > spatial_record.max_lat_nano) spatial_record.max_lat_nano = node_record.lat_nano;
            has_node = 1;
        }
        rt_free(refs);
        if (!has_node) continue;
        osm_spatial_index_write_record_data(spatial_data, &spatial_record);
        if (osm_index_writer_write(&writer, spatial_data, sizeof(spatial_data)) != 0) goto fail_write;
        written_count += 1ULL;
        if ((flags & OSM_INDEX_BUILD_PROGRESS) != 0U && written_count % OSM_WAY_INDEX_PROGRESS_INTERVAL == 0ULL) {
            osm_index_write_progress_label("spatial_ways_indexed: ", written_count);
        }
    }
    if (osm_index_writer_seek(&writer, 0, PLATFORM_SEEK_SET) != 0) goto fail_write;
    osm_spatial_index_make_header(header, written_count);
    if (rt_write_all(writer.fd, header, sizeof(header)) != 0) goto fail_write;
    if (osm_index_writer_close(&writer) != 0) {
        osm_index_set_error(error, error_capacity, "could not close spatial index");
        osm_way_index_close(&way_index);
        rt_free(node_records);
        osm_node_index_close(&node_index);
        return -1;
    }
    if (way_count_out != 0) *way_count_out = written_count;
    osm_way_index_close(&way_index);
    rt_free(node_records);
    osm_node_index_close(&node_index);
    return 0;

fail_read:
    osm_index_writer_discard(&writer);
    osm_way_index_close(&way_index);
    rt_free(node_records);
    osm_node_index_close(&node_index);
    osm_index_set_error(error, error_capacity, "could not read source indexes while building spatial index");
    return -1;
fail_write:
    osm_index_writer_discard(&writer);
    osm_way_index_close(&way_index);
    rt_free(node_records);
    osm_node_index_close(&node_index);
    osm_index_set_error(error, error_capacity, "could not write spatial index");
    return -1;
}

int osm_spatial_index_open(OsmSpatialIndex *index, const char *path, char *error, size_t error_capacity) {
    unsigned char header[OSM_SPATIAL_INDEX_HEADER_SIZE];
    unsigned char data[OSM_SPATIAL_INDEX_RECORD_SIZE * 1024U];
    unsigned int version;
    unsigned int record_size;
    unsigned long long record_index;

    rt_memset(index, 0, sizeof(*index));
    index->fd = -1;
    index->fd = platform_open_read(path);
    if (index->fd < 0) {
        osm_index_set_error(error, error_capacity, "could not open spatial index");
        return -1;
    }
    if (osm_index_read_exact(index->fd, header, sizeof(header)) != 0 || memcmp(header, osm_spatial_index_magic, sizeof(osm_spatial_index_magic)) != 0) {
        osm_spatial_index_close(index);
        osm_index_set_error(error, error_capacity, "invalid spatial index header");
        return -1;
    }
    version = osm_index_read_u32(header + 8U);
    record_size = osm_index_read_u32(header + 12U);
    if (version != 1U || record_size != OSM_SPATIAL_INDEX_RECORD_SIZE) {
        osm_spatial_index_close(index);
        osm_index_set_error(error, error_capacity, "unsupported spatial index version");
        return -1;
    }
    index->count = osm_index_read_u64(header + 16U);
    if (index->count > (unsigned long long)(((size_t)-1) / sizeof(OsmSpatialIndexRecord))) {
        osm_spatial_index_close(index);
        osm_index_set_error(error, error_capacity, "spatial index is too large to load");
        return -1;
    }
    index->records = (OsmSpatialIndexRecord *)rt_malloc(sizeof(OsmSpatialIndexRecord) * (size_t)index->count);
    if (index->records == 0 && index->count != 0ULL) {
        osm_spatial_index_close(index);
        osm_index_set_error(error, error_capacity, "out of memory while loading spatial index");
        return -1;
    }
    record_index = 0ULL;
    while (record_index < index->count) {
        unsigned long long remaining = index->count - record_index;
        unsigned int batch_count = remaining > 1024ULL ? 1024U : (unsigned int)remaining;
        unsigned int batch_index;
        size_t batch_bytes = (size_t)batch_count * OSM_SPATIAL_INDEX_RECORD_SIZE;

        if (osm_index_read_exact(index->fd, data, batch_bytes) != 0) {
            osm_spatial_index_close(index);
            osm_index_set_error(error, error_capacity, "could not read spatial index records");
            return -1;
        }
        for (batch_index = 0U; batch_index < batch_count; ++batch_index) {
            osm_spatial_index_read_record_data(data + (size_t)batch_index * OSM_SPATIAL_INDEX_RECORD_SIZE, &index->records[record_index + (unsigned long long)batch_index]);
        }
        record_index += (unsigned long long)batch_count;
    }
    return 0;
}

void osm_spatial_index_close(OsmSpatialIndex *index) {
    if (index == 0) return;
    if (index->fd >= 0) (void)platform_close(index->fd);
    rt_free(index->records);
    index->fd = -1;
    index->count = 0ULL;
    index->records = 0;
}

int osm_spatial_index_way_intersects(OsmSpatialIndex *index, long long id, long long min_lon_nano, long long min_lat_nano, long long max_lon_nano, long long max_lat_nano) {
    unsigned long long left = 0ULL;
    unsigned long long right = index->count;

    while (left < right) {
        unsigned long long mid = left + (right - left) / 2ULL;
        const OsmSpatialIndexRecord *record = &index->records[mid];
        if (record->id == id) {
            if (record->max_lon_nano < min_lon_nano || record->min_lon_nano > max_lon_nano) return 0;
            if (record->max_lat_nano < min_lat_nano || record->min_lat_nano > max_lat_nano) return 0;
            return 1;
        }
        if (record->id < id) left = mid + 1ULL;
        else right = mid;
    }
    return 0;
}