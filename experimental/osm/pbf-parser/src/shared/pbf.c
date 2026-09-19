#include "pbf.h"

#include "compression/zlib.h"
#include "platform.h"
#include "runtime.h"

#define PBF_MAX_BLOB_HEADER_SIZE (64U * 1024U)
#define PBF_MAX_BLOB_RAW_SIZE (32U * 1024U * 1024U)
#define PBF_PARALLEL_MAX_WORKERS 64U
#define PBF_PARALLEL_QUEUE_MULTIPLIER 2U

typedef struct {
    const unsigned char *cursor;
    const unsigned char *end;
} PbfReader;

typedef struct {
    char type[32];
    unsigned int datasize;
} PbfBlobHeader;

typedef struct {
    unsigned char *data;
    size_t size;
    int is_zlib;
    int should_free;
} PbfDecodedBlob;

typedef struct {
    PbfBlobHeader header;
    unsigned char *blob_data;
    size_t blob_size;
    size_t capacity;
    int stop;
} PbfParallelSlot;

typedef struct {
    PbfParallelSlot *slots;
    unsigned int *free_indices;
    unsigned int *ready_indices;
    unsigned int slot_count;
    unsigned int free_read_index;
    unsigned int free_write_index;
    unsigned int ready_read_index;
    unsigned int ready_write_index;
    PlatformSemaphore free_slots;
    PlatformSemaphore ready_slots;
    PlatformMutex free_mutex;
    PlatformMutex ready_mutex;
    PlatformMutex merge_mutex;
    PbfSummary summary;
    const PbfStreamParallelOptions *stream_options;
    unsigned char *stream_worker_users;
    volatile int failed;
    volatile int stopped;
    char *error;
    size_t error_capacity;
} PbfParallelContext;

typedef struct {
    PbfParallelContext *context;
    unsigned int worker_index;
} PbfParallelWorkerStart;

typedef struct {
    PbfText *items;
    unsigned int count;
    unsigned int capacity;
} PbfStringTable;

typedef struct {
    PbfStringTable string_table;
    long long granularity;
    long long lat_offset;
    long long lon_offset;
    const PbfStreamCallbacks *callbacks;
    void *user;
    int stop;
} PbfStreamBlockContext;

typedef struct {
    PbfTag *items;
    unsigned int count;
    unsigned int capacity;
} PbfTagList;

typedef struct {
    unsigned int *items;
    unsigned int count;
    unsigned int capacity;
} PbfUintList;

typedef struct {
    long long *items;
    unsigned int count;
    unsigned int capacity;
} PbfInt64List;

typedef struct {
    PbfRelationMember *items;
    unsigned int count;
    unsigned int capacity;
} PbfRelationMemberList;

static void pbf_set_error(char *error, size_t error_capacity, const char *message) {
    if (error != 0 && error_capacity != 0U) {
        rt_copy_string(error, error_capacity, message);
    }
}

static void pbf_summary_merge(PbfSummary *dst, const PbfSummary *src) {
    unsigned int index;

    dst->fileblocks += src->fileblocks;
    dst->header_blocks += src->header_blocks;
    dst->data_blocks += src->data_blocks;
    dst->other_blocks += src->other_blocks;
    dst->raw_blobs += src->raw_blobs;
    dst->zlib_blobs += src->zlib_blobs;
    dst->compressed_bytes += src->compressed_bytes;
    dst->uncompressed_bytes += src->uncompressed_bytes;
    dst->primitive_blocks += src->primitive_blocks;
    dst->primitive_groups += src->primitive_groups;
    dst->dense_node_groups += src->dense_node_groups;
    dst->nodes += src->nodes;
    dst->ways += src->ways;
    dst->relations += src->relations;
    for (index = 0U; index < src->required_feature_count && dst->required_feature_count < PBF_MAX_FEATURES; ++index) {
        dst->required_features[dst->required_feature_count] = src->required_features[index];
        dst->required_feature_count += 1U;
    }
    for (index = 0U; index < src->optional_feature_count && dst->optional_feature_count < PBF_MAX_FEATURES; ++index) {
        dst->optional_features[dst->optional_feature_count] = src->optional_features[index];
        dst->optional_feature_count += 1U;
    }
    if (dst->writing_program[0] == '\0' && src->writing_program[0] != '\0') {
        rt_copy_string(dst->writing_program, sizeof(dst->writing_program), src->writing_program);
    }
    if (dst->source[0] == '\0' && src->source[0] != '\0') {
        rt_copy_string(dst->source, sizeof(dst->source), src->source);
    }
}

static void pbf_parallel_set_error(PbfParallelContext *context, const char *message) {
    platform_mutex_lock(&context->merge_mutex);
    if (!context->failed) {
        context->failed = 1;
        pbf_set_error(context->error, context->error_capacity, message);
    }
    platform_mutex_unlock(&context->merge_mutex);
}

void pbf_summary_init(PbfSummary *summary) {
    rt_memset(summary, 0, sizeof(*summary));
}

static int pbf_read_exact(int fd, unsigned char *buffer, size_t count) {
    size_t offset = 0U;

    while (offset < count) {
        long bytes = platform_read(fd, buffer + offset, count - offset);
        if (bytes <= 0) {
            return -1;
        }
        offset += (size_t)bytes;
    }
    return 0;
}

static unsigned int pbf_read_u32_be(const unsigned char bytes[4]) {
    return ((unsigned int)bytes[0] << 24U) |
           ((unsigned int)bytes[1] << 16U) |
           ((unsigned int)bytes[2] << 8U) |
           (unsigned int)bytes[3];
}

static void pbf_reader_init(PbfReader *reader, const unsigned char *data, size_t size) {
    reader->cursor = data;
    reader->end = data + size;
}

static int pbf_read_varint(PbfReader *reader, unsigned long long *value_out) {
    unsigned long long value = 0ULL;
    unsigned int shift = 0U;

    while (reader->cursor < reader->end && shift < 64U) {
        unsigned char byte = *reader->cursor++;
        value |= ((unsigned long long)(byte & 0x7fU)) << shift;
        if ((byte & 0x80U) == 0U) {
            *value_out = value;
            return 0;
        }
        shift += 7U;
    }
    return -1;
}

static int pbf_read_length(PbfReader *reader, const unsigned char **data_out, size_t *size_out) {
    unsigned long long length;

    if (pbf_read_varint(reader, &length) != 0 || length > (unsigned long long)(reader->end - reader->cursor)) {
        return -1;
    }
    *data_out = reader->cursor;
    *size_out = (size_t)length;
    reader->cursor += (size_t)length;
    return 0;
}

static int pbf_skip_field(PbfReader *reader, unsigned int wire_type) {
    unsigned long long ignored;
    const unsigned char *data;
    size_t size;

    switch (wire_type) {
        case 0:
            return pbf_read_varint(reader, &ignored);
        case 1:
            if ((size_t)(reader->end - reader->cursor) < 8U) return -1;
            reader->cursor += 8U;
            return 0;
        case 2:
            return pbf_read_length(reader, &data, &size);
        case 5:
            if ((size_t)(reader->end - reader->cursor) < 4U) return -1;
            reader->cursor += 4U;
            return 0;
        default:
            return -1;
    }
}

static void pbf_copy_text(char *dst, size_t dst_capacity, const unsigned char *src, size_t src_size) {
    size_t copy_size;

    if (dst_capacity == 0U) {
        return;
    }
    copy_size = src_size;
    if (copy_size + 1U > dst_capacity) {
        copy_size = dst_capacity - 1U;
    }
    if (copy_size != 0U) {
        memcpy(dst, src, copy_size);
    }
    dst[copy_size] = '\0';
}

