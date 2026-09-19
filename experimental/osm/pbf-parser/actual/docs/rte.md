# RTE Route Pack Format

Status: future stable-format design. This is not the `.rte` format currently written by the repository tools. The active implemented route-pack format is `OSMRTE01`, documented in `../../docs/OSMRTE01.md`, built by `../../src/tools/pbf_to_rte.c`, inspected by `../../src/tools/rte_info.c`, and queried by `../../src/tools/rte_route.c`.

`RTE` is a binary, seekable, tile-oriented route format built from OpenStreetMap PBF source data and optional public-transport feeds. It is optimized for walking and public-transport route queries over large regions while avoiding source parsing at query time.

The file is an application-native routing cache. The builder may spend significant time parsing source data, selecting walkable geometry, simplifying graphs, resolving addresses, building snap indexes, grouping transit schedules, and compressing payload chunks. A query engine should open the file, read compact directories and indexes, then load only the graph and schedule chunks required for a route query.

## Design Goals

- route without scanning `.osm.pbf` files at query time
- resolve coordinates, addresses, places, POIs, and stops onto a reusable walking graph
- make local walking routes fast with independently loadable route tiles
- make public-transport routing fast with indexed route patterns, trips, calendars, and stop events
- keep required lookup indexes directly readable
- compress large tile and schedule payloads independently
- support browser runtimes that may fetch only selected chunks
- keep route reconstruction possible without source data

## Non-Goals

- preserving the full OSM or GTFS source model
- preserving all original OSM or GTFS fields
- arbitrary road-vehicle routing in the base profile
- complete fare modelling
- real-time updates in the base file
- requiring whole-file decompression

## File Layout

```text
RTE file
  header                         fixed 256 bytes
  section directory              global typed section table
  chunk directory                compressed/raw payload chunk table
  route tile directory           one record per walking graph tile
  global string table            names, stop IDs, route IDs, address text
  global address indexes         normalized lookup tables
  global stop index              stops and stop locality records
  transit route-pattern indexes  routes, patterns, stop-to-pattern refs
  transit calendar indexes       service-day masks
  tile payload chunks            walking graph, snap grid, local refs
  transit event chunks           independently loadable schedule pages
  optional overlay indexes       portals, landmarks, transfer caches
  optional metadata/checksums
```

Directories and indexes needed to decide what to load are uncompressed. Large tile graphs, geometry, address payloads, and event pages are stored as independently addressable chunks.

## Encoding Rules

- All integers are little-endian.
- Offsets are absolute byte offsets from the start of the file.
- Counts are unsigned.
- Coordinates use signed fixed-point WGS84 degrees scaled by `1e7` unless a section explicitly says otherwise.
- Distances are meters unless a field explicitly declares another unit.
- Time values are seconds since the local service-day start. Values may exceed `86400` for after-midnight trips.
- Strings are UTF-8 byte spans and are not required to have NUL terminators.
- Hot lookup arrays are aligned to 64 bytes.
- Records contain zero-filled reserved fields.
- Unknown optional section and chunk types are ignored.
- Unknown required section types make the file unsupported.

Coordinate scale:

```text
lat_e7 = round(latitude_degrees  * 10000000)
lon_e7 = round(longitude_degrees * 10000000)
```

## Header

Header size: 256 bytes.

```text
offset size field
0      8    magic = "RTE00001"
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
120    4    tile_record_size = 128
124    4    tile_count
128    4    tile_scheme
132    4    tile_level_count
136    4    coord_scale = 10000000
140    4    routing_profile_mask
144    4    projection_kind
148    4    tile_size_m
152    4    projection_origin_lat_e7
156    4    projection_origin_lon_e7
160    4    tile_lookup_kind
164    4    reserved = 0
168    4    min_lon_e7
172    4    min_lat_e7
176    4    max_lon_e7
180    4    max_lat_e7
184    8    string_table_offset
192    8    string_table_size
200    8    metadata_offset
208    8    metadata_size
216    4    checksum_kind
220    4    header_checksum
224    16   routing_profile_fingerprint
240    16   source_fingerprint
256         end of header
```

