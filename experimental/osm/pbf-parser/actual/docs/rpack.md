# RPACK Render Pack Format

Status: future stable-format design. This is not the `.rpack` format currently written by the repository tools. The active implemented render-pack format is `OSMRPK02`, documented in `../../docs/osmrpack_format.md`, built by `../../src/tools/pbf_to_rpack.c`, and rendered by `../../src/tools/rpack_render.c`.

`RPACK` is a binary, seekable, tile-oriented map render format built from OpenStreetMap PBF source data. It is optimized for fast repeated rendering in native tools, servers, and browser runtimes where a renderer should open a small set of directories and then read only the map payloads required for the requested viewport.

The file is a render cache, not an interchange format. The builder may spend significant time parsing PBF data, resolving relation boundaries, classifying features, simplifying geometry, assigning features to tiles, and compressing payload chunks. A renderer should not parse source PBF data and should not need to decompress the entire pack before drawing a city, bbox, or route preview.

## Design Goals

- render local and regional map views without scanning source `.osm.pbf` files
- keep lookup directories and viewport-selection metadata directly readable
- store render geometry in independently compressed tile payloads
- support place-name lookup for city or region views
- store place boundary geometry for outlines, clipping, and outside-boundary fade
- support route overlays and optional transit-stop overlays without changing base geometry
- make browser delivery practical through full-file loading, range requests, or application-managed chunk fetching
- keep the reader small enough for freestanding native builds and WebAssembly

## Non-Goals

- preserving the full OSM object model
- preserving all original OSM tags
- preserving source entity order
- dynamic map editing
- arbitrary runtime styling over arbitrary source tags
- requiring whole-file decompression

## File Layout

```text
RPACK file
  header                         fixed 256 bytes
  section directory              global typed section table
  chunk directory                compressed/raw payload chunk table
  tile directory                 one record per non-empty render tile
  place directory                one record per named place or render extent
  feature dictionary             layer and feature-class metadata
  string table                   UTF-8 strings used by records
  tile payload chunks            independently compressed feature streams
  boundary payload chunks        independently compressed place boundaries
  optional overlay chunks        labels, stops, route overlays, debug metadata
  optional metadata/checksums
```

The header, section directory, chunk directory, tile directory, place directory, and small lookup indexes are uncompressed. Large geometry payloads are stored as independently addressable chunks. A renderer must be able to determine which chunks are required before decompressing any geometry.

## Encoding Rules

- All integer fields are little-endian.
- Offsets are absolute byte offsets from the start of the file.
- Counts are unsigned.
- Coordinates are signed WGS84 nanodegrees unless a section explicitly says otherwise.
- Strings are UTF-8 byte spans and are not required to have NUL terminators.
- Records contain zero-filled reserved fields.
- Directories are sorted by their primary lookup key.
- Unknown optional section and chunk types are ignored.
- Unknown required section types make the file unsupported.

Coordinate scale:

```text
lat_nano = round(latitude_degrees  * 1000000000)
lon_nano = round(longitude_degrees * 1000000000)
```

## Header

Header size: 256 bytes.

```text
offset size field
0      8    magic = "RPACK001"
8      4    format_version = 1
12     4    header_size = 256
16     4    endian_marker = 0x01020304
20     4    flags
24     8    file_size
32     8    build_unix_time
40     8    source_osm_nodes
48     8    source_osm_ways
56     8    source_osm_relations
64     8    section_directory_offset
72     4    section_count
76     4    section_record_size = 64
80     8    chunk_directory_offset
88     8    chunk_directory_size
96     4    chunk_record_size = 64
100    4    chunk_count
104    8    tile_directory_offset
112    8    tile_directory_size
120    4    tile_record_size = 112
124    4    tile_count
128    8    place_directory_offset
136    8    place_directory_size
144    4    place_record_size = 128
148    4    place_count
152    4    tile_scheme
156    4    tile_zoom
160    4    tile_halo
164    4    layer_count
168    4    coord_scale = 1000000000
172    4    reserved = 0
176    8    string_table_offset
184    8    string_table_size
192    8    metadata_offset
200    8    metadata_size
208    4    checksum_kind
212    4    header_checksum
216    16   build_profile_fingerprint
232    24   source_fingerprint
256         end of header
```

Header flags:

```text
bit 0 one or more payload chunks use XZ/LZMA2 compression
bit 1 one or more payload chunks are stored raw as an adaptive fallback
bit 2 chunk checksums present
bit 3 place directory present
bit 4 feature dictionary present
```

`tile_scheme` values:

```text
0 unknown
1 geographic grid
2 Web Mercator
```

The recommended default is `tile_scheme = 1`, a simple geographic grid over longitude and latitude. This keeps tile math small and deterministic for render use.

Compressed payload chunks use XZ streams with LZMA2 preset 6. Readers implement XZ decoding plus the raw fallback indicated by chunk flags. The header does not carry a codec selector.