static int pbf_text_equals(const char *text, const char *expected) {
    return rt_strcmp(text, expected) == 0;
}

static void pbf_add_feature(PbfFeature *features, unsigned int *count_io, const unsigned char *text, size_t text_size) {
    if (*count_io >= PBF_MAX_FEATURES) {
        return;
    }
    pbf_copy_text(features[*count_io].text, sizeof(features[*count_io].text), text, text_size);
    *count_io += 1U;
}

static int pbf_parse_blob_header(const unsigned char *data, size_t size, PbfBlobHeader *header) {
    PbfReader reader;

    header->type[0] = '\0';
    header->datasize = 0U;
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 2U) {
            const unsigned char *text;
            size_t text_size;
            if (pbf_read_length(&reader, &text, &text_size) != 0) return -1;
            pbf_copy_text(header->type, sizeof(header->type), text, text_size);
        } else if (field == 3U && wire_type == 0U) {
            unsigned long long value;
            if (pbf_read_varint(&reader, &value) != 0 || value > PBF_MAX_BLOB_RAW_SIZE) return -1;
            header->datasize = (unsigned int)value;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    return header->type[0] != '\0' && header->datasize != 0U ? 0 : -1;
}

static int pbf_decode_blob(unsigned char *blob_data, size_t blob_size, PbfDecodedBlob *decoded) {
    PbfReader reader;
    unsigned long long raw_size = 0ULL;
    const unsigned char *raw = 0;
    size_t raw_length = 0U;
    const unsigned char *zlib_data = 0;
    size_t zlib_size = 0U;

    decoded->data = 0;
    decoded->size = 0U;
    decoded->is_zlib = 0;
    decoded->should_free = 0;
    pbf_reader_init(&reader, blob_data, blob_size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 2U) {
            if (pbf_read_length(&reader, &raw, &raw_length) != 0) return -1;
        } else if (field == 2U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &raw_size) != 0 || raw_size > PBF_MAX_BLOB_RAW_SIZE) return -1;
        } else if (field == 3U && wire_type == 2U) {
            if (pbf_read_length(&reader, &zlib_data, &zlib_size) != 0) return -1;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }

    if (raw != 0) {
        decoded->data = (unsigned char *)raw;
        decoded->size = raw_length;
        return 0;
    }
    if (zlib_data != 0 && raw_size != 0ULL) {
        size_t output_size = 0U;
        decoded->data = (unsigned char *)rt_malloc((size_t)raw_size);
        if (decoded->data == 0) return -1;
        decoded->should_free = 1;
        decoded->is_zlib = 1;
        if (compression_zlib_inflate(zlib_data, zlib_size, decoded->data, (size_t)raw_size, &output_size) != 0 || output_size != (size_t)raw_size) {
            rt_free(decoded->data);
            decoded->data = 0;
            decoded->should_free = 0;
            return -1;
        }
        decoded->size = output_size;
        return 0;
    }
    return -1;
}

static int pbf_count_packed_varints(const unsigned char *data, size_t size, unsigned long long *count_out) {
    size_t index;
    unsigned int varint_bytes = 0U;

    for (index = 0U; index < size; ++index) {
        varint_bytes += 1U;
        if (varint_bytes > 10U) {
            return -1;
        }
        if ((data[index] & 0x80U) == 0U) {
            *count_out += 1ULL;
            varint_bytes = 0U;
        }
    }
    if (varint_bytes != 0U) {
        return -1;
    }
    return 0;
}

static int pbf_parse_dense_nodes(const unsigned char *data, size_t size, PbfSummary *summary) {
    PbfReader reader;
    unsigned long long node_count = 0ULL;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) return -1;
            if (pbf_count_packed_varints(packed, packed_size, &node_count) != 0) return -1;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    summary->dense_node_groups += 1ULL;
    summary->nodes += node_count;
    return 0;
}

static int pbf_parse_primitive_group(const unsigned char *data, size_t size, PbfSummary *summary) {
    PbfReader reader;

    summary->primitive_groups += 1ULL;
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (wire_type == 2U) {
            const unsigned char *payload;
            size_t payload_size;
            if (pbf_read_length(&reader, &payload, &payload_size) != 0) return -1;
            if (field == 1U) {
                summary->nodes += 1ULL;
            } else if (field == 2U) {
                if (pbf_parse_dense_nodes(payload, payload_size, summary) != 0) return -1;
            } else if (field == 3U) {
                summary->ways += 1ULL;
            } else if (field == 4U) {
                summary->relations += 1ULL;
            }
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    return 0;
}

static int pbf_parse_primitive_block(const unsigned char *data, size_t size, PbfSummary *summary) {
    PbfReader reader;

    summary->primitive_blocks += 1ULL;
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 2U && wire_type == 2U) {
            const unsigned char *group;
            size_t group_size;
            if (pbf_read_length(&reader, &group, &group_size) != 0) return -1;
            if (pbf_parse_primitive_group(group, group_size, summary) != 0) return -1;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    return 0;
}

static int pbf_parse_header_block(const unsigned char *data, size_t size, PbfSummary *summary) {
    PbfReader reader;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (wire_type == 2U) {
            const unsigned char *payload;
            size_t payload_size;
            if (pbf_read_length(&reader, &payload, &payload_size) != 0) return -1;
            if (field == 4U) {
                pbf_add_feature(summary->required_features, &summary->required_feature_count, payload, payload_size);
            } else if (field == 5U) {
                pbf_add_feature(summary->optional_features, &summary->optional_feature_count, payload, payload_size);
            } else if (field == 16U) {
                pbf_copy_text(summary->writing_program, sizeof(summary->writing_program), payload, payload_size);
            } else if (field == 17U) {
                pbf_copy_text(summary->source, sizeof(summary->source), payload, payload_size);
            }
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    return 0;
}

static long long pbf_decode_zigzag64(unsigned long long value) {
    return (long long)(value >> 1U) ^ -((long long)(value & 1ULL));
}

static int pbf_text_is_empty(PbfText text) {
    return text.data == 0 || text.size == 0U;
}

static int pbf_grow_array(void **items_io, unsigned int *capacity_io, size_t item_size, unsigned int needed) {
    unsigned int capacity = *capacity_io;
    void *items;

    if (needed <= capacity) {
        return 0;
    }
    if (capacity == 0U) {
        capacity = 8U;
    }
    while (capacity < needed) {
        if (capacity > 0x7fffffffU) {
            return -1;
        }
        capacity *= 2U;
    }
    items = rt_realloc(*items_io, item_size * (size_t)capacity);
    if (items == 0) {
        return -1;
    }
    *items_io = items;
    *capacity_io = capacity;
    return 0;
}

static int pbf_string_table_append(PbfStringTable *table, const unsigned char *data, size_t size) {
    if (pbf_grow_array((void **)&table->items, &table->capacity, sizeof(PbfText), table->count + 1U) != 0) {
        return -1;
    }
    table->items[table->count].data = (const char *)data;
    table->items[table->count].size = size;
    table->count += 1U;
    return 0;
}

static int pbf_uint_list_append(PbfUintList *list, unsigned int value) {
    if (pbf_grow_array((void **)&list->items, &list->capacity, sizeof(unsigned int), list->count + 1U) != 0) {
        return -1;
    }
    list->items[list->count] = value;
    list->count += 1U;
    return 0;
}

static int pbf_int64_list_append(PbfInt64List *list, long long value) {
    if (pbf_grow_array((void **)&list->items, &list->capacity, sizeof(long long), list->count + 1U) != 0) {
        return -1;
    }
    list->items[list->count] = value;
    list->count += 1U;
    return 0;
}

static int pbf_tag_list_append(PbfTagList *list, PbfText key, PbfText value) {
    if (pbf_grow_array((void **)&list->items, &list->capacity, sizeof(PbfTag), list->count + 1U) != 0) {
        return -1;
    }
    list->items[list->count].key = key;
    list->items[list->count].value = value;
    list->count += 1U;
    return 0;
}

static int pbf_member_list_append(PbfRelationMemberList *list, PbfRelationMember member) {
    if (pbf_grow_array((void **)&list->items, &list->capacity, sizeof(PbfRelationMember), list->count + 1U) != 0) {
        return -1;
    }
    list->items[list->count] = member;
    list->count += 1U;
    return 0;
}

static int pbf_resolve_string(const PbfStringTable *table, unsigned int index, PbfText *text_out) {
    if (index >= table->count) {
        return -1;
    }
    *text_out = table->items[index];
    return 0;
}

static int pbf_parse_string_table(const unsigned char *data, size_t size, PbfStringTable *table) {
    PbfReader reader;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 2U) {
            const unsigned char *text;
            size_t text_size;
            if (pbf_read_length(&reader, &text, &text_size) != 0) return -1;
            if (pbf_string_table_append(table, text, text_size) != 0) return -1;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    return 0;
}

static int pbf_parse_packed_uints(const unsigned char *data, size_t size, PbfUintList *list) {
    PbfReader reader;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long value;
        if (pbf_read_varint(&reader, &value) != 0 || value > 4294967295ULL) return -1;
        if (pbf_uint_list_append(list, (unsigned int)value) != 0) return -1;
    }
    return 0;
}

static int pbf_parse_packed_sints(const unsigned char *data, size_t size, PbfInt64List *list) {
    PbfReader reader;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long value;
        if (pbf_read_varint(&reader, &value) != 0) return -1;
        if (pbf_int64_list_append(list, pbf_decode_zigzag64(value)) != 0) return -1;
    }
    return 0;
}