Header flags:

```text
bit 0 one or more payload chunks use XZ/LZMA2 compression
bit 1 one or more payload chunks are stored raw as an adaptive fallback
bit 2 address index present
bit 3 public transport data present
bit 4 walking overlay present
bit 5 chunk checksums present
```

`routing_profile_mask` values:

```text
bit 0 walking
bit 1 wheelchair/accessibility walking
bit 2 public transport
bit 3 bicycle, optional
bit 4 road vehicle, optional
```

`tile_scheme` values:

```text
0 unknown
1 local metric grid
2 geographic grid
3 Web Mercator
```

The recommended default is `tile_scheme = 1` with `projection_kind = 1`, a local equirectangular metric grid. Metric route tiles make snapping radii, edge costs, and neighborhood expansion easier to reason about than degree-based tiles.

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
bit 2 hot query path
bit 3 directory/index
bit 4 global payload
```

Core section types:

```text
0x0001 chunk directory
0x0002 route tile directory
0x0003 string table
0x0100 tile lookup index, optional
0x0200 address dictionaries
0x0201 address lookup index
0x0300 global stop table
0x0301 stop locality index
0x0400 transit route table
0x0401 transit pattern table
0x0402 stop-to-pattern index
0x0403 trip table
0x0404 calendar/service table
0x0405 event page index
0x0500 walking overlay index, optional
0x0501 transit transfer index, optional
0x0700 debug/source map, optional
```

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
0x1000 route tile payload
0x1001 tile shape geometry payload
0x1002 tile address payload
0x1003 tile stop-link payload
0x2000 transit event page
0x2001 transit shape payload
0x2002 transfer footpath payload
0x3000 walking overlay payload
0x7000 debug/source payload
```

Chunk flags:

```text
bit 0 required for normal routing
bit 1 hot after query planning
bit 2 contains graph or event arrays
bit 3 contains cold metadata
bit 4 stored raw because standard compression was not useful
```

The chunk directory is uncompressed and sorted by `(type, chunk_id)`. Directory records and indexes point to chunk IDs. If chunk flag bit 4 is set, `compressed_size` must equal `uncompressed_size` and the payload bytes are stored raw. Otherwise payload bytes are encoded as a single XZ stream.

## Metric Tile Coordinate System

For `tile_scheme = 1` and `projection_kind = 1`, the pack uses a local equirectangular metric grid anchored at the header projection origin:

```text
origin_lat = projection_origin_lat_e7 / 10000000.0
origin_lon = projection_origin_lon_e7 / 10000000.0
lat        = lat_e7 / 10000000.0
lon        = lon_e7 / 10000000.0
origin_lat_rad = origin_lat * pi / 180.0

meters_per_degree_lat = 111320.0
meters_per_degree_lon = 111320.0 * cos(origin_lat_rad)

metric_x_m = round((lon - origin_lon) * meters_per_degree_lon)
metric_y_m = round((lat - origin_lat) * meters_per_degree_lat)

tile_x = floor_div(metric_x_m, tile_size_m)
tile_y = floor_div(metric_y_m, tile_size_m)
```

Negative coordinates use mathematical floor division, not truncation toward zero.

Tile IDs are sortable:

```text
x_code  = tile_x + 0x10000000
y_code  = tile_y + 0x10000000
tile_id = (level << 58) | (x_code << 29) | y_code
```

## Route Tile Directory

Record size: 128 bytes.

```text
offset size field
0      8    tile_id
8      4    level
12     4    x
16     4    y
20     4    flags
24     4    local_node_count
28     4    local_directed_edge_count
32     4    portal_count
36     4    stop_count
40     4    address_count
44     4    snap_cell_count
48     4    min_lon_e7
52     4    min_lat_e7
56     4    max_lon_e7
60     4    max_lat_e7
64     8    payload_chunk_id
72     8    payload_uncompressed_size
80     8    address_chunk_id
88     8    stop_link_chunk_id
96     8    shape_chunk_id
104    8    neighbor_mask
112    8    first_overlay_ref
120    8    reserved = 0
```

