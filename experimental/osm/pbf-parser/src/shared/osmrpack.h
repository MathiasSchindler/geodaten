#ifndef NEWOS_OSMRPACK_H
#define NEWOS_OSMRPACK_H

#include <stddef.h>

#include "pbf.h"

#define OSMRPACK_ERROR_CAPACITY 160U
#define OSMRPACK_HEADER_SIZE 160U
#define OSMRPACK_TILE_RECORD_SIZE 80U
#define OSMRPACK_FEATURE_HEADER_SIZE 48U
#define OSMRPACK_VERSION 1U
#define OSMRPACK_DEFAULT_TILE_ZOOM 10U
#define OSMRPACK_MAX_TILE_ZOOM 18U

#define OSMRPACK_FLAG_EMPTY_GEOMETRY (1U << 0)

#define OSMRPACK_FEATURE_FLAG_AREA (1U << 0)
#define OSMRPACK_FEATURE_FLAG_COASTLINE (1U << 1)

typedef struct {
    unsigned int version;
    unsigned int header_size;
    unsigned int tile_record_size;
    unsigned int tile_zoom;
    unsigned int flags;
    unsigned int layer_count;
    unsigned long long tile_count;
    unsigned long long tile_directory_offset;
    unsigned long long feature_data_offset;
    unsigned long long feature_data_size;
    unsigned long long string_table_offset;
    unsigned long long string_table_size;
    unsigned long long source_fileblocks;
    unsigned long long source_data_blocks;
    unsigned long long source_nodes;
    unsigned long long source_ways;
    unsigned long long source_relations;
    unsigned long long source_compressed_bytes;
    unsigned long long source_uncompressed_bytes;
} OsmrPackHeader;

typedef struct {
    unsigned long long tile_id;
    unsigned int z;
    unsigned int x;
    unsigned int y;
    unsigned int feature_count;
    unsigned int layer_mask;
    unsigned long long payload_offset;
    unsigned long long payload_size;
    long long min_lon_nano;
    long long min_lat_nano;
    long long max_lon_nano;
    long long max_lat_nano;
} OsmrPackTileRecord;

void osmrpack_header_init(OsmrPackHeader *header, unsigned int tile_zoom, const PbfSummary *summary);
int osmrpack_write_empty(const char *path, unsigned int tile_zoom, const PbfSummary *summary, char *error, size_t error_capacity);
int osmrpack_read_header(const char *path, OsmrPackHeader *header_out, char *error, size_t error_capacity);
int osmrpack_validate_header(const OsmrPackHeader *header, char *error, size_t error_capacity);
int osmrpack_write_header_fd(int fd, const OsmrPackHeader *header);
int osmrpack_write_tile_record_fd(int fd, const OsmrPackTileRecord *record);
int osmrpack_read_tile_record_fd(int fd, OsmrPackTileRecord *record);
void osmrpack_write_header_text(int fd, const OsmrPackHeader *header);

#endif