static int pbf_build_tags(const PbfStringTable *string_table, const PbfUintList *keys, const PbfUintList *values, PbfTagList *tags) {
    unsigned int index;

    if (keys->count != values->count) {
        return -1;
    }
    tags->count = 0U;
    for (index = 0U; index < keys->count; ++index) {
        PbfText key;
        PbfText value;
        if (pbf_resolve_string(string_table, keys->items[index], &key) != 0) return -1;
        if (pbf_resolve_string(string_table, values->items[index], &value) != 0) return -1;
        if (pbf_tag_list_append(tags, key, value) != 0) return -1;
    }
    return 0;
}

static int pbf_stream_skip_node_tags(const PbfStreamBlockContext *context) {
    return context->callbacks != 0 && (context->callbacks->flags & PBF_STREAM_SKIP_NODE_TAGS) != 0U;
}

static int pbf_stream_node_payload(const unsigned char *data, size_t size, PbfStreamBlockContext *context) {
    PbfReader reader;
    PbfUintList keys;
    PbfUintList values;
    PbfTagList tags;
    PbfNode node;
    long long raw_lat = 0;
    long long raw_lon = 0;

    rt_memset(&keys, 0, sizeof(keys));
    rt_memset(&values, 0, sizeof(values));
    rt_memset(&tags, 0, sizeof(tags));
    rt_memset(&node, 0, sizeof(node));
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned long long value;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) goto fail;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            node.id = pbf_decode_zigzag64(value);
        } else if (field == 2U && wire_type == 2U && !pbf_stream_skip_node_tags(context)) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (pbf_parse_packed_uints(packed, packed_size, &keys) != 0) goto fail;
        } else if (field == 3U && wire_type == 2U && !pbf_stream_skip_node_tags(context)) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (pbf_parse_packed_uints(packed, packed_size, &values) != 0) goto fail;
        } else if (field == 8U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            raw_lat = pbf_decode_zigzag64(value);
        } else if (field == 9U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            raw_lon = pbf_decode_zigzag64(value);
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            goto fail;
        }
    }
    if (!pbf_stream_skip_node_tags(context) && pbf_build_tags(&context->string_table, &keys, &values, &tags) != 0) goto fail;
    node.lat_nano = context->lat_offset + context->granularity * raw_lat;
    node.lon_nano = context->lon_offset + context->granularity * raw_lon;
    node.tags = tags.items;
    node.tag_count = tags.count;
    if (context->callbacks != 0 && context->callbacks->node != 0) {
        if (context->callbacks->node(context->user, &node) != 0) {
            context->stop = 1;
        }
    }
    rt_free(keys.items);
    rt_free(values.items);
    rt_free(tags.items);
    return context->stop ? 1 : 0;

fail:
    rt_free(keys.items);
    rt_free(values.items);
    rt_free(tags.items);
    return -1;
}

static int pbf_stream_dense_nodes_no_tags(const unsigned char *data, size_t size, PbfStreamBlockContext *context) {
    PbfReader reader;
    PbfReader ids;
    PbfReader lats;
    PbfReader lons;
    const unsigned char *id_data = 0;
    const unsigned char *lat_data = 0;
    const unsigned char *lon_data = 0;
    size_t id_size = 0U;
    size_t lat_size = 0U;
    size_t lon_size = 0U;
    long long id = 0;
    long long lat = 0;
    long long lon = 0;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 2U) {
            if (pbf_read_length(&reader, &id_data, &id_size) != 0) return -1;
        } else if (field == 8U && wire_type == 2U) {
            if (pbf_read_length(&reader, &lat_data, &lat_size) != 0) return -1;
        } else if (field == 9U && wire_type == 2U) {
            if (pbf_read_length(&reader, &lon_data, &lon_size) != 0) return -1;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    if (id_data == 0 || lat_data == 0 || lon_data == 0) return -1;
    pbf_reader_init(&ids, id_data, id_size);
    pbf_reader_init(&lats, lat_data, lat_size);
    pbf_reader_init(&lons, lon_data, lon_size);
    while (ids.cursor < ids.end) {
        unsigned long long raw_id;
        unsigned long long raw_lat;
        unsigned long long raw_lon;
        PbfNode node;

        if (pbf_read_varint(&ids, &raw_id) != 0 || pbf_read_varint(&lats, &raw_lat) != 0 || pbf_read_varint(&lons, &raw_lon) != 0) return -1;
        id += pbf_decode_zigzag64(raw_id);
        lat += pbf_decode_zigzag64(raw_lat);
        lon += pbf_decode_zigzag64(raw_lon);
        rt_memset(&node, 0, sizeof(node));
        node.id = id;
        node.lat_nano = context->lat_offset + context->granularity * lat;
        node.lon_nano = context->lon_offset + context->granularity * lon;
        if (context->callbacks->node(context->user, &node) != 0) {
            context->stop = 1;
            break;
        }
    }
    if (!context->stop && (lats.cursor != lats.end || lons.cursor != lons.end)) return -1;
    return context->stop ? 1 : 0;
}