The canonical builder default is XZ/LZMA2 preset 6 for release packs. Preset 6 was selected for its compression ratio on representative render-pack and route-pack data while keeping decompression practical for independently loaded chunks. Higher presets may be used for experiments, but they are a builder policy and do not change the file format.

## Section Directory

Record size: 64 bytes.

```text
offset size field
0      4    type
4      4    flags
8      8    offset
16     8    size
24     8    uncompressed_size
32     8    record_count
40     4    record_size
44     4    checksum
48     16   reserved = 0
```

Section flags:

```text
bit 0 required
bit 1 compressed
bit 2 hot open path
bit 3 directory/index
bit 4 payload table
```

Core section types:

```text
0x0001 chunk directory
0x0002 tile directory
0x0003 place directory
0x0004 feature dictionary
0x0005 string table
0x0006 metadata
0x0100 tile lookup index, optional
0x0200 label index, optional
0x0300 transit stop overlay index, optional
0x0700 debug/source map, optional
```

The section directory describes uncompressed directories and logical indexes. Large geometry payloads are normally described by chunk records rather than by one section per tile.

## Chunk Directory

Record size: 64 bytes.

```text
offset size field
0      4    type
4      4    flags
8      8    chunk_id
16     8    offset
24     8    compressed_size
32     8    uncompressed_size
40     4    reserved = 0
44     4    checksum
48     4    record_count
52     4    record_size
56     8    reserved = 0
```

Chunk types:

```text
0x1000 render tile payload
0x1001 place boundary payload
0x1002 label payload
0x1003 transit stop overlay payload
0x1004 route overlay cache payload
0x7000 debug/source payload
```

Chunk flags:

```text
bit 0 required for base rendering
bit 1 hot after viewport selection
bit 2 contains geometry
bit 3 contains strings
bit 4 can be skipped for simplified rendering
bit 5 stored raw because standard compression was not useful
```

The chunk table is uncompressed and sorted by `(type, chunk_id)`. Tile and place records point to chunk IDs, not directly to payload offsets, so a writer may reorder chunks without changing high-level directory records.

If chunk flag bit 5 is set, `compressed_size` must equal `uncompressed_size` and the payload bytes are stored raw. Otherwise payload bytes are encoded as a single XZ stream.

## Tile Coordinate System

For `tile_scheme = 1`, tiles use a geographic grid:

```text
axis = 1 << tile_zoom
x = floor((lon_nano + 180000000000) * axis / 360000000000)
y = floor(( 90000000000 - lat_nano) * axis / 180000000000)
```

Inputs are clamped to the valid world extent before conversion. Resulting `x` and `y` values are clamped to `0..axis-1`.

Tile extents are derived with integer division:

```text
min_lon = floor(x       * 360000000000 / axis) - 180000000000
max_lon = floor((x + 1) * 360000000000 / axis) - 180000000000
max_lat =  90000000000 - floor(y       * 180000000000 / axis)
min_lat =  90000000000 - floor((y + 1) * 180000000000 / axis)
```

Tile IDs are sortable unsigned 64-bit values:

```text
tile_id = (tile_zoom << 58) | (x << 29) | y
```

## Tile Directory

Record size: 112 bytes.

```text
offset size field
0      8    tile_id
8      4    z
12     4    x
16     4    y
20     4    feature_count
24     4    layer_mask
28     4    flags
32     4    reserved = 0
36     8    payload_chunk_id
44     8    payload_uncompressed_size
52     8    min_lon_nano
60     8    min_lat_nano
68     8    max_lon_nano
76     8    max_lat_nano
84     8    neighbor_mask
92     4    label_ref_count
96     4    overlay_ref_count
100    12   reserved = 0
```

`layer_mask` has bit `style_id` set for every style present in the tile payload. It lets the renderer skip tiles that cannot affect a selected layer set.

`neighbor_mask` marks the eight neighboring tile positions that exist in the pack. It is a hint for prefetching and edge effects.

Records are sorted by `tile_id`. Readers must support binary search and may support optional lookup sections.

## Place Directory

Record size: 128 bytes.

```text
offset size field
0      8    source_id
8      4    source_type
12     4    place_kind
16     4    admin_level
20     4    rank_score
24     8    min_lon_nano
32     8    min_lat_nano
40     8    max_lon_nano
48     8    max_lat_nano
56     8    primary_min_lon_nano
64     8    primary_min_lat_nano
72     8    primary_max_lon_nano
80     8    primary_max_lat_nano
88     8    name_offset
96     4    name_size
100    4    flags
104    8    boundary_chunk_id
112    8    boundary_uncompressed_size
120    4    boundary_feature_count
124    4    reserved = 0
```

`source_id` is informational and may be zero when the builder creates a synthetic place or render extent.

`place_kind` values:

