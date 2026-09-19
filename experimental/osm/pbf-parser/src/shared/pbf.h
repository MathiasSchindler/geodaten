#ifndef NEWOS_PBF_H
#define NEWOS_PBF_H

#include <stddef.h>

#define PBF_MAX_FEATURES 32U
#define PBF_FEATURE_TEXT_CAPACITY 96U
#define PBF_TEXT_CAPACITY 256U
#define PBF_ERROR_CAPACITY 160U

typedef struct {
    char text[PBF_FEATURE_TEXT_CAPACITY];
} PbfFeature;

typedef struct {
    unsigned long long fileblocks;
    unsigned long long header_blocks;
    unsigned long long data_blocks;
    unsigned long long other_blocks;
    unsigned long long raw_blobs;
    unsigned long long zlib_blobs;
    unsigned long long compressed_bytes;
    unsigned long long uncompressed_bytes;
    unsigned long long primitive_blocks;
    unsigned long long primitive_groups;
    unsigned long long dense_node_groups;
    unsigned long long nodes;
    unsigned long long ways;
    unsigned long long relations;
    PbfFeature required_features[PBF_MAX_FEATURES];
    unsigned int required_feature_count;
    PbfFeature optional_features[PBF_MAX_FEATURES];
    unsigned int optional_feature_count;
    char writing_program[PBF_TEXT_CAPACITY];
    char source[PBF_TEXT_CAPACITY];
} PbfSummary;

typedef struct {
    const char *data;
    size_t size;
} PbfText;

typedef struct {
    PbfText key;
    PbfText value;
} PbfTag;

typedef struct {
    long long id;
    long long lat_nano;
    long long lon_nano;
    const PbfTag *tags;
    unsigned int tag_count;
} PbfNode;

typedef struct {
    long long id;
    const long long *refs;
    unsigned int ref_count;
    const PbfTag *tags;
    unsigned int tag_count;
} PbfWay;

#define PBF_RELATION_MEMBER_NODE 0U
#define PBF_RELATION_MEMBER_WAY 1U
#define PBF_RELATION_MEMBER_RELATION 2U

typedef struct {
    long long id;
    unsigned int type;
    PbfText role;
} PbfRelationMember;

typedef struct {
    long long id;
    const PbfRelationMember *members;
    unsigned int member_count;
    const PbfTag *tags;
    unsigned int tag_count;
} PbfRelation;

typedef struct {
    unsigned int flags;
    int (*node)(void *user, const PbfNode *node);
    int (*way_id)(void *user, long long id);
    int (*way_tags)(void *user, long long id, const PbfTag *tags, unsigned int tag_count);
    int (*way)(void *user, const PbfWay *way);
    int (*relation_tags)(void *user, long long id, const PbfTag *tags, unsigned int tag_count);
    int (*relation)(void *user, const PbfRelation *relation);
} PbfStreamCallbacks;

typedef struct {
    const PbfStreamCallbacks *callbacks;
    size_t worker_user_size;
    int (*init_worker)(void *worker_user, unsigned int worker_index, void *shared_user);
    int (*merge_worker)(void *shared_user, void *worker_user);
    void (*destroy_worker)(void *worker_user);
    void *shared_user;
} PbfStreamParallelOptions;

#define PBF_STREAM_SKIP_NODE_TAGS (1U << 0)
#define PBF_STREAM_SKIP_WAY_TAGS  (1U << 1)
#define PBF_STREAM_SKIP_RELATION_ROLES (1U << 2)

void pbf_summary_init(PbfSummary *summary);
int pbf_read_summary(const char *path, PbfSummary *summary, char *error, size_t error_capacity);
int pbf_read_summary_parallel(const char *path, unsigned int worker_count, PbfSummary *summary, char *error, size_t error_capacity);
int pbf_stream_entities(const char *path, const PbfStreamCallbacks *callbacks, void *user, char *error, size_t error_capacity);
int pbf_stream_entities_parallel(const char *path, unsigned int worker_count, const PbfStreamParallelOptions *options, char *error, size_t error_capacity);

#endif