static int pbf_stream_dense_nodes(const unsigned char *data, size_t size, PbfStreamBlockContext *context) {
    PbfReader reader;
    PbfInt64List ids;
    PbfInt64List lats;
    PbfInt64List lons;
    PbfUintList keys_values;
    PbfTagList tags;
    unsigned int node_index;
    unsigned int key_value_index = 0U;
    long long id = 0;
    long long lat = 0;
    long long lon = 0;

    if (pbf_stream_skip_node_tags(context)) {
        return pbf_stream_dense_nodes_no_tags(data, size, context);
    }

    rt_memset(&ids, 0, sizeof(ids));
    rt_memset(&lats, 0, sizeof(lats));
    rt_memset(&lons, 0, sizeof(lons));
    rt_memset(&keys_values, 0, sizeof(keys_values));
    rt_memset(&tags, 0, sizeof(tags));
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) goto fail;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if ((field == 1U || field == 8U || field == 9U) && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            PbfInt64List *target = field == 1U ? &ids : (field == 8U ? &lats : &lons);
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (pbf_parse_packed_sints(packed, packed_size, target) != 0) goto fail;
        } else if (field == 10U && wire_type == 2U && !pbf_stream_skip_node_tags(context)) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (pbf_parse_packed_uints(packed, packed_size, &keys_values) != 0) goto fail;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            goto fail;
        }
    }
    if (ids.count != lats.count || ids.count != lons.count) goto fail;
    for (node_index = 0U; node_index < ids.count; ++node_index) {
        PbfNode node;

        id += ids.items[node_index];
        lat += lats.items[node_index];
        lon += lons.items[node_index];
        tags.count = 0U;
        if (keys_values.count != 0U) {
            while (key_value_index < keys_values.count && keys_values.items[key_value_index] != 0U) {
                PbfText key;
                PbfText value;
                unsigned int key_index = keys_values.items[key_value_index];
                key_value_index += 1U;
                if (key_value_index >= keys_values.count) goto fail;
                if (pbf_resolve_string(&context->string_table, key_index, &key) != 0) goto fail;
                if (pbf_resolve_string(&context->string_table, keys_values.items[key_value_index], &value) != 0) goto fail;
                key_value_index += 1U;
                if (pbf_tag_list_append(&tags, key, value) != 0) goto fail;
            }
            if (key_value_index < keys_values.count && keys_values.items[key_value_index] == 0U) {
                key_value_index += 1U;
            }
        }
        node.id = id;
        node.lat_nano = context->lat_offset + context->granularity * lat;
        node.lon_nano = context->lon_offset + context->granularity * lon;
        node.tags = tags.items;
        node.tag_count = tags.count;
        if (context->callbacks != 0 && context->callbacks->node != 0) {
            if (context->callbacks->node(context->user, &node) != 0) {
                context->stop = 1;
                break;
            }
        }
    }
    rt_free(ids.items);
    rt_free(lats.items);
    rt_free(lons.items);
    rt_free(keys_values.items);
    rt_free(tags.items);
    return context->stop ? 1 : 0;

fail:
    rt_free(ids.items);
    rt_free(lats.items);
    rt_free(lons.items);
    rt_free(keys_values.items);
    rt_free(tags.items);
    return -1;
}

static int pbf_stream_way_payload(const unsigned char *data, size_t size, PbfStreamBlockContext *context) {
    PbfReader reader;
    PbfUintList keys;
    PbfUintList values;
    PbfInt64List refs;
    PbfTagList tags;
    PbfWay way;
    unsigned int index;
    long long ref = 0;
    int tags_ready = 0;
    int id_ready = 0;
    int want_way = 1;
    int skip_way_tags = context->callbacks != 0 && (context->callbacks->flags & PBF_STREAM_SKIP_WAY_TAGS) != 0U;

    rt_memset(&keys, 0, sizeof(keys));
    rt_memset(&values, 0, sizeof(values));
    rt_memset(&refs, 0, sizeof(refs));
    rt_memset(&tags, 0, sizeof(tags));
    rt_memset(&way, 0, sizeof(way));
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned long long value;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) goto fail;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            way.id = (long long)value;
            id_ready = 1;
            if (context->callbacks != 0 && context->callbacks->way_id != 0 && context->callbacks->way_id(context->user, way.id) == 0) {
                want_way = 0;
            }
        } else if (field == 2U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (want_way && !skip_way_tags && pbf_parse_packed_uints(packed, packed_size, &keys) != 0) goto fail;
        } else if (field == 3U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (want_way && !skip_way_tags && pbf_parse_packed_uints(packed, packed_size, &values) != 0) goto fail;
        } else if (field == 8U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (want_way && !tags_ready) {
                if (!skip_way_tags && pbf_build_tags(&context->string_table, &keys, &values, &tags) != 0) goto fail;
                tags_ready = 1;
                if (context->callbacks != 0 && context->callbacks->way_tags != 0 &&
                    context->callbacks->way_tags(context->user, way.id, tags.items, tags.count) == 0) {
                    want_way = 0;
                }
            }
            if (want_way && pbf_parse_packed_sints(packed, packed_size, &refs) != 0) goto fail;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            goto fail;
        }
    }
    if (!id_ready && context->callbacks != 0 && context->callbacks->way_id != 0 && context->callbacks->way_id(context->user, way.id) == 0) {
        want_way = 0;
    }
    if (want_way && !tags_ready) {
        if (!skip_way_tags && pbf_build_tags(&context->string_table, &keys, &values, &tags) != 0) goto fail;
        tags_ready = 1;
        if (context->callbacks != 0 && context->callbacks->way_tags != 0 &&
            context->callbacks->way_tags(context->user, way.id, tags.items, tags.count) == 0) {
            want_way = 0;
        }
    }
    if (!want_way) {
        rt_free(keys.items);
        rt_free(values.items);
        rt_free(refs.items);
        rt_free(tags.items);
        return 0;
    }
    for (index = 0U; index < refs.count; ++index) {
        ref += refs.items[index];
        refs.items[index] = ref;
    }
    way.refs = refs.items;
    way.ref_count = refs.count;
    way.tags = tags.items;
    way.tag_count = tags.count;
    if (context->callbacks != 0 && context->callbacks->way != 0) {
        if (context->callbacks->way(context->user, &way) != 0) {
            context->stop = 1;
        }
    }
    rt_free(keys.items);
    rt_free(values.items);
    rt_free(refs.items);
    rt_free(tags.items);
    return context->stop ? 1 : 0;

fail:
    rt_free(keys.items);
    rt_free(values.items);
    rt_free(refs.items);
    rt_free(tags.items);
    return -1;
}