Records are sorted by `tile_id`. Readers must support binary search over the tile directory. Optional tile lookup sections may provide hash or dense-grid lookup acceleration.

`neighbor_mask` marks existing immediate neighbors:

```text
bit 0 northwest  x - 1, y + 1
bit 1 north      x,     y + 1
bit 2 northeast  x + 1, y + 1
bit 3 west       x - 1, y
bit 4 east       x + 1, y
bit 5 southwest  x - 1, y - 1
bit 6 south      x,     y - 1
bit 7 southeast  x + 1, y - 1
```

## Route Tile Payload

Each route tile payload is stored in a chunk and decompresses to a compact internal directory plus hot walking arrays.

```text
tile payload header       64 bytes
tile payload directory    directory_count * 32 bytes
tile-local arrays         aligned as required
```

Tile payload header size: 64 bytes.

```text
offset size field
0      8    tile_id
8      4    payload_format_version = 1
12     4    flags
16     4    directory_count
20     4    directory_record_size = 32
24     8    directory_offset_relative_to_payload
32     4    local_node_count
36     4    local_directed_edge_count
40     4    snap_cell_count
44     4    address_count
48     4    stop_link_count
52     4    portal_count
56     8    reserved = 0
```

Payload directory record size: 32 bytes.

```text
offset size field
0      4    type
4      4    flags
8      8    relative_offset_from_tile_payload
16     8    size
24     4    record_count
28     4    record_size
```

Walking payload types:

```text
0x1000 walking nodes
0x1001 walking adjacency offsets
0x1002 walking directed edges
0x1003 snap grid
0x1004 portal records
0x1005 stop links
0x1006 tile-local addresses
0x1007 shape geometry refs
```

## Walking Graph

The walking graph uses compressed sparse row adjacency inside each tile.

Hot arrays:

```text
node records             local_node_count * 16 bytes
node_edge_offsets        (local_node_count + 1) * 4 bytes
directed edge records    local_directed_edge_count * 20 bytes
```

Node record size: 16 bytes.

```text
offset size field
0      4    lat_e7
4      4    lon_e7
8      4    first_portal_or_stop_link, 0xffffffff when absent
12     2    flags
14     2    elevation_dm_or_0
```

Directed edge record size: 20 bytes.

```text
offset size field
0      4    to_local_node
4      4    cost_seconds
8      4    distance_m
12     4    name_string_id
16     2    flags
18     1    surface_class
19     1    grade_class
```

Directed edges are tile-local. Cross-tile movement is represented by portal records and by the query engine stitching loaded tiles into a temporary search graph.

Edge flags:

```text
bit 0 forward source direction
bit 1 backward source direction
bit 2 stairs
bit 3 ramp
bit 4 indoor/covered
bit 5 bridge
bit 6 tunnel
bit 7 ferry
bit 8 unpaved
bit 9 wheelchair allowed
bit 10 wheelchair forbidden
bit 11 private/no access
```

## Snap Grid

Each tile has a snap grid over walking edge bounding boxes. It maps cells to candidate edge references.

Snap grid header:

```text
offset size field
0      4    grid_width
4      4    grid_height
8      4    cell_count
12     4    edge_ref_count
16     4    cell_size_m
20     4    flags
24     8    cell_offsets_offset
32     8    edge_refs_offset
40     24   reserved = 0
```

Arrays:

```text
cell_offsets[cell_count + 1] uint32
edge_refs[edge_ref_count]    uint32 directed_edge_index
```

Snapping creates temporary query nodes outside the mapped file. The query engine connects each temporary node to both endpoints of the nearest accessible edge with proportional costs.

## Address Index

Addresses are resolved through global normalized dictionaries and optional tile-local payloads.

Global address key:

```text
city_string_id
suburb_string_id
street_string_id
house_string_id
postcode_string_id
```

