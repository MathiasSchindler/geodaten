# OSMRTE01 Route Pack Format

`OSMRTE01` is a proposed binary, memory-mappable route-pack format for fast walking and public-transport routing over a large OSM/GTFS region such as Brandenburg or Germany.

The format is optimized for route queries, not for interchange. The builder may spend minutes parsing OSM PBF and GTFS CSV files, simplifying graphs, building indexes, and sorting schedule events. A query tool should avoid all source parsing and should be able to answer common local routes by touching only a small number of memory-mapped sections.

Primary goals:

- avoid scanning `.osm.pbf` or GTFS CSV files at query time
- support anywhere-to-anywhere routing by snapping coordinates, addresses, POIs, and stops onto a reusable walking graph
- make local walking routes fast with tiled graph sections
- make transit routing fast with pre-grouped route patterns and stop-time event arrays
- keep hot routing arrays uncompressed and cache-friendly
- keep cold metadata compact and optionally compressed
- allow future acceleration indexes without changing the core graph layout

Non-goals for version 1:

- interchange with arbitrary GIS tools
- preserving all original OSM tags
- turn-by-turn road-vehicle routing
- perfect fare modelling
- real-time GTFS-RT updates in the base file

## High-Level Layout

```text
OSMRTE01 file
  header                    fixed 256 bytes
  section directory          fixed records
  tile directory             one record per non-empty route tile
  global string table        UTF-8 names and IDs
  global address dictionaries
  global transit tables      stops, routes, patterns, trips, events, calendars
  tile payloads              walking graph, snap index, addresses, stop links
  optional overlay indexes   long-distance walking acceleration
  optional metadata/checksums
```

The file is a sectioned binary container. Every section is independently typed, sized, and aligned. Query tools should be able to `mmap` the file, validate the header and directory, and then use array offsets directly.

Core graph/search sections should remain uncompressed. Cold sections such as names, original IDs, debug metadata, and detailed shape polylines may be compressed in later versions, but version 1 should prefer simple direct arrays.

## Encoding Rules

- All integers are little-endian.
- Offsets are absolute byte offsets from the start of the file.
- Counts are unsigned.
- Coordinates use signed fixed-point WGS84 degrees scaled by `1e7` unless a section explicitly says otherwise.
- Strings are UTF-8 byte strings and are not required to have NUL terminators.
- Hot arrays are aligned to 64 bytes.
- Records contain zero-filled reserved fields.
- Unknown section types are ignored when the section is not marked required.
- Required unknown section types make the file unsupported.

Coordinate scale:

```text
lat_e7 = round(latitude_degrees  * 10000000)
lon_e7 = round(longitude_degrees * 10000000)
```

Germany fits comfortably in signed 32-bit coordinates at this scale. The precision is finer than routing needs and avoids floating point in file storage.

## Header

Header size: 256 bytes.

```text
offset size field
0      8    magic = "OSMRTE01"
8      4    version = 1
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
80     8    tile_directory_offset
88     8    tile_directory_size
96     4    tile_record_size = 128
100    4    tile_count
104    4    tile_scheme
108    4    tile_level_count
112    4    coord_scale = 10000000
116    4    routing_profile_mask
120    4    projection_kind
124    4    tile_size_m
128    4    projection_origin_lat_e7
132    4    projection_origin_lon_e7
136    4    tile_lookup_kind
140    4    reserved_projection = 0
144    4    min_lon_e7
148    4    min_lat_e7
152    4    max_lon_e7
156    4    max_lat_e7
160    8    string_table_offset
168    8    string_table_size
176    8    metadata_offset
184    8    metadata_size
192    4    checksum_kind
196    4    header_checksum
200    24   routing_profile_fingerprint
224    32   source_fingerprint
256         end of header
```

`tile_scheme` values:

```text
0 unknown
1 metric grid, fixed meter-like projected cells
2 geographic grid compatible with OSMRPK tile math
3 Web Mercator tile IDs
```

Version 1 should use `tile_scheme = 1`: a local metric grid derived from a file-level projection origin. Metric tiles make walking distances, snapping radii, and tile-neighborhood expansion easier to reason about than latitude/longitude degree tiles.

`projection_kind` values:

```text
0 unknown
1 local equirectangular metric grid using projection_origin_lat_e7/projection_origin_lon_e7
```