static int pbf_stream_relation_payload(const unsigned char *data, size_t size, PbfStreamBlockContext *context) {
    PbfReader reader;
    PbfUintList keys;
    PbfUintList values;
    PbfUintList roles;
    PbfInt64List member_ids;
    PbfUintList member_types;
    PbfRelationMemberList members;
    PbfTagList tags;
    PbfRelation relation;
    unsigned int index;
    long long member_id = 0;
    int want_members = context->callbacks != 0 && context->callbacks->relation != 0;
    int want_relation = 1;
    const unsigned char *roles_payload = 0;
    const unsigned char *member_ids_payload = 0;
    const unsigned char *member_types_payload = 0;
    size_t roles_payload_size = 0;
    size_t member_ids_payload_size = 0;
    size_t member_types_payload_size = 0;
    int skip_relation_roles = context->callbacks != 0 && (context->callbacks->flags & PBF_STREAM_SKIP_RELATION_ROLES) != 0U;

    rt_memset(&keys, 0, sizeof(keys));
    rt_memset(&values, 0, sizeof(values));
    rt_memset(&roles, 0, sizeof(roles));
    rt_memset(&member_ids, 0, sizeof(member_ids));
    rt_memset(&member_types, 0, sizeof(member_types));
    rt_memset(&members, 0, sizeof(members));
    rt_memset(&tags, 0, sizeof(tags));
    rt_memset(&relation, 0, sizeof(relation));
    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned long long value;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) goto fail;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            relation.id = (long long)value;
        } else if (field == 2U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (pbf_parse_packed_uints(packed, packed_size, &keys) != 0) goto fail;
        } else if (field == 3U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (pbf_parse_packed_uints(packed, packed_size, &values) != 0) goto fail;
        } else if (field == 8U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (want_members && !skip_relation_roles) {
                roles_payload = packed;
                roles_payload_size = packed_size;
            }
        } else if (field == 9U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (want_members) {
                member_ids_payload = packed;
                member_ids_payload_size = packed_size;
            }
        } else if (field == 10U && wire_type == 2U) {
            const unsigned char *packed;
            size_t packed_size;
            if (pbf_read_length(&reader, &packed, &packed_size) != 0) goto fail;
            if (want_members) {
                member_types_payload = packed;
                member_types_payload_size = packed_size;
            }
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            goto fail;
        }
    }
    if (pbf_build_tags(&context->string_table, &keys, &values, &tags) != 0) goto fail;
    if (context->callbacks != 0 && context->callbacks->relation_tags != 0 &&
        context->callbacks->relation_tags(context->user, relation.id, tags.items, tags.count) == 0) {
        want_relation = 0;
    }
    if (!want_relation || !want_members) {
        rt_free(keys.items);
        rt_free(values.items);
        rt_free(roles.items);
        rt_free(member_ids.items);
        rt_free(member_types.items);
        rt_free(members.items);
        rt_free(tags.items);
        return context->stop ? 1 : 0;
    }
    if (!skip_relation_roles && roles_payload != 0 && pbf_parse_packed_uints(roles_payload, roles_payload_size, &roles) != 0) goto fail;
    if (member_ids_payload != 0 && pbf_parse_packed_sints(member_ids_payload, member_ids_payload_size, &member_ids) != 0) goto fail;
    if (member_types_payload != 0 && pbf_parse_packed_uints(member_types_payload, member_types_payload_size, &member_types) != 0) goto fail;
    if (member_ids.count != member_types.count || (!skip_relation_roles && roles.count != member_ids.count)) goto fail;
    for (index = 0U; index < member_ids.count; ++index) {
        PbfRelationMember member;
        member_id += member_ids.items[index];
        member.id = member_id;
        member.type = member_types.items[index];
        if (skip_relation_roles) {
            member.role.data = "";
            member.role.size = 0U;
        } else if (pbf_resolve_string(&context->string_table, roles.items[index], &member.role) != 0) goto fail;
        if (pbf_member_list_append(&members, member) != 0) goto fail;
    }
    relation.members = members.items;
    relation.member_count = members.count;
    relation.tags = tags.items;
    relation.tag_count = tags.count;
    if (context->callbacks != 0 && context->callbacks->relation != 0) {
        if (context->callbacks->relation(context->user, &relation) != 0) {
            context->stop = 1;
        }
    }
    rt_free(keys.items);
    rt_free(values.items);
    rt_free(roles.items);
    rt_free(member_ids.items);
    rt_free(member_types.items);
    rt_free(members.items);
    rt_free(tags.items);
    return context->stop ? 1 : 0;

fail:
    rt_free(keys.items);
    rt_free(values.items);
    rt_free(roles.items);
    rt_free(member_ids.items);
    rt_free(member_types.items);
    rt_free(members.items);
    rt_free(tags.items);
    return -1;
}

static int pbf_stream_primitive_group(const unsigned char *data, size_t size, PbfStreamBlockContext *context) {
    PbfReader reader;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;
        int result;

        if (pbf_read_varint(&reader, &key) != 0) return -1;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (wire_type == 2U) {
            const unsigned char *payload;
            size_t payload_size;
            if (pbf_read_length(&reader, &payload, &payload_size) != 0) return -1;
            if (field == 1U && context->callbacks != 0 && context->callbacks->node != 0) {
                result = pbf_stream_node_payload(payload, payload_size, context);
            } else if (field == 2U && context->callbacks != 0 && context->callbacks->node != 0) {
                result = pbf_stream_dense_nodes(payload, payload_size, context);
            } else if (field == 3U && context->callbacks != 0 && (context->callbacks->way != 0 || context->callbacks->way_tags != 0)) {
                result = pbf_stream_way_payload(payload, payload_size, context);
            } else if (field == 4U && context->callbacks != 0 && (context->callbacks->relation != 0 || context->callbacks->relation_tags != 0)) {
                result = pbf_stream_relation_payload(payload, payload_size, context);
            } else {
                result = 0;
            }
            if (result != 0) return result;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            return -1;
        }
    }
    return 0;
}

static int pbf_stream_primitive_block(const unsigned char *data, size_t size, const PbfStreamCallbacks *callbacks, void *user) {
    PbfReader reader;
    PbfStreamBlockContext context;
    int result = 0;

    rt_memset(&context, 0, sizeof(context));
    context.granularity = 100;
    context.callbacks = callbacks;
    context.user = user;

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned long long value;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) goto fail;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 1U && wire_type == 2U) {
            const unsigned char *string_table;
            size_t string_table_size;
            if (pbf_read_length(&reader, &string_table, &string_table_size) != 0) goto fail;
            if (pbf_parse_string_table(string_table, string_table_size, &context.string_table) != 0) goto fail;
        } else if (field == 17U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            context.granularity = (long long)value;
        } else if (field == 19U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            context.lat_offset = (long long)value;
        } else if (field == 20U && wire_type == 0U) {
            if (pbf_read_varint(&reader, &value) != 0) goto fail;
            context.lon_offset = (long long)value;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            goto fail;
        }
    }

    pbf_reader_init(&reader, data, size);
    while (reader.cursor < reader.end) {
        unsigned long long key;
        unsigned int field;
        unsigned int wire_type;

        if (pbf_read_varint(&reader, &key) != 0) goto fail;
        field = (unsigned int)(key >> 3U);
        wire_type = (unsigned int)(key & 7U);
        if (field == 2U && wire_type == 2U) {
            const unsigned char *group;
            size_t group_size;
            if (pbf_read_length(&reader, &group, &group_size) != 0) goto fail;
            result = pbf_stream_primitive_group(group, group_size, &context);
            if (result != 0) break;
        } else if (pbf_skip_field(&reader, wire_type) != 0) {
            goto fail;
        }
    }
    rt_free(context.string_table.items);
    return result;

fail:
    rt_free(context.string_table.items);
    return -1;
}

static int pbf_parse_decoded_blob(const PbfBlobHeader *header, const PbfDecodedBlob *decoded, PbfSummary *summary) {
    if (decoded->is_zlib) summary->zlib_blobs += 1ULL;
    else summary->raw_blobs += 1ULL;
    summary->uncompressed_bytes += (unsigned long long)decoded->size;

    if (pbf_text_equals(header->type, "OSMHeader")) {
        summary->header_blocks += 1ULL;
        return pbf_parse_header_block(decoded->data, decoded->size, summary);
    }
    if (pbf_text_equals(header->type, "OSMData")) {
        summary->data_blocks += 1ULL;
        return pbf_parse_primitive_block(decoded->data, decoded->size, summary);
    }
    summary->other_blocks += 1ULL;
    return 0;
}