```text
0 unknown
1 city
2 town
3 village
4 suburb
5 district
6 state
7 country
8 named area
```

The `primary_*` bbox lets renderers avoid zooming out to far-away detached components by default. A UI can still use the full bbox when it wants every component.

For exact name lookup, readers compare UTF-8 byte strings in the string table. If several records match the same name, the highest `rank_score` wins unless the caller applies a more specific filter.

## Feature Dictionary

The feature dictionary defines stable render classes used by tile payloads. It stores semantic classes, not final presentation colors.

Feature class record size: 32 bytes.

```text
offset size field
0      4    style_id
4      4    flags
8      4    name_offset
12     4    name_size
16     4    default_draw_order
20     4    geometry_kind_mask
24     8    reserved = 0
```

Recommended base style IDs:

```text
0  water
1  waterway
2  forest
3  park
4  building
5  motorway
6  primary road
7  secondary road
8  minor road
9  path
10 rail
11 boundary
12 transit stop overlay
13 route overlay
```

## Tile Payload

Each tile payload is stored in a chunk and decompresses to a feature stream.

```text
tile payload header        64 bytes
feature records            feature_count * 56 bytes
coordinate data            variable
optional local string refs variable
```

Tile payload header:

```text
offset size field
0      8    tile_id
8      4    payload_format_version = 1
12     4    flags
16     4    feature_count
20     4    feature_record_size = 56
24     8    feature_records_offset
32     8    coordinate_data_offset
40     8    coordinate_data_size
48     4    local_string_ref_count
52     4    local_string_ref_record_size
56     8    reserved = 0
```

Feature record size: 56 bytes.

```text
offset size field
0      4    style_id
4      4    flags
8      4    point_count
12     4    coordinate_encoding
16     8    coordinate_offset
24     8    min_lon_nano
32     8    min_lat_nano
40     8    max_lon_nano
48     8    max_lat_nano
```

Feature flags:

```text
bit 0 closed area
bit 1 coastline
bit 2 label anchor available
bit 3 simplified geometry
bit 4 boundary geometry
```

Coordinate encodings:

```text
0 raw i64 lon_nano, i64 lat_nano pairs
1 tile-local delta varints, optional
2 fixed-point tile-local int32 pairs, optional
```

Raw coordinate pairs are required for the first reader implementation. More compact encodings may be added when the size reduction justifies the reader complexity.

## Boundary Payload

A boundary payload is a chunk containing a feature stream for one place. The payload format is the same as a tile payload, except the payload header identifies a place record instead of a tile ID.

Boundary chunks are compressed independently from tile chunks. Rendering a city by name should require only the place directory, the selected boundary chunk, selected tile chunks, and string table spans used for names or labels.

## Compression Strategy

Compression is applied at chunk granularity. Writers should choose raw storage for chunks where compression does not reduce size enough to justify runtime cost.

Recommended builder policy:

```text
if uncompressed_size < 1024 bytes: store raw
else compress with XZ/LZMA2 preset 6
if compressed_size + chunk_overhead >= uncompressed_size * 0.95: store raw
```

Readers should maintain a decoded-chunk cache keyed by `chunk_id`. Browser readers may combine this with HTTP range requests when the full file is not already available. Readers are not required to carry alternate codec implementations.

## Rendering Algorithm

```text
open file
validate header and required directories
resolve bbox directly, or resolve place name to bbox and boundary chunk
convert bbox to tile range
expand by tile_halo
select tile records that intersect the render range and layer mask
load and decompress selected tile chunks
collect visible features intersecting the render bbox
load selected boundary chunk when requested
draw layers in style order
draw optional overlays
write image
```

The renderer should avoid decoding chunks outside the selected tile range. It should also avoid decoding optional overlay chunks unless the caller enabled the corresponding overlay.

## Builder Responsibilities

The builder should:

- stream the source PBF in deterministic phases
- classify renderable ways and relation-derived boundary members
- resolve node coordinates required by renderable geometry
- assign features to intersecting render tiles
- resolve named places and choose deterministic place records
- write boundary chunks for place outlines
- sort directories by stable keys
- compress chunks independently
- record source counts and build metadata

Builds should be reproducible. Threaded builders must merge worker-local data through stable sorts rather than depending on worker scheduling.

## Runtime Responsibilities

The renderer should:

- validate headers, directory sizes, record sizes, and required sections
- treat payload chunks as immutable
- decompress only selected chunks
- bound memory use with a decoded-chunk cache
- ignore unknown optional sections
- reject unknown required sections
- reject chunks whose decompressed size does not match the chunk directory
- avoid requiring source PBF side files for normal rendering

## Minimum Useful RPACK

A minimum useful file contains:

- header
- section directory
- chunk directory
- tile directory
- place directory
- feature dictionary
- string table
- compressed or raw tile payload chunks
- compressed or raw boundary chunks for named places

This is enough to render city and bbox maps without reading source PBF data.