Version 1 should use `projection_kind = 1`. The conversion is defined in [Metric Tile Coordinate System](#metric-tile-coordinate-system).

`tile_size_m` is the edge length of level-0 metric tiles. Recommended values are 4000 or 8000.

`tile_lookup_kind` values:

```text
0 sorted tile directory only, binary search by tile_id
1 optional sparse tile lookup section present
2 optional dense grid tile lookup section present
```

Version 1 readers must support `tile_lookup_kind = 0`. Other lookup kinds are acceleration sections; they must not be required to route.

`routing_profile_mask` is a bitset of profiles included in the pack:

```text
bit 0 walking
bit 1 wheelchair/accessibility walking
bit 2 public transport
bit 3 bicycle, future
bit 4 road vehicle, future
```

`routing_profile_fingerprint` identifies the build-time access and costing profile. It should change whenever the builder changes rules such as allowed OSM highway types, sidewalk handling, private-access policy, stairs cost, wheelchair filtering, or walking speed assumptions. Query tools can display this fingerprint and may reject packs whose profile is incompatible with the requested mode.

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
bit 3 tile-local payload
bit 4 global payload
```

Recommended section type ranges:

```text
0x0001..0x00ff container metadata
0x0100..0x01ff strings and dictionaries
0x0200..0x02ff tile directories and tile indexes
0x0300..0x03ff walking graph global/overlay sections
0x0400..0x04ff address/place lookup sections
0x0500..0x05ff transit sections
0x0600..0x06ff acceleration indexes
0x0700..0x07ff debug/source mapping sections
```

Recommended additional section types:

```text
0x0201 sparse or dense tile lookup index
0x0400 global city/street/house dictionaries
0x0505 stop-to-pattern index, required when public transport profile is present
0x0506 optional stop transfer footpath index
0x0700 optional OSM/GTFS source ID maps
```

Version 1 required global section types:

```text
0x0100 string table
0x0200 tile directory
0x0400 address dictionaries, when address lookup is present
0x0500 transit stops, when public transport profile is present
0x0501 transit route patterns, when public transport profile is present
0x0502 transit trips, when public transport profile is present
0x0503 transit stop events, when public transport profile is present
0x0504 transit calendar/service days, when public transport profile is present
0x0505 stop-to-pattern index, when public transport profile is present
```

Current implementation note: `pbf-to-rte --gtfs DIR` writes a compact first transit payload in section `0x0500`. The section is named `GTFSLEG1` and stores stops, routes, services, trips, and stop-time events. `rte-route --depart` can chain multiple scheduled vehicle legs with walking access, walking transfers between nearby stops, and walking egress without reading GTFS CSV files at query time. Pass `--transit` to run only the public-transport planner and skip walking-route graph loading/output; pass `--verbose` to include successful human diagnostic fields. `--arrive` currently keeps a compact single-leg fallback. Future sections `0x0501..0x0505` can add optimized route-pattern and transfer indexes alongside it.

### GTFSLEG1 Transit Section

Section type: `0x0500`.

Header size: 128 bytes.

```text
offset size field
0      8    magic = "GTFSLEG1"
8      4    version = 1
12     4    header_size = 128
16     8    stop_count
24     8    route_count
32     8    service_count
40     8    exception_count
48     8    trip_count
56     8    event_count
64     4    string_size
68     4    reserved = 0
72     8    stops_offset_relative_to_section
80     8    routes_offset_relative_to_section
88     8    services_offset_relative_to_section
96     8    exceptions_offset_relative_to_section
104    8    trips_offset_relative_to_section
112    8    events_offset_relative_to_section
120    8    strings_offset_relative_to_section
```

String offsets in the records below point into the section-local UTF-8 string blob. Strings are byte spans and are not required to be NUL terminated.

Stop record size: 40 bytes.

```text
offset size field
0      4    stop_id_offset
4      4    stop_id_size
8      4    stop_name_offset
12     4    stop_name_size
16     4    lat_e7
20     4    lon_e7
24     8    tile_id
32     4    mode_mask
36     4    reserved = 0
```

Route record size: 24 bytes.

```text
offset size field
0      4    route_short_name_offset
4      4    route_short_name_size
8      4    route_long_name_offset
12     4    route_long_name_size
16     4    mode
20     4    original_gtfs_route_type
```

Mode values:

```text
1 tram
2 subway
3 rail/train
4 bus
5 other transit
```

Service record size: 32 bytes.

```text
offset size field
0      4    service_id_offset
4      4    service_id_size
8      4    start_date YYYYMMDD
12     4    end_date YYYYMMDD
16     4    weekday_mask, bit 0 = Monday
20     4    exception_offset
24     4    exception_count
28     4    reserved = 0
```

Exception record size: 16 bytes.

```text
offset size field
0      4    service_index
4      4    date YYYYMMDD
8      4    exception_type, GTFS calendar_dates semantics
12     4    reserved = 0
```

Trip record size: 12 bytes.

```text
offset size field
0      4    route_index
4      4    service_index
8      4    mode
```

Stop-time event record size: 20 bytes.

```text
offset size field
0      4    trip_index
4      4    stop_index
8      4    arrival_sec, may exceed 86400 for after-midnight trips
12     4    departure_sec, may exceed 86400 for after-midnight trips
16     4    stop_sequence
```

Events are sorted by `(trip_index, stop_sequence)`. The query tool scans active trips, finds candidate boarding stops within the access-walk radius, then finds a later alighting stop within the egress-walk radius.

Per-tile walking payloads are reached from tile directory records rather than by scanning the global section directory.

## Tile Model

Tiles are storage and locality units. They are not hard route boundaries.

The query engine should use this hierarchy:

1. For same-tile endpoints, try one-tile walking search.
2. If endpoints are near a tile edge, if no path is found, or if the path is suspiciously long, expand to a 3x3 neighborhood.
3. For longer walking routes, use portal/overlay sections when present.
4. For transit routes, use tile-local walking only for origin/destination access and transfers, then search global transit arrays.

This avoids loading a Germany-wide walking graph for a neighborhood route while still keeping cross-tile routing possible.

Recommended version 1 tile size: 4 km or 8 km metric tiles. Use 4 km for denser city packs, 8 km for state/country packs when portal overhead matters more than local array size.

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

The builder and reader must use the same integer rounding rule. Negative coordinates use mathematical floor division, not truncation toward zero.

This projection is not intended for survey-grade measurement. It is sufficient for snapping, tile membership, and route heuristics over a country-sized region. Distances stored on graph edges come from builder-side geometry measurement, not from recomputing edge length from the tile grid.

Tile lookup is performed by computing `tile_x` and `tile_y`, forming the sortable `tile_id`, and then locating the tile directory record. Version 1 readers must support binary search over the tile directory sorted by `tile_id`. Optional lookup sections may provide a sparse hash or dense grid table from `(level, x, y)` to tile directory index.

The tile coordinate system is part of the file contract. A builder must not silently change projection origin, tile size, or tile assignment rules without changing the header fields and routing profile/build metadata.

## Tile Directory

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
64     8    payload_offset
72     8    payload_size
80     8    payload_directory_offset
88     4    payload_directory_count
92     4    payload_directory_record_size = 32
96     8    neighbor_mask
104    24   reserved = 0
```

`tile_id` should be sortable. For a metric grid:

```text
x_code  = tile_x + 0x10000000
y_code  = tile_y + 0x10000000
tile_id = (level << 58) | (x_code << 29) | y_code
```

`x` and `y` in the tile directory are signed 32-bit tile coordinates. `x_code` and `y_code` are biased unsigned 29-bit values used only for sortable IDs. Version 1 builders must reject tile coordinates outside the encodable range.

`neighbor_mask` marks which of the 8 immediate neighboring tile positions exist. It is a fast hint for neighborhood expansion. Bit order is:

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

## Tile Payload Directory

Each tile payload begins at `payload_offset` and contains:

```text
tile payload header       fixed 64 bytes
tile payload directory    payload_directory_count records
tile-local arrays         aligned as required by each payload type
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

The counts repeat the hot tile directory counts intentionally. The tile directory lets the runtime reject or rank tiles without touching tile payload pages; the tile payload header lets tools validate a payload after mapping it.

Each tile payload starts with a compact local directory. Record size: 32 bytes.

```text
offset size field
0      4    type
4      4    flags
8      8    relative_offset_from_tile_payload
16     8    size
24     4    record_count
28     4    record_size
```

Required tile payload types for walking:

```text
0x1000 tile walking nodes
0x1001 tile walking adjacency offsets
0x1002 tile walking directed edges
0x1003 tile snap grid
```

Optional tile payload types:

```text
0x1004 tile addresses
0x1005 tile stops and stop-to-walk links
0x1006 tile portals
0x1007 tile shape geometry
0x1008 tile source ID map
```

Payload directory records are sorted by `type`. Unknown optional payload records are skipped. Unknown records marked required make the tile payload unsupported.

## Graph Simplification And PBF Deduplication

The route pack should not preserve the OSM PBF graph verbatim. The builder converts noisy source data into dense routing arrays.

For walking, the builder should:

- discard non-walkable ways and irrelevant tags before graph materialization
- reduce access, surface, stairs, bridge, tunnel, indoor, ferry, and wheelchair tags into compact edge flags/classes
- split ways at intersections, barriers, access changes, tile boundaries, stop access points, and portal points
- collapse degree-2 geometry chains when the intermediate nodes are not routing decisions
- keep shape geometry for snapped directions only when it is needed outside the hot adjacency arrays
- intern street names, route names, stop names, city names, and house-number strings in the global string table
- replace OSM node and way IDs with dense tile-local node IDs and directed edge indices
- store original OSM IDs only in optional cold source-map sections

The preferred hot graph model is:

```text
routing nodes = intersections, endpoints, portals, stops, barriers, access changes
directed edges = simplified walkable segments between routing nodes
shape payload = optional polyline used for snapping, display, and turn text
```

Degree-2 collapse must preserve correct costs, names, access flags, and enough geometry for snap distance and plain-language output. It must not collapse across a change that affects route choice or route instructions, such as a different street name, stairs, access restriction, surface class, or tile boundary.

For addresses, the builder should normalize `(city, street, house)` into dictionary IDs, choose one best address point per normalized key by `source_rank`, and keep alternates only in optional cold sections. `nearest_edge_ref` should be precomputed so address routing does not repeat full snapping work.

For GTFS, the builder should discard unused source columns after converting them into stable stop, route, pattern, trip, event, service, and string IDs. Identical stop sequences become one pattern. Identical service calendars become one service mask. Parent-station metadata may be deduplicated, but platform-level stops remain distinct when schedules distinguish them.

## Walking Graph Representation

The walking graph uses compressed sparse row adjacency inside each tile.

Hot query arrays:

```text
node records             local_node_count * 16 bytes
node_edge_offsets        (local_node_count + 1) * 4 bytes
directed edge records    local_directed_edge_count * 20 bytes
```

### Tile Walking Node Record

Record size: 16 bytes.

```text
offset size field
0      4    lat_e7
4      4    lon_e7
8      4    first_portal_or_stop_link, 0xffffffff when absent
12     2    flags
14     2    elevation_dm_or_0
```

Node flags:

```text
bit 0 portal node
bit 1 transit stop access node
bit 2 barrier/gate nearby
bit 3 stairs nearby
bit 4 wheelchair-relevant metadata present
```

### Node Edge Offsets

Array item size: 4 bytes.

For local node `n`, outgoing directed edges are:

```text
start = node_edge_offsets[n]
end   = node_edge_offsets[n + 1]
edges = directed_edges[start..end)
```

The array length is `local_node_count + 1`.

### Directed Walking Edge Record

Record size: 20 bytes.

```text
offset size field
0      4    to_local_node
4      4    cost_centiseconds_or_seconds
8      4    distance_cm_or_m
12     4    name_string_id
16     2    flags
18     1    surface_class
19     1    grade_class
```

Version 1 should use seconds and meters for costs/distances unless sub-meter shape precision is required. If future profiles need finer cost precision, the header or section flags can declare centiseconds/centimeters.

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

The graph stores directed edges. A bidirectional OSM way segment becomes two directed edge records. This keeps query code simple and supports asymmetric future costs.

Normal directed walking edges are tile-local: `to_local_node` always names a node in the same tile payload. Cross-tile movement is represented by portal records, not by direct adjacency into another tile payload. Query engines that search multiple tiles insert portal records as additional edges in their temporary search state.

Implementation note: the first `pbf-to-rte` walking-graph writer keeps `to_local_node` tile-local, but represents cross-tile OSM way segments by duplicating that segment into both endpoint tile payloads. `rte-route` then merges duplicate coordinates while loading a small tile neighborhood. This is an interim compatibility path until explicit portal payloads and overlay routing are written.

## Snapping Index

Anywhere-to-anywhere routing depends on fast snapping to graph edges, not only graph nodes.

Each tile has a snap grid over walking edge bounding boxes. The grid maps cells to candidate directed or undirected edge references.

Tile snap grid header:

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

Cell offsets array:

```text
cell_offsets[cell_count + 1] uint32
```

Edge refs array:

```text
edge_refs[edge_ref_count] uint32
```

`edge_refs` points to tile directed edge indices. The query tool may normalize opposite directed edges into one undirected candidate while computing point-to-segment distance.

Snapping algorithm:

1. Convert the input coordinate to a tile and snap-grid cell.
2. Search that cell and neighboring cells.
3. For each candidate edge, compute point-to-segment distance using node coordinates and optional shape geometry.
4. Pick the nearest accessible edge within the configured radius.
5. Create a temporary query node connected to both edge endpoints with proportional costs.

The graph file should not precompute all possible origin/destination pairs. It should store a reusable graph plus a fast snap index.

## Address Index

Address lookup is tile-local for fast common queries, with global dictionaries for normalized strings.

Global dictionaries:

```text
city dictionary
street dictionary
house-number normalization table, optional
```

Tile address record size: 32 bytes.

```text
offset size field
0      4    city_string_id
4      4    street_string_id
8      4    house_string_id
12     4    lat_e7
16     4    lon_e7
20     4    nearest_edge_ref
24     4    flags
28     4    source_rank
```

Records should be sorted by `(city_string_id, street_string_id, house_string_id)` for binary search. A later prefix/fuzzy index can be added as an optional section.

For address routing, the query engine resolves the address record, then snaps the stored coordinate to the nearest edge or uses `nearest_edge_ref` as a strong hint.

Current converter milestone: section `0x0400` may contain a temporary global address scan payload before the optimized tile-local dictionary above is implemented. The payload begins with a 64-byte `ADDRIDX1` header, followed by 80-byte records and a local UTF-8 string blob. Records store source entity type, source ID, optional node coordinate, tile ID, and offsets/sizes for state, city, suburb, street, house number, and postcode. This is intended for verifying address availability and reader plumbing; it is not the final hot-path address index.

## Portals And Cross-Tile Walking

Portal records link local walking graphs across tile boundaries and feed optional overlay routing.

Tile portal record size: 32 bytes.

```text
offset size field
0      4    local_node_id
4      4    neighbor_tile_local_index_or_global_tile_ref
8      4    neighbor_local_node_id
12     4    overlay_node_id, 0xffffffff when absent
16     4    crossing_cost
20     4    flags
24     8    reserved = 0
```

Version 1 can work without a global overlay by searching tile neighborhoods. For Germany-wide long walking routes, add overlay sections later:

```text
overlay node = tile portal
overlay edge = precomputed best path between important portals
```

A practical query progression:

```text
same tile search
3x3 tile window search
rectangular tile corridor search for medium distances
overlay-assisted search for long distances
```

## Transit Model

Transit data is global and route-pattern based. Tile sections only provide stop locality and walking access links.

The recommended query algorithm is RAPTOR-like:

1. Walk from origin to nearby stops using tile walking graph.
2. Run rounds over route patterns reachable from marked stops.
3. Track earliest arrival per stop.
4. Use tile-local walking from candidate destination stops to the target.

This avoids scanning `stop_times.txt` at query time.

Transit sections are compound sections: each starts with a small internal header followed by one or more aligned arrays. Offsets inside these headers are relative to the start of the section, not absolute file offsets. The global section directory still provides the absolute section start and total size.

Common compound section rule:

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16          section-specific offset/count pairs
```

### Transit Stops Section Header, Type 0x0500

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16     8    stop_records_offset
24     4    stop_count
28     4    stop_record_size = 40
32     8    global_walk_links_offset, 0 when absent
40     4    global_walk_link_count
44     4    global_walk_link_record_size = 24
48     8    reserved = 0
```

Version 1 may store stop-to-walk links per tile only. In that case, `global_walk_links_offset` and `global_walk_link_count` are zero.

### Transit Routes And Patterns Section Header, Type 0x0501

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16     8    route_records_offset
24     4    route_count
28     4    route_record_size = 32
32     8    pattern_records_offset
40     4    pattern_count
44     4    pattern_record_size = 40
48     8    pattern_stop_ids_offset
56     4    pattern_stop_id_count
60     4    pattern_stop_id_record_size = 4
64     8    reserved = 0
```

### Transit Trips Section Header, Type 0x0502

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16     8    trip_records_offset
24     4    trip_count
28     4    trip_record_size = 32
32     8    reserved = 0
```

### Transit Events Section Header, Type 0x0503

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16     8    event_records_offset
24     4    event_count
28     4    event_record_size = 12
32     8    reserved = 0
```

### Transit Calendar Section Header, Type 0x0504

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16     8    service_mask_records_offset
24     4    service_mask_count
28     4    service_mask_record_size = 16
32     8    service_active_bits_offset
40     4    service_active_bit_word_count
44     4    pack_epoch_yyyymmdd
48     4    calendar_day_count
52     4    reserved = 0
```

### Stop-To-Pattern Section Header, Type 0x0505

```text
offset size field
0      4    format_version = 1
4      4    flags
8      8    header_size
16     8    stop_pattern_offsets_offset
24     4    stop_pattern_offset_count
28     4    stop_pattern_offset_record_size = 4
32     8    stop_pattern_refs_offset
40     4    stop_pattern_ref_count
44     4    stop_pattern_ref_record_size = 4
48     8    reserved = 0
```

The stop-to-pattern section is required for public transport routing. Without it, boarding would require scanning all patterns or all trips.

## Transit Stops

Global stop record size: 40 bytes.

```text
offset size field
0      4    stop_string_id
4      4    stop_name_string_id
8      4    lat_e7
12     4    lon_e7
16     8    primary_tile_id
24     4    first_route_pattern_ref
28     4    route_pattern_ref_count
32     4    first_walk_link_ref
36     4    walk_link_count
```

Stop IDs and names are stored in the global string table. Platform-level GTFS stops may share the same parent station name but should remain distinct stops when schedules distinguish them.

## Stop-To-Walk Links

Stop walking access links can be stored globally or per tile. Per-tile storage is better for local access queries.

Record size: 24 bytes.

```text
offset size field
0      4    stop_id
4      8    tile_id
12     4    local_node_id
16     4    walk_seconds
20     4    walk_distance_m
```

The builder should precompute links from each stop to nearby walking graph nodes/edges within a radius such as 500 m to 1500 m. Larger transfer radii can be represented by optional transfer sections rather than bloating every stop.

## Transit Routes And Patterns

GTFS trips should be grouped by route pattern: a pattern is a route variant with the same ordered stop sequence.

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

Mode values:

```text
1 tram
2 subway
3 rail
4 bus
5 ferry
6 cable/other
```

Pattern record size: 40 bytes.

```text
offset size field
0      4    route_id
4      4    stop_count
8      4    first_stop_id_ref
12     4    trip_count
16     4    first_trip_id
20     4    first_stop_route_ref
24     4    flags
28     12   reserved = 0
```

Pattern stop ID array:

```text
pattern_stop_ids[] uint32 stop_id
```

`first_stop_id_ref` points into this array.

## Transit Trips And Events

Trip record size: 32 bytes.

```text
offset size field
0      4    pattern_id
4      4    service_mask_id
8      4    first_event_index
12     4    event_count
16     4    headsign_string_id
20     4    trip_string_id
24     4    flags
28     4    reserved = 0
```

Events are stored in pattern stop order. For a trip with `event_count == pattern.stop_count`, event `first_event_index + i` belongs to `pattern_stop_ids[first_stop_id_ref + i]`.

Transit event record size: 12 bytes.

```text
offset size field
0      4    arrival_seconds_since_service_day_start
4      4    departure_seconds_since_service_day_start
8      2    pickup_dropoff_flags
10     2    reserved = 0
```

GTFS times beyond 24:00 are stored directly as seconds greater than 86400. The query engine handles day rollover.

## Stop-To-Pattern Index

To board quickly at a stop, the query must avoid scanning all patterns.

Arrays:

```text
stop_pattern_offsets[stop_count + 1] uint32
stop_pattern_refs[]                 uint32 pattern_id
```

For stop `s`, candidate patterns are:

```text
stop_pattern_refs[stop_pattern_offsets[s] .. stop_pattern_offsets[s + 1])
```

This is the transit equivalent of CSR adjacency.

## Calendar And Service Days

GTFS calendars and exceptions are resolved at build time into compact service-day validity tables.

Service mask record size: 16 bytes.

```text
offset size field
0      4    first_day_index
4      4    day_count
8      4    bitset_offset_u32
12     4    flags
```

Bitset layout:

```text
service_active_bits[] uint64 words
```

For a query date, compute `day_index` relative to the pack epoch, then test whether a trip's `service_mask_id` is active. A second inverted index may be added later:

```text
date_active_service_offsets[day_count + 1]
date_active_service_ids[]
```

The inverted index can make date initialization faster when the service count is large.

## Query Algorithm

### Walking-Only Query

```text
resolve origin address/coordinate
resolve destination address/coordinate
snap origin to tile edge
snap destination to tile edge

if same tile:
    run A* in tile
    if path found and accepted: return

run A* over expanded tile neighborhood
if path found and accepted: return

if overlay exists:
    route origin -> origin portals
    route overlay portals
    route destination portals -> destination
    stitch path
```

The default heuristic for A* is straight-line walking time from node coordinate to target coordinate.

### Transit Query

```text
resolve and snap origin/destination
walk from origin to nearby stops
initialize earliest arrival per stop

for each RAPTOR round:
    collect route patterns serving updated stops
    scan trips active on query date
    board earliest feasible trip after current stop arrival
    update downstream stops
    apply transfer/walk links if enabled

walk from candidate alighting stops to destination
return best itinerary compared with walking-only route
```

Version 1 may support only one transit leg. The file layout is already compatible with multi-round RAPTOR transfers.

## Journey Reconstruction

Routing arrays must be sufficient to reconstruct a user-facing itinerary without reading OSM PBF or GTFS CSV files.

The query engine keeps predecessor state outside the mapped file:

```text
walking predecessor: previous local node, tile id, edge index, temporary endpoint marker
transit predecessor: previous stop, pattern id, trip id, board stop position, alight stop position
transfer predecessor: from stop, to stop, walk link or temporary walking path reference
```

The file provides stable identifiers and names needed to turn that predecessor state into directions:

- edge `name_string_id` for street/path names
- optional tile shape geometry for drawing and more precise turn text
- stop names and GTFS stop IDs
- route short/long names and mode
- trip ID and headsign
- pattern stop positions so board/alight stops can be named without scanning the trip
- arrival and departure seconds for the selected trip events

Plain-language output should be reconstructable as:

```text
walk origin -> stop access node/stop
board route R, trip T, at stop S, departure time D
alight at stop U, arrival time A
walk stop U -> destination
```

For walking-only routes, the runtime stitches tile-local edge predecessors and optional shape polylines. For transit routes, walking access and egress are reconstructed from walking predecessors, while the transit leg is reconstructed from pattern, trip, event, and string-table records.

## Acceleration Sections

Optional acceleration sections can be added without changing the base graph:

```text
0x0600 tile landmark distances
0x0601 inter-tile portal overlay graph
0x0602 contraction hierarchy shortcuts
0x0603 transit transfer cache
0x0604 popular origin/destination cache
```

Recommended progression:

1. Tiled CSR + A*.
2. 3x3 or corridor tile-neighborhood search.
3. Landmark distances for walking.
4. Portal overlay for long walking routes.
5. Multi-round RAPTOR for transit transfers.
6. Contraction hierarchy only if walking queries still need it.

## Builder Responsibilities

The pack builder should:

- parse OSM PBF once
- select walkable ways using profile rules
- split ways at intersections and tile boundaries
- assign nodes and directed edges to tiles
- create portal nodes/links where edges cross tile boundaries
- build per-tile CSR arrays
- build per-tile snap grids
- normalize and sort address records
- parse GTFS once
- resolve calendars and exceptions into service masks
- group trips into route patterns
- write stop-to-pattern indexes
- precompute stop-to-walk links
- write all hot arrays aligned and uncompressed

For reproducibility, metadata should include source file names, source file sizes, timestamps, and content hashes when available.

## Deterministic Parallel Build Strategy

The format is designed so builders can parallelize by tile and by transit table without changing query semantics. A recommended build pipeline is:

```text
1. stream OSM PBF and collect only routing-relevant nodes, ways, relations, and addresses
2. partition walkable geometry and addresses by metric tile
3. build tile-local graph, snap, address, stop-link, and portal payloads in worker threads
4. parse GTFS stops/routes/trips/calendars in parallel-friendly staging tables
5. group GTFS trips into route patterns and service masks
6. merge worker-local dictionaries into global dictionaries
7. assign final string IDs, stop IDs, route IDs, pattern IDs, trip IDs, and tile payload offsets
8. patch records with final IDs/offsets and write aligned sections
```

To make builds reproducible, the final file must not depend on thread scheduling. The builder should apply stable sort and merge rules:

- tile directory records sorted by `tile_id`
- tile-local nodes sorted by a deterministic local key, such as `(metric_y_m, metric_x_m, source_rank, source_osm_node_id)` before dense ID assignment
- directed edges sorted by `(from_local_node, to_local_node, name_string_id, flags, distance_m)`
- snap grid cells sorted by cell index, with candidate edges sorted by edge index
- address records sorted by `(city_string_id, street_string_id, house_string_id, source_rank)`
- strings sorted by normalized UTF-8 bytes before assigning global string IDs, unless an explicit deterministic dictionary order is used
- routes sorted by stable GTFS route ID, patterns by `(route_id, stop_sequence_hash)`, trips by `(pattern_id, service_mask_id, first_departure, trip_string_id)`
- service masks sorted by bitset contents, then by first active day

Worker threads should use local temporary dictionaries and staging arrays. The global string table, service-mask table, and route-pattern table are merged in deterministic single-purpose phases. This avoids lock contention and prevents file differences caused by insertion order races.

Tile-boundary handling should also be deterministic. Ways crossing a tile boundary are split at the boundary. Each side receives tile-local directed edges, and matching portal records are created from sorted `(tile_id, neighbor_tile_id, local_node_key)` pairs.

The writer can run in parallel while preparing payload bytes, but final file offsets are assigned by a single ordered layout pass. After offsets are known, workers may patch offset fields in their own payload buffers before the file is written.

## Runtime Responsibilities

The query tool should:

- mmap the file read-only
- validate header, endian marker, sizes, and required sections
- avoid copying hot arrays
- only touch tiles needed for the current query
- use `--no-color` or equivalent when emitting machine-readable output
- keep temporary query nodes and endpoint edge splits outside the mapped file
- cache recently used tile payload pointers

The mapped route pack is immutable and safe to share across query threads. Each concurrent query must keep its own heap/priority queue, RAPTOR labels, temporary endpoint nodes, predecessor arrays, output buffers, and color/formatting state. Shared runtime caches, such as decoded optional compressed metadata or recently used tile pointers, need normal synchronization or thread-local storage.

## Compactness Strategy

Keep these uncompressed because they are hot in the search loop:

- tile node records
- node edge offsets
- directed walking edges
- snap grid offsets and edge refs
- stop-to-pattern offsets and refs
- pattern stop IDs
- trip records
- event records
- service bitsets

These can be compressed or stored more densely later:

- string table
- source OSM/GTFS IDs
- detailed edge shapes
- debug/source maps
- metadata
- alternate language names

The first version should prefer one extra byte per hot record over a decode branch in the routing loop.

## Suggested Tools

Builder:

```sh
pbf-to-rte [--tile-size-m 4000] [--threads N] FILE.osm.pbf GTFS_DIR OUT.rte
```

Inspector:

```sh
rte-info OUT.rte
```

Router:

```sh
rte-route OUT.rte "Friedrich-Engels-Straße 22" "Hermann-Mattern-Promenade 25" --depart 2026-05-27T11:00
```

## Version 1 Minimum Viable Pack

The smallest useful `OSMRTE01` implementation should include:

- header and section directory
- tile directory
- metric tile coordinate metadata
- global string table
- tiled walking graph CSR arrays
rte-route OUT.rte "Friedrich-Engels-Straße 22" "Hermann-Mattern-Promenade 25" --depart 2026-05-27T11:00
- tiled address records
- global transit stops
- stop-to-walk links
- route patterns
- trips and events
- service-day bitsets
- stop-to-pattern index
- enough names and IDs to reconstruct plain-language walking and transit directions

That is enough to eliminate the current per-query PBF scan, graph rebuild, GTFS CSV parse, and full `stop_times.txt` scan. Those are the main causes of minute-scale route query times in the current prototype.

The version 1 builder may write zero/default values for these fields when the data is not available or not needed by the selected profile:

- elevation
- surface class
- grade class
- wheelchair-specific flags
- detailed shape geometry
- source OSM/GTFS ID maps
- compressed cold metadata
- fuzzy/prefix address indexes
- stop transfer caches
- portal overlays
- landmark distances
- contraction hierarchy shortcuts

The version 1 runtime must be able to route with only the required MVP sections. Optional acceleration sections may improve speed or output quality, but their absence must not make a valid MVP pack unusable.