static int pbf_parallel_worker_main(void *arg) {
    PbfParallelContext *context = (PbfParallelContext *)arg;
    PbfSummary local_summary;

    pbf_summary_init(&local_summary);
    for (;;) {
        PbfParallelSlot *slot;
        PbfDecodedBlob decoded;
        unsigned int slot_index;
        int failed;

        platform_semaphore_wait(&context->ready_slots);
        platform_mutex_lock(&context->ready_mutex);
        slot_index = context->ready_indices[context->ready_read_index % context->slot_count];
        context->ready_read_index += 1U;
        platform_mutex_unlock(&context->ready_mutex);
        slot = &context->slots[slot_index];

        if (slot->stop) {
            break;
        }

        failed = __atomic_load_n(&context->failed, __ATOMIC_ACQUIRE);
        if (!failed) {
            local_summary.fileblocks += 1ULL;
            local_summary.compressed_bytes += (unsigned long long)slot->blob_size;
            if (pbf_decode_blob(slot->blob_data, slot->blob_size, &decoded) != 0) {
                if (pbf_text_equals(slot->header.type, "OSMHeader")) {
                    pbf_parallel_set_error(context, "could not decode OSMHeader Blob");
                } else if (pbf_text_equals(slot->header.type, "OSMData")) {
                    pbf_parallel_set_error(context, "could not decode OSMData Blob");
                } else {
                    pbf_parallel_set_error(context, "could not decode Blob");
                }
            } else {
                if (pbf_parse_decoded_blob(&slot->header, &decoded, &local_summary) != 0) {
                    pbf_parallel_set_error(context, "could not parse OSM protobuf payload");
                }
                if (decoded.should_free) {
                    rt_free(decoded.data);
                }
            }
        }
        platform_mutex_lock(&context->free_mutex);
        context->free_indices[context->free_write_index % context->slot_count] = slot_index;
        context->free_write_index += 1U;
        platform_mutex_unlock(&context->free_mutex);
        platform_semaphore_post(&context->free_slots);
    }

    platform_mutex_lock(&context->merge_mutex);
    pbf_summary_merge(&context->summary, &local_summary);
    platform_mutex_unlock(&context->merge_mutex);
    return 0;
}

static int pbf_parallel_stream_worker_main(void *arg) {
    PbfParallelWorkerStart *start = (PbfParallelWorkerStart *)arg;
    PbfParallelContext *context = start->context;
    const PbfStreamParallelOptions *options = context->stream_options;
    void *worker_user = context->stream_worker_users + (size_t)start->worker_index * options->worker_user_size;

    for (;;) {
        PbfParallelSlot *slot;
        PbfDecodedBlob decoded;
        unsigned int slot_index;
        int failed;

        platform_semaphore_wait(&context->ready_slots);
        platform_mutex_lock(&context->ready_mutex);
        slot_index = context->ready_indices[context->ready_read_index % context->slot_count];
        context->ready_read_index += 1U;
        platform_mutex_unlock(&context->ready_mutex);
        slot = &context->slots[slot_index];

        if (slot->stop) {
            break;
        }

        failed = __atomic_load_n(&context->failed, __ATOMIC_ACQUIRE);
        if (!failed && !__atomic_load_n(&context->stopped, __ATOMIC_ACQUIRE) && pbf_text_equals(slot->header.type, "OSMData")) {
            if (pbf_decode_blob(slot->blob_data, slot->blob_size, &decoded) != 0) {
                pbf_parallel_set_error(context, "could not decode OSMData Blob");
            } else {
                int result = pbf_stream_primitive_block(decoded.data, decoded.size, options->callbacks, worker_user);
                if (result < 0) {
                    pbf_parallel_set_error(context, "could not parse OSM protobuf payload");
                } else if (result > 0) {
                    __atomic_store_n(&context->stopped, 1, __ATOMIC_RELEASE);
                }
                if (decoded.should_free) {
                    rt_free(decoded.data);
                }
            }
        }
        platform_mutex_lock(&context->free_mutex);
        context->free_indices[context->free_write_index % context->slot_count] = slot_index;
        context->free_write_index += 1U;
        platform_mutex_unlock(&context->free_mutex);
        platform_semaphore_post(&context->free_slots);
    }

    return 0;
}

static int pbf_parallel_take_free_slot(PbfParallelContext *context, unsigned int *slot_index_out) {
    platform_semaphore_wait(&context->free_slots);
    platform_mutex_lock(&context->free_mutex);
    *slot_index_out = context->free_indices[context->free_read_index % context->slot_count];
    context->free_read_index += 1U;
    platform_mutex_unlock(&context->free_mutex);
    return 0;
}

static void pbf_parallel_post_ready_slot(PbfParallelContext *context, unsigned int slot_index) {
    platform_mutex_lock(&context->ready_mutex);
    context->ready_indices[context->ready_write_index % context->slot_count] = slot_index;
    context->ready_write_index += 1U;
    platform_mutex_unlock(&context->ready_mutex);
    platform_semaphore_post(&context->ready_slots);
}

static int pbf_parallel_enqueue_stop(PbfParallelContext *context) {
    PbfParallelSlot *slot;
    unsigned int slot_index;

    if (pbf_parallel_take_free_slot(context, &slot_index) != 0) {
        return -1;
    }
    slot = &context->slots[slot_index];
    slot->stop = 1;
    slot->blob_size = 0U;
    pbf_parallel_post_ready_slot(context, slot_index);
    return 0;
}

static int pbf_parallel_read_blocks(int fd, PbfParallelContext *context, unsigned int worker_count) {
    unsigned char length_bytes[4];

    for (;;) {
        long first = platform_read(fd, length_bytes, sizeof(length_bytes));
        unsigned int header_size;
        unsigned char *header_data;
        PbfBlobHeader header;
        PbfParallelSlot *slot;
        unsigned int slot_index;

        if (first == 0) {
            break;
        }
        if (first != (long)sizeof(length_bytes) || pbf_read_exact(fd, length_bytes + (size_t)first, sizeof(length_bytes) - (size_t)first) != 0) {
            pbf_parallel_set_error(context, "truncated fileblock header length");
            break;
        }
        header_size = pbf_read_u32_be(length_bytes);
        if (header_size == 0U || header_size > PBF_MAX_BLOB_HEADER_SIZE) {
            pbf_parallel_set_error(context, "invalid BlobHeader size");
            break;
        }
        header_data = (unsigned char *)rt_malloc(header_size);
        if (header_data == 0 || pbf_read_exact(fd, header_data, header_size) != 0) {
            if (header_data != 0) rt_free(header_data);
            pbf_parallel_set_error(context, "could not read BlobHeader");
            break;
        }
        if (pbf_parse_blob_header(header_data, header_size, &header) != 0) {
            rt_free(header_data);
            pbf_parallel_set_error(context, "invalid BlobHeader");
            break;
        }
        rt_free(header_data);

        if (pbf_parallel_take_free_slot(context, &slot_index) != 0) {
            pbf_parallel_set_error(context, "could not reserve queue slot");
            break;
        }
        slot = &context->slots[slot_index];
        if (slot->capacity < (size_t)header.datasize) {
            unsigned char *new_data = (unsigned char *)rt_realloc(slot->blob_data, header.datasize);
            if (new_data == 0) {
                platform_semaphore_post(&context->free_slots);
                pbf_parallel_set_error(context, "out of memory");
                break;
            }
            slot->blob_data = new_data;
            slot->capacity = (size_t)header.datasize;
        }
        if (pbf_read_exact(fd, slot->blob_data, header.datasize) != 0) {
            platform_semaphore_post(&context->free_slots);
            pbf_parallel_set_error(context, "could not read Blob");
            break;
        }
        slot->header = header;
        slot->blob_size = (size_t)header.datasize;
        slot->stop = 0;
        pbf_parallel_post_ready_slot(context, slot_index);

        if (__atomic_load_n(&context->failed, __ATOMIC_ACQUIRE) || __atomic_load_n(&context->stopped, __ATOMIC_ACQUIRE)) {
            break;
        }
    }

    while (worker_count != 0U) {
        (void)pbf_parallel_enqueue_stop(context);
        worker_count -= 1U;
    }
    return __atomic_load_n(&context->failed, __ATOMIC_ACQUIRE) ? -1 : 0;
}

