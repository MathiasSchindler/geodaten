# OSM Entity Streaming and Lookup

The PBF parser now exposes decoded OpenStreetMap entities as a stream. This is the base layer for lookup, extraction, indexing, and later rendering tools.

## Stream API

The public API is declared in `src/shared/pbf.h`:

```c
typedef struct {
    unsigned int flags;
    int (*node)(void *user, const PbfNode *node);
    int (*way_tags)(void *user, long long id, const PbfTag *tags, unsigned int tag_count);
    int (*way)(void *user, const PbfWay *way);
    int (*relation_tags)(void *user, long long id, const PbfTag *tags, unsigned int tag_count);
    int (*relation)(void *user, const PbfRelation *relation);
} PbfStreamCallbacks;

#define PBF_STREAM_SKIP_NODE_TAGS (1U << 0)

int pbf_stream_entities(const char *path,
                        const PbfStreamCallbacks *callbacks,
                        void *user,
                        char *error,
                        size_t error_capacity);

typedef struct {
    const PbfStreamCallbacks *callbacks;
    size_t worker_user_size;
    int (*init_worker)(void *worker_user, unsigned int worker_index, void *shared_user);
    int (*merge_worker)(void *shared_user, void *worker_user);
    void (*destroy_worker)(void *worker_user);
    void *shared_user;
} PbfStreamParallelOptions;

int pbf_stream_entities_parallel(const char *path,
                                 unsigned int worker_count,
                                 const PbfStreamParallelOptions *options,
                                 char *error,
                                 size_t error_capacity);
```

Each callback receives data that is valid only for the duration of the callback. Consumers that need to keep tags, refs, members, or text must copy them.

`PBF_STREAM_SKIP_NODE_TAGS` lets node-only consumers skip dense-node tag decoding. When this flag is set, dense-node streaming decodes IDs and coordinates directly from the packed protobuf fields instead of first materializing temporary arrays. `pbf-to-rpack` and `pbf-to-rte` use this in phases that only need node IDs and coordinates.

`way_tags` is an optional prefilter. It receives decoded way tags before packed node refs are decoded. Returning `0` rejects the way and skips ref decoding; returning non-zero continues to the normal `way` callback. Renderers use this to avoid ref work for ways that do not match any visible style rule.

`relation_tags` provides the same tag-only path for relations. Tag-only consumers can use it without a `relation` callback to avoid decoding relation members.

Decoded entities include:

- Nodes: ID, latitude, longitude, and tags.
- Ways: ID, decoded node refs, and tags.
- Relations: ID, decoded members, roles, member types, and tags.

Coordinates are exposed as signed nanodegrees. Convert with:

```text
degrees = nano / 1000000000
```

`pbf_stream_entities` runs serially. `pbf_stream_entities_parallel` uses the threaded fileblock pipeline and gives each worker a separate callback user buffer. Worker callbacks must write only to their worker-local state or to explicitly shared read-only structures. After all worker threads exit, `merge_worker` runs serially for each worker so consumers can append results into their global output without callback-time locks.

Parallel entity streaming does not preserve source entity order. It is intended for order-independent consumers such as render-pack construction phases that collect worker-local ways or fill a prebuilt node lookup. If `worker_count` is `1`, the parallel API still uses one worker-local buffer and then calls `merge_worker`; this keeps serial and threaded code paths consistent.

## osm-lookup

`osm-lookup` is the first stream consumer. It filters entities while scanning the PBF file and prints matching records.

Build:

```sh
make all
```

Usage:

```sh
./build/freestanding-linux-x86_64/osm-lookup FILE.osm.pbf [options]
```

Options:

- `--type node|way|relation|all`
- `--tag KEY`
- `--tag KEY=VALUE`
- `--name VALUE`
- `--id node:ID`, `--id way:ID`, or `--id relation:ID`
- `--bbox MINLON,MINLAT,MAXLON,MAXLAT`
- `--limit N`, where `0` means no limit

Multiple tag filters are combined with AND semantics.

Examples:

```sh
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type node --tag amenity --limit 3
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type way --tag highway --limit 3
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type relation --tag type=multipolygon --limit 3
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --name Alexanderplatz --limit 10
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --id node:16541597
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type node --bbox 13.30,52.50,13.40,52.60 --tag amenity --limit 3
```

Output is line-oriented text. Nodes include coordinates; ways include decoded ref counts; relations include member counts.

## osm-addresses

`osm-addresses` extracts address tags from nodes, ways, and relations and writes tab-separated output. By default it emits only complete records that contain all three tags:

```text
addr:street
addr:housenumber
addr:postcode
```

Build and run:

```sh
make all
./build/freestanding-linux-x86_64/osm-addresses data/germany-060524.osm.pbf build/germany-addresses.tsv
```

Output columns:

```text
type    id    lat    lon    state    city    suburb    street    housenumber    postcode
```

Node rows include coordinates. Way and relation rows leave the coordinate columns empty because this extractor only reads address tags.

Options:

- `--include-incomplete`, emit records that have at least one of state, city, suburb, street, housenumber, or postcode.
- `--no-header`, omit the TSV header row.
- `--limit N`, stop after writing `N` rows when the limit is reached from node or relation callbacks.

The tool uses buffered TSV output and the `way_tags` / `relation_tags` prefilters. Address ways are written without decoding packed node refs, and address relations are written without decoding relation members. This keeps address extraction cheaper than a full geometry lookup.

Smoke validation on `data/hamburg-260524.osm.pbf`:

```sh
./build/freestanding-linux-x86_64/osm-addresses data/hamburg-260524.osm.pbf build/hamburg-addresses-sample.tsv --limit 5
```

Small bounded validation result:

```text
addresses_written: 5
node_addresses: 5
way_addresses: 0
relation_addresses: 0
```

Full Hamburg extraction after adding buffered output and relation tag prefiltering:

```text
addresses_written: 262415
node_addresses: 52076
way_addresses: 210108
relation_addresses: 231
elapsed: 0:01.72
maxrss_kb: 7168
output: 14 MB
```

## Validation

Validated on `data/brandenburg-260524.osm.pbf`:

```sh
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type node --tag amenity --limit 1
```

produced a decoded node with coordinates and tags:

```text
node 16541597 lat=52.546314700 lon=13.345599000 amenity=fuel ...
```

Way and relation decoding was checked with:

```sh
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type way --tag highway --limit 3
./build/freestanding-linux-x86_64/osm-lookup data/brandenburg-260524.osm.pbf --type relation --tag type=multipolygon --limit 3
```

The `pbf-info` regression check still reports the known Brandenburg/Berlin totals:

```text
dense_node_groups: 3348
nodes: 26778793
ways: 4447642
relations: 59572
```

## Render Pack Path

Map rendering now goes through `OSMRPK02` render packs instead of rendering directly from `.osm.pbf` files:

```sh
./build/freestanding-linux-x86_64/pbf-to-rpack data/brandenburg-260524.osm.pbf build/brandenburg.rpack
./build/freestanding-linux-x86_64/rpack-render build/brandenburg.rpack build/potsdam.png --city Potsdam
```

`pbf-to-rpack` is the PBF stream consumer. It classifies renderable ways, collects the needed node coordinates, builds the place and tile directories, and writes the `.rpack`. `rpack-render` reads only the pack at render time, so map output no longer requires scanning the source PBF. Render colors and stroke widths come from `styles/osmrender-default.conf` by default, or from `--style FILE`.