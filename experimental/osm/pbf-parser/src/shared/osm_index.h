#ifndef NEWOS_OSM_INDEX_H
#define NEWOS_OSM_INDEX_H

#include <stddef.h>

#define OSM_INDEX_ERROR_CAPACITY 160U
#define OSM_INDEX_BUILD_PROGRESS (1U << 0)

typedef struct {
    long long id;
    long long lat_nano;
    long long lon_nano;
} OsmNodeIndexRecord;

typedef struct {
    int fd;
    unsigned long long count;
} OsmNodeIndex;

typedef struct {
    long long id;
    unsigned long long ref_offset;
    unsigned int ref_count;
} OsmWayIndexRecord;

typedef struct {
    int fd;
    unsigned long long count;
    unsigned long long ref_count;
} OsmWayIndex;

typedef struct {
    long long id;
    unsigned long long member_offset;
    unsigned int member_count;
    unsigned int admin_level;
    unsigned long long name_offset;
    unsigned int name_size;
    unsigned int score;
} OsmRelationIndexRecord;

typedef struct {
    int fd;
    unsigned long long count;
    unsigned long long member_count;
    unsigned long long name_size;
    OsmRelationIndexRecord *records;
    long long *members;
    char *names;
} OsmRelationIndex;

typedef struct {
    long long id;
    long long min_lon_nano;
    long long min_lat_nano;
    long long max_lon_nano;
    long long max_lat_nano;
} OsmSpatialIndexRecord;

typedef struct {
    int fd;
    unsigned long long count;
    OsmSpatialIndexRecord *records;
} OsmSpatialIndex;

int osm_node_index_build(const char *pbf_path, const char *index_path, unsigned long long *count_out, char *error, size_t error_capacity);
int osm_node_index_build_ex(const char *pbf_path, const char *index_path, unsigned long long *count_out, unsigned int flags, char *error, size_t error_capacity);
int osm_node_index_open(OsmNodeIndex *index, const char *path, char *error, size_t error_capacity);
void osm_node_index_close(OsmNodeIndex *index);
int osm_node_index_find(OsmNodeIndex *index, long long id, OsmNodeIndexRecord *record_out, char *error, size_t error_capacity);
int osm_node_index_load_records(OsmNodeIndex *index, OsmNodeIndexRecord **records_out, unsigned long long *count_out, char *error, size_t error_capacity);

int osm_way_index_build(const char *pbf_path, const char *index_path, unsigned long long *way_count_out, unsigned long long *ref_count_out, char *error, size_t error_capacity);
int osm_way_index_build_ex(const char *pbf_path, const char *index_path, unsigned long long *way_count_out, unsigned long long *ref_count_out, unsigned int flags, char *error, size_t error_capacity);
int osm_index_build_ex(const char *pbf_path, const char *node_index_path, const char *way_index_path, unsigned long long *node_count_out, unsigned long long *way_count_out, unsigned long long *ref_count_out, unsigned int flags, char *error, size_t error_capacity);
int osm_way_index_open(OsmWayIndex *index, const char *path, char *error, size_t error_capacity);
void osm_way_index_close(OsmWayIndex *index);
int osm_way_index_find(OsmWayIndex *index, long long id, OsmWayIndexRecord *record_out, char *error, size_t error_capacity);
int osm_way_index_read_refs(OsmWayIndex *index, const OsmWayIndexRecord *record, long long **refs_out, char *error, size_t error_capacity);

int osm_relation_index_build(const char *pbf_path, const char *index_path, unsigned long long *relation_count_out, unsigned long long *member_count_out, unsigned int flags, char *error, size_t error_capacity);
int osm_relation_index_open(OsmRelationIndex *index, const char *path, char *error, size_t error_capacity);
void osm_relation_index_close(OsmRelationIndex *index);
int osm_relation_index_find_city(OsmRelationIndex *index, const char *name, OsmRelationIndexRecord *record_out);

int osm_spatial_index_build(const char *node_index_path, const char *way_index_path, const char *spatial_index_path, unsigned long long *way_count_out, unsigned int flags, char *error, size_t error_capacity);
int osm_spatial_index_open(OsmSpatialIndex *index, const char *path, char *error, size_t error_capacity);
void osm_spatial_index_close(OsmSpatialIndex *index);
int osm_spatial_index_way_intersects(OsmSpatialIndex *index, long long id, long long min_lon_nano, long long min_lat_nano, long long max_lon_nano, long long max_lat_nano);

#endif