int pbf_read_summary(const char *path, PbfSummary *summary, char *error, size_t error_capacity) {
    int fd;
    unsigned char length_bytes[4];

    pbf_summary_init(summary);
    fd = platform_open_read(path);
    if (fd < 0) {
        pbf_set_error(error, error_capacity, "could not open input file");
        return -1;
    }

    for (;;) {
        long first = platform_read(fd, length_bytes, sizeof(length_bytes));
        unsigned int header_size;
        unsigned char *header_data;
        unsigned char *blob_data;
        PbfBlobHeader header;
        PbfDecodedBlob decoded;

        if (first == 0) {
            (void)platform_close(fd);
            return 0;
        }
        if (first != (long)sizeof(length_bytes) || pbf_read_exact(fd, length_bytes + (size_t)first, sizeof(length_bytes) - (size_t)first) != 0) {
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "truncated fileblock header length");
            return -1;
        }
        header_size = pbf_read_u32_be(length_bytes);
        if (header_size == 0U || header_size > PBF_MAX_BLOB_HEADER_SIZE) {
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "invalid BlobHeader size");
            return -1;
        }
        header_data = (unsigned char *)rt_malloc(header_size);
        if (header_data == 0 || pbf_read_exact(fd, header_data, header_size) != 0) {
            if (header_data != 0) rt_free(header_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not read BlobHeader");
            return -1;
        }
        if (pbf_parse_blob_header(header_data, header_size, &header) != 0) {
            rt_free(header_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "invalid BlobHeader");
            return -1;
        }
        rt_free(header_data);
        blob_data = (unsigned char *)rt_malloc(header.datasize);
        if (blob_data == 0 || pbf_read_exact(fd, blob_data, header.datasize) != 0) {
            if (blob_data != 0) rt_free(blob_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not read Blob");
            return -1;
        }
        summary->fileblocks += 1ULL;
        summary->compressed_bytes += (unsigned long long)header.datasize;
        if (pbf_decode_blob(blob_data, header.datasize, &decoded) != 0) {
            rt_free(blob_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not decode Blob");
            return -1;
        }
        if (pbf_parse_decoded_blob(&header, &decoded, summary) != 0) {
            if (decoded.should_free) rt_free(decoded.data);
            rt_free(blob_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not parse OSM protobuf payload");
            return -1;
        }
        if (decoded.should_free) rt_free(decoded.data);
        rt_free(blob_data);
    }
}

int pbf_read_summary_parallel(const char *path, unsigned int worker_count, PbfSummary *summary, char *error, size_t error_capacity) {
    PbfParallelContext context;
    PlatformThread *threads;
    int fd;
    unsigned int slot_count;
    unsigned int started = 0U;
    unsigned int index;
    int result = 0;

    if (worker_count <= 1U) {
        return pbf_read_summary(path, summary, error, error_capacity);
    }
    if (worker_count > PBF_PARALLEL_MAX_WORKERS) {
        worker_count = PBF_PARALLEL_MAX_WORKERS;
    }

    pbf_summary_init(summary);
    rt_memset(&context, 0, sizeof(context));
    pbf_summary_init(&context.summary);
    context.error = error;
    context.error_capacity = error_capacity;
    slot_count = worker_count * PBF_PARALLEL_QUEUE_MULTIPLIER;
    context.slot_count = slot_count;
    context.slots = (PbfParallelSlot *)rt_malloc(sizeof(PbfParallelSlot) * (size_t)slot_count);
    context.free_indices = (unsigned int *)rt_malloc(sizeof(unsigned int) * (size_t)slot_count);
    context.ready_indices = (unsigned int *)rt_malloc(sizeof(unsigned int) * (size_t)slot_count);
    threads = (PlatformThread *)rt_malloc(sizeof(PlatformThread) * (size_t)worker_count);
    if (context.slots == 0 || context.free_indices == 0 || context.ready_indices == 0 || threads == 0) {
        pbf_set_error(error, error_capacity, "out of memory");
        if (context.slots != 0) rt_free(context.slots);
        if (context.free_indices != 0) rt_free(context.free_indices);
        if (context.ready_indices != 0) rt_free(context.ready_indices);
        if (threads != 0) rt_free(threads);
        return -1;
    }
    rt_memset(context.slots, 0, sizeof(PbfParallelSlot) * (size_t)slot_count);
    for (index = 0U; index < slot_count; ++index) {
        context.free_indices[index] = index;
    }
    context.free_write_index = slot_count;
    platform_mutex_init(&context.free_mutex);
    platform_mutex_init(&context.ready_mutex);
    platform_mutex_init(&context.merge_mutex);
    platform_semaphore_init(&context.free_slots, (int)slot_count);
    platform_semaphore_init(&context.ready_slots, 0);

    fd = platform_open_read(path);
    if (fd < 0) {
        pbf_set_error(error, error_capacity, "could not open input file");
        rt_free(context.slots);
        rt_free(threads);
        return -1;
    }

    for (index = 0U; index < worker_count; ++index) {
        if (platform_thread_start(&threads[index], pbf_parallel_worker_main, &context, 0U) != 0) {
            pbf_parallel_set_error(&context, "could not start worker thread");
            break;
        }
        started += 1U;
    }

    if (started == worker_count) {
        result = pbf_parallel_read_blocks(fd, &context, started);
    } else {
        for (index = 0U; index < started; ++index) {
            (void)pbf_parallel_enqueue_stop(&context);
        }
        result = -1;
    }
    (void)platform_close(fd);

    for (index = 0U; index < started; ++index) {
        int thread_result = 0;
        if (platform_thread_join(&threads[index], &thread_result) != 0 || thread_result != 0) {
            result = -1;
            pbf_set_error(error, error_capacity, "worker thread failed");
        }
    }
    if (result == 0 && !context.failed) {
        *summary = context.summary;
    } else {
        result = -1;
    }
    for (index = 0U; index < slot_count; ++index) {
        if (context.slots[index].blob_data != 0) {
            rt_free(context.slots[index].blob_data);
        }
    }
    rt_free(context.slots);
    rt_free(context.free_indices);
    rt_free(context.ready_indices);
    rt_free(threads);
    return result;
}

static int pbf_stream_entities_parallel_serial(const char *path, const PbfStreamParallelOptions *options, char *error, size_t error_capacity) {
    void *worker_user;
    int result;

    if (options == 0 || options->callbacks == 0 || options->worker_user_size == 0U || options->merge_worker == 0) {
        pbf_set_error(error, error_capacity, "invalid parallel stream options");
        return -1;
    }
    worker_user = rt_malloc(options->worker_user_size);
    if (worker_user == 0) {
        pbf_set_error(error, error_capacity, "out of memory");
        return -1;
    }
    rt_memset(worker_user, 0, options->worker_user_size);
    if (options->init_worker != 0 && options->init_worker(worker_user, 0U, options->shared_user) != 0) {
        rt_free(worker_user);
        pbf_set_error(error, error_capacity, "could not initialize stream worker");
        return -1;
    }
    result = pbf_stream_entities(path, options->callbacks, worker_user, error, error_capacity);
    if (result == 0 && options->merge_worker(options->shared_user, worker_user) != 0) {
        pbf_set_error(error, error_capacity, "could not merge stream worker results");
        result = -1;
    }
    if (options->destroy_worker != 0) {
        options->destroy_worker(worker_user);
    }
    rt_free(worker_user);
    return result;
}

int pbf_stream_entities_parallel(const char *path, unsigned int worker_count, const PbfStreamParallelOptions *options, char *error, size_t error_capacity) {
    PbfParallelContext context;
    PlatformThread *threads;
    PbfParallelWorkerStart *starts;
    int fd;
    unsigned int slot_count;
    unsigned int started = 0U;
    unsigned int initialized = 0U;
    unsigned int index;
    int result = 0;

    if (worker_count <= 1U) {
        return pbf_stream_entities_parallel_serial(path, options, error, error_capacity);
    }
    if (options == 0 || options->callbacks == 0 || options->worker_user_size == 0U || options->merge_worker == 0) {
        pbf_set_error(error, error_capacity, "invalid parallel stream options");
        return -1;
    }
    if (worker_count > PBF_PARALLEL_MAX_WORKERS) {
        worker_count = PBF_PARALLEL_MAX_WORKERS;
    }

    rt_memset(&context, 0, sizeof(context));
    context.error = error;
    context.error_capacity = error_capacity;
    context.stream_options = options;
    slot_count = worker_count * PBF_PARALLEL_QUEUE_MULTIPLIER;
    context.slot_count = slot_count;
    context.slots = (PbfParallelSlot *)rt_malloc(sizeof(PbfParallelSlot) * (size_t)slot_count);
    context.free_indices = (unsigned int *)rt_malloc(sizeof(unsigned int) * (size_t)slot_count);
    context.ready_indices = (unsigned int *)rt_malloc(sizeof(unsigned int) * (size_t)slot_count);
    threads = (PlatformThread *)rt_malloc(sizeof(PlatformThread) * (size_t)worker_count);
    starts = (PbfParallelWorkerStart *)rt_malloc(sizeof(PbfParallelWorkerStart) * (size_t)worker_count);
    context.stream_worker_users = (unsigned char *)rt_malloc(options->worker_user_size * (size_t)worker_count);
    if (context.slots == 0 || context.free_indices == 0 || context.ready_indices == 0 || threads == 0 || starts == 0 || context.stream_worker_users == 0) {
        pbf_set_error(error, error_capacity, "out of memory");
        if (context.slots != 0) rt_free(context.slots);
        if (context.free_indices != 0) rt_free(context.free_indices);
        if (context.ready_indices != 0) rt_free(context.ready_indices);
        if (threads != 0) rt_free(threads);
        if (starts != 0) rt_free(starts);
        if (context.stream_worker_users != 0) rt_free(context.stream_worker_users);
        return -1;
    }
    rt_memset(context.slots, 0, sizeof(PbfParallelSlot) * (size_t)slot_count);
    rt_memset(context.stream_worker_users, 0, options->worker_user_size * (size_t)worker_count);
    for (index = 0U; index < slot_count; ++index) {
        context.free_indices[index] = index;
    }
    context.free_write_index = slot_count;
    platform_mutex_init(&context.free_mutex);
    platform_mutex_init(&context.ready_mutex);
    platform_mutex_init(&context.merge_mutex);
    platform_semaphore_init(&context.free_slots, (int)slot_count);
    platform_semaphore_init(&context.ready_slots, 0);

    for (index = 0U; index < worker_count; ++index) {
        void *worker_user = context.stream_worker_users + (size_t)index * options->worker_user_size;
        if (options->init_worker != 0 && options->init_worker(worker_user, index, options->shared_user) != 0) {
            pbf_set_error(error, error_capacity, "could not initialize stream worker");
            result = -1;
            break;
        }
        initialized += 1U;
    }

    fd = result == 0 ? platform_open_read(path) : -1;
    if (result == 0 && fd < 0) {
        pbf_set_error(error, error_capacity, "could not open input file");
        result = -1;
    }

    if (result == 0) {
        for (index = 0U; index < worker_count; ++index) {
            starts[index].context = &context;
            starts[index].worker_index = index;
            if (platform_thread_start(&threads[index], pbf_parallel_stream_worker_main, &starts[index], 0U) != 0) {
                pbf_parallel_set_error(&context, "could not start worker thread");
                break;
            }
            started += 1U;
        }

        if (started == worker_count) {
            result = pbf_parallel_read_blocks(fd, &context, started);
        } else {
            for (index = 0U; index < started; ++index) {
                (void)pbf_parallel_enqueue_stop(&context);
            }
            result = -1;
        }
        (void)platform_close(fd);
    }

    for (index = 0U; index < started; ++index) {
        int thread_result = 0;
        if (platform_thread_join(&threads[index], &thread_result) != 0 || thread_result != 0) {
            result = -1;
            pbf_set_error(error, error_capacity, "worker thread failed");
        }
    }
    if (result == 0 && !context.failed) {
        for (index = 0U; index < initialized; ++index) {
            void *worker_user = context.stream_worker_users + (size_t)index * options->worker_user_size;
            if (options->merge_worker(options->shared_user, worker_user) != 0) {
                pbf_set_error(error, error_capacity, "could not merge stream worker results");
                result = -1;
                break;
            }
        }
    } else {
        result = -1;
    }
    if (options->destroy_worker != 0) {
        for (index = 0U; index < initialized; ++index) {
            void *worker_user = context.stream_worker_users + (size_t)index * options->worker_user_size;
            options->destroy_worker(worker_user);
        }
    }
    for (index = 0U; index < slot_count; ++index) {
        if (context.slots[index].blob_data != 0) {
            rt_free(context.slots[index].blob_data);
        }
    }
    rt_free(context.slots);
    rt_free(context.free_indices);
    rt_free(context.ready_indices);
    rt_free(threads);
    rt_free(starts);
    rt_free(context.stream_worker_users);
    return result;
}

int pbf_stream_entities(const char *path, const PbfStreamCallbacks *callbacks, void *user, char *error, size_t error_capacity) {
    int fd;
    unsigned char length_bytes[4];
    int stopped = 0;

    fd = platform_open_read(path);
    if (fd < 0) {
        pbf_set_error(error, error_capacity, "could not open input file");
        return -1;
    }

    while (!stopped) {
        long first = platform_read(fd, length_bytes, sizeof(length_bytes));
        unsigned int header_size;
        unsigned char *header_data;
        unsigned char *blob_data;
        PbfBlobHeader header;
        PbfDecodedBlob decoded;
        int result;

        if (first == 0) {
            (void)platform_close(fd);
            return 0;
        }
        if (first != (long)sizeof(length_bytes) || pbf_read_exact(fd, length_bytes + (size_t)first, sizeof(length_bytes) - (size_t)first) != 0) {
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "truncated fileblock header length");
            return -1;
        }
        header_size = pbf_read_u32_be(length_bytes);
        if (header_size == 0U || header_size > PBF_MAX_BLOB_HEADER_SIZE) {
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "invalid BlobHeader size");
            return -1;
        }
        header_data = (unsigned char *)rt_malloc(header_size);
        if (header_data == 0 || pbf_read_exact(fd, header_data, header_size) != 0) {
            if (header_data != 0) rt_free(header_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not read BlobHeader");
            return -1;
        }
        if (pbf_parse_blob_header(header_data, header_size, &header) != 0) {
            rt_free(header_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "invalid BlobHeader");
            return -1;
        }
        rt_free(header_data);
        blob_data = (unsigned char *)rt_malloc(header.datasize);
        if (blob_data == 0 || pbf_read_exact(fd, blob_data, header.datasize) != 0) {
            if (blob_data != 0) rt_free(blob_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not read Blob");
            return -1;
        }
        if (pbf_decode_blob(blob_data, header.datasize, &decoded) != 0) {
            rt_free(blob_data);
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not decode Blob");
            return -1;
        }
        result = 0;
        if (pbf_text_equals(header.type, "OSMData")) {
            result = pbf_stream_primitive_block(decoded.data, decoded.size, callbacks, user);
        }
        if (decoded.should_free) rt_free(decoded.data);
        rt_free(blob_data);
        if (result < 0) {
            (void)platform_close(fd);
            pbf_set_error(error, error_capacity, "could not parse OSM protobuf payload");
            return -1;
        }
        if (result > 0) {
            stopped = 1;
        }
    }

    (void)platform_close(fd);
    return 0;
}