Address lookup records should be sorted by normalized `(place, street, house)` keys. Each result points to a coordinate, a primary tile, and optionally a nearest edge hint.

Address record size: 40 bytes.

```text
offset size field
0      4    city_string_id
4      4    street_string_id
8      4    house_string_id
12     4    lat_e7
16     4    lon_e7
20     8    primary_tile_id
28     4    nearest_edge_ref
32     4    flags
36     4    source_rank
```

The builder should choose one best address point per normalized key and may store alternates in optional cold chunks.

## Stop Table

Stop record size: 48 bytes.

```text
offset size field
0      4    stop_id_string_id
4      4    stop_name_string_id
8      4    lat_e7
12     4    lon_e7
16     8    primary_tile_id
24     4    mode_mask
28     4    first_pattern_ref
32     4    pattern_ref_count
36     4    first_walk_link_ref
40     4    walk_link_count
44     4    flags
```

Mode mask values:

```text
bit 0 tram
bit 1 subway
bit 2 rail/train
bit 3 bus
bit 4 ferry
bit 5 other transit
```

Stop-to-walk links may be stored globally for fast transfer initialization or per tile for locality.

Stop walk-link record size: 24 bytes.

```text
offset size field
0      4    stop_id
4      8    tile_id
12     4    local_node_id
16     4    walk_seconds
20     4    walk_distance_m
```

## Transit Routes And Patterns

Transit data is route-pattern based. A pattern is a route variant with the same ordered stop sequence.

Route record size: 32 bytes.

```text
offset size field
0      4    route_string_id
4      4    short_name_string_id
8      4    long_name_string_id
12     2    mode
14     2    flags
16     4    first_pattern_id
20     4    pattern_count
24     8    reserved = 0
```

Pattern record size: 48 bytes.

```text
offset size field
0      4    route_id
4      4    stop_count
8      4    first_stop_id_ref
12     4    trip_count
16     4    first_trip_id
20     4    first_stop_pattern_ref
24     4    first_event_page_ref
28     4    event_page_count
32     4    flags
36     12   reserved = 0
```

Pattern stop IDs:

```text
pattern_stop_ids[] uint32 stop_id
```

Stop-to-pattern index:

```text
stop_pattern_offsets[stop_count + 1] uint32
stop_pattern_refs[]                 uint32 pattern_id
```

For stop `s`, candidate patterns are:

```text
stop_pattern_refs[stop_pattern_offsets[s] .. stop_pattern_offsets[s + 1])
```

This index is required for public-transport routing.

## Trips, Calendars, And Event Pages

Trip record size: 32 bytes.

```text
offset size field
0      4    pattern_id
4      4    service_mask_id
8      4    first_event_ref
12     4    event_count
16     4    headsign_string_id
20     4    trip_string_id
24     4    flags
28     4    reserved = 0
```

Service mask record size: 16 bytes.

```text
offset size field
0      4    first_day_index
4      4    day_count
8      4    bitset_offset_u32
12     4    flags
```

Service active bitsets:

```text
service_active_bits[] uint64 words
```

Transit events are stored in independently loadable event pages. Pages are selected by pattern, route, service day, and time window.

Event page index record size: 48 bytes.

```text
offset size field
0      4    pattern_id
4      4    first_trip_id
8      4    trip_count
12     4    first_departure_sec
16     4    last_arrival_sec
20     4    service_mask_min_id
24     4    service_mask_max_id
28     4    flags
32     8    event_chunk_id
40     8    reserved = 0
```

Event record size: 16 bytes.

```text
offset size field
0      4    trip_local_index
4      4    stop_position
8      4    arrival_seconds_since_service_day_start
12     4    departure_seconds_since_service_day_start
```

Event pages should be sized so typical queries decompress a small number of pages. A practical first policy is one page per pattern per service-time range, with a target uncompressed size between 32 KiB and 512 KiB.

## Compression Strategy

Compression is applied at chunk granularity.

Good compression candidates:

- route tile payloads
- tile shape geometry
- tile-local address payloads
- transit event pages
- transfer footpath payloads
- debug/source maps
- cold metadata

Usually uncompressed:

- header
- section directory
- chunk directory
- tile directory
- stop-to-pattern offsets and refs
- event page index
- small hot lookup tables

Recommended builder policy:

```text
if uncompressed_size < 1024 bytes: store raw
else compress with XZ/LZMA2 preset 6
if compressed_size + chunk_overhead >= uncompressed_size * 0.95: store raw
```

Readers should keep a decoded-chunk cache. A walking route query commonly needs only endpoint tiles, a 3x3 neighborhood, or a corridor. A transit query commonly needs origin/destination walking tiles plus event pages for candidate patterns. Readers are not required to carry alternate codec implementations.

## Walking Query Algorithm

```text
resolve origin address/place/coordinate
resolve destination address/place/coordinate
compute endpoint tiles
load endpoint tile payloads
snap endpoints to walking edges

if same tile:
    run A* in tile
    if path found and accepted: return

load expanded tile neighborhood
run A* over temporary merged graph
if path found and accepted: return

if overlay exists:
    route origin to portal set
    route through overlay graph
    route destination portal set to target
    stitch path
```

The default heuristic is straight-line walking time from a node coordinate to the destination.

## Transit Query Algorithm

```text
resolve and snap origin/destination
walk from origin to nearby stops
initialize earliest arrival per stop

for each RAPTOR round:
    collect patterns serving updated stops
    use event page index to select candidate pages
    decompress selected event pages
    scan active trips for the query date and time
    update downstream stops
    apply transfer/walk links

walk from candidate alighting stops to destination
compare against walking-only route
return best itinerary
```

The event page index is the main scale control. The query must avoid scanning every event in a country-sized or continent-sized pack.

## Journey Reconstruction

The runtime keeps predecessor state outside the mapped file.

Walking predecessor:

```text
previous local node
tile id
edge index
temporary endpoint marker
```

Transit predecessor:

```text
previous stop
pattern id
trip id
board stop position
alight stop position
event page id
```

The file provides enough names, IDs, times, and geometry references to reconstruct human-readable directions without source files:

- edge names
- optional shape geometry
- stop names and IDs
- route short and long names
- trip IDs and headsigns
- pattern stop positions
- event arrival and departure times

## Builder Responsibilities

The builder should:

- parse OSM PBF in deterministic phases
- select walkable ways according to the routing profile
- split ways at intersections, access changes, barriers, tile boundaries, stops, and portal points
- collapse degree-2 geometry chains only when route choice and instructions are preserved
- build per-tile CSR walking graphs
- build snap grids over walking edges
- normalize and index addresses
- parse public-transport feeds when supplied
- group trips into route patterns
- resolve service calendars into bitsets
- write stop-to-pattern indexes
- partition events into independently loadable pages
- precompute stop-to-walk links
- write hot indexes uncompressed
- compress payload chunks independently

Build output should be deterministic. Worker-local staging data must be merged by stable sort keys rather than by thread completion order.

## Runtime Responsibilities

The query engine should:

- validate headers, directory sizes, record sizes, and required sections
- avoid copying hot lookup arrays unnecessarily
- decompress only selected chunks
- cache recently decoded tile and event chunks
- keep temporary query nodes outside the mapped file
- keep per-query heaps, labels, predecessors, and output buffers separate for concurrent queries
- reject chunks whose decompressed size does not match the chunk directory
- ignore unknown optional sections
- reject unknown required sections
- avoid requiring source PBF or transit CSV files for normal routing

## Minimum Useful RTE

A minimum useful file contains:

- header
- section directory
- chunk directory
- route tile directory
- string table
- address lookup index
- route tile walking payload chunks
- snap grids
- stop table, when public transport is present
- route and pattern tables, when public transport is present
- stop-to-pattern index, when public transport is present
- trip and calendar tables, when public transport is present
- event page index and event page chunks, when public transport is present

This is enough to route without reading source PBF or transit CSV files at query time.
