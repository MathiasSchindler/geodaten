# OSMRPACK Version 2 Format

`OSMRPACK` is a render-oriented cache for city and bbox rendering from large OSM PBF extracts. The current on-disk format is version 2 with magic `OSMRPK02`.

An `.rpack` file contains:

- a fixed-size header
- a place directory for name-to-bbox lookup
- a tile directory for spatial payload lookup
- one feature payload per non-empty tile
- per-place boundary payloads for city outline/fade rendering
- a UTF-8 string table for place names

All map feature geometry used by the pack renderer is stored in the `.rpack` file, including per-place administrative boundary geometry for `--city` outline and outside-boundary fade.

## Tools

Build the tools:

```sh
make
```

Build a version 2 pack:

```sh
./build/freestanding-linux-x86_64/pbf-to-rpack [--tile-zoom N] [--tile-halo N] [--threads N] [--way-threads N] [--buildings] FILE.osm.pbf OUT.rpack
```

Inspect a pack header:

```sh
./build/freestanding-linux-x86_64/rpack-info OUT.rpack
```

Render from a pack:

```sh
./build/freestanding-linux-x86_64/rpack-render OUT.rpack OUT.png (--bbox MINLON,MINLAT,MAXLON,MAXLAT | --city NAME) [--width N] [--height N] [--style FILE] [--route-polyline FILE] [--exclave-insets] [--no-boundary-fade] [--png-rgb] [--profile]
```

If neither `--width` nor `--height` is supplied, the renderer uses its default width and derives height from the resolved bbox aspect ratio. If only `--width` or only `--height` is supplied, the renderer derives the missing dimension from that same aspect ratio. Supplying both dimensions keeps the exact requested image size, even when that stretches the map.

By default, `rpack-render` loads `styles/osmrender-default.conf` when that file exists. Pass `--style FILE` to use another render style. Style keys are `background`, plus `STYLE.line`, `STYLE.fill`, `STYLE.casing`, `STYLE.width`, and `STYLE.casing_width` for the style names listed below.

`--route-polyline FILE` draws a route overlay after the normal map and optional GTFS layers. The file is a plain text polyline with one `lon,lat` WGS84 decimal-degree point per line. The overlay is rendered as a single red route stroke.

For city renders with far-away boundary components, the renderer uses the primary boundary component as the default viewport. `--exclave-insets` renders those distant components in a corner inset without changing the `.rpack` format.

## Encoding Rules

- All integer fields are little-endian.
- Offsets are absolute byte offsets from the start of the file.
- Coordinates are signed 64-bit WGS84 nanodegrees.
- Strings are UTF-8 byte strings stored without required NUL terminators.
- Fixed records contain zero-filled reserved fields.
- The current renderer rejects version 2 files whose header, place, tile, or feature record sizes do not match the constants in this document.

## File Layout

```text
OSMRPK02 file
  header             256 bytes
  place directory    place_count * 112 bytes
  tile directory     tile_count * 96 bytes
  tile payloads      variable, one payload per non-empty tile
  boundary payloads  variable, one payload per place with resolved boundary ways
  string table       variable, UTF-8 place names
```

The current writer sets `tile_range_index_offset` and `tile_range_index_size` to zero. Boundary payloads are reached from place records rather than from a top-level directory.

## Header

Header size: 256 bytes.

```text
offset size field
0      8    magic = "OSMRPK02"
8      4    version = 2
12     4    header_size = 256
16     4    flags = 0
20     4    tile_zoom
24     4    tile_halo
28     4    layer_count = 12
32     4    place_record_size = 112
36     4    tile_record_size = 96
40     4    feature_record_size = 48
44     4    reserved = 0
48     8    place_count
56     8    tile_count
64     8    place_directory_offset
72     8    place_directory_size
80     8    tile_directory_offset
88     8    tile_directory_size
96     8    tile_range_index_offset = 0
104    8    tile_range_index_size = 0
112    8    tile_payload_offset
120    8    tile_payload_size
128    8    string_table_offset
136    8    string_table_size
144    8    source_nodes
152    8    source_ways
160    8    source_relations
168    88   reserved = 0
```

`tile_zoom` controls the geographic tile grid. `tile_halo` is the number of neighboring tile columns/rows the renderer includes around the bbox-derived tile range. The default builder value is `1`; the builder accepts values from `0` through `8`.

## Tile Coordinate System

Version 2 uses a simple geographic grid, not Web Mercator.

```text
axis = 1 << tile_zoom
x = floor((lon_nano + 180000000000) * axis / 360000000000)
y = floor(( 90000000000 - lat_nano) * axis / 180000000000)
```

Inputs are clamped to the valid world extent before conversion. The resulting `x` and `y` values are clamped to `0..axis-1`.

Tile extents are derived with integer division:

```text
min_lon = floor(x       * 360000000000 / axis) - 180000000000
max_lon = floor((x + 1) * 360000000000 / axis) - 180000000000
max_lat =  90000000000 - floor(y       * 180000000000 / axis)
min_lat =  90000000000 - floor((y + 1) * 180000000000 / axis)
```

Tile IDs are sortable unsigned 64-bit values:

```text
tile_id = (z << 58) | (x << 29) | y
```

The renderer accepts `tile_zoom <= 29`. The builder uses the shared maximum from `osmrpack.h`.

## Place Directory

The place directory contains one fixed-size record for each relation-derived place whose bbox can be resolved during pack construction. The renderer scans this directory for exact UTF-8 name matches when `--city NAME` is used.

Record size: 112 bytes.

```text
offset size field
0      8    source_id              signed OSM id
8      4    source_type            1 = relation
12     4    place_kind
16     4    admin_level            0 when absent or not parsed
20     4    rank_score             larger wins for default name matches
24     8    min_lon_nano
32     8    min_lat_nano
40     8    max_lon_nano
48     8    max_lat_nano
56     8    name_offset            absolute offset into string table
64     4    name_size              byte length
68     8    boundary_payload_offset absolute offset, 0 when absent
76     8    boundary_payload_size   bytes, 0 when absent
84     4    boundary_feature_count
88     4    reserved = 0
92     4    flags
96     16   reserved = 0
```

Current `place_kind` values:

```text
0 unknown
1 city
2 town
3 village
4 suburb
5 district
6 state
7 country
```

The builder derives places from relation tags. A relation is accepted when it has a `name` tag and either:

- `boundary=administrative` with `type` absent, `type=boundary`, or `type=multipolygon`
- a supported `place=*` value

The builder stores `source_type = 1` for these records. It stores `admin_level` when the tag parses as an unsigned integer.

`rank_score` is computed from place kind, admin level, and relation way-member count:

```text
city      +100000
town      + 90000
state     + 80000
district  + 70000
village   + 60000
suburb    + 50000
country   + 40000
admin_level contribution = (20 - min(admin_level, 20)) * 1000
way member contribution  = min(way_member_count, 999)
```

For `--city`, the renderer compares the requested name to `name_size` bytes at `name_offset`. If several records match, the highest `rank_score` is used. When the selected place bbox is used for rendering, the renderer adds 10% padding per axis with a minimum padding of 0.005 degrees.

## Tile Directory

The tile directory contains one record for each non-empty tile. Records are written in sorted `tile_id` order.

Record size: 96 bytes.

```text
offset size field
0      8    tile_id
8      4    z
12     4    x
16     4    y
20     4    feature_count
24     4    layer_mask
28     4    flags = 0
32     8    payload_offset
40     8    payload_size
48     8    min_lon_nano
56     8    min_lat_nano
64     8    max_lon_nano
72     8    max_lat_nano
80     8    payload_uncompressed_size = 0
88     8    reserved = 0
```

`layer_mask` has bit `style_id` set for every style present in the tile payload. The current renderer scans the tile directory linearly, selects records intersecting the bbox-derived tile range expanded by `tile_halo`, and then reads only the selected payloads.

## Tile Payloads

Each tile payload starts with a little-endian feature count, followed by feature records and coordinate arrays.

```text
u64 feature_count
feature_count repetitions:
  feature header, 48 bytes
  point_count repetitions:
    i64 lon_nano
    i64 lat_nano
```

`feature_count` in the tile payload matches `feature_count` in the corresponding tile directory record.

## Boundary Payloads

Each valid place record can point to a boundary payload containing the relation member ways used to compute that place bbox. The payload format is the same feature stream used by tile payloads, except it is selected by the matched place record instead of by tile range:

```text
offset size field
0      8    feature_count
8      ...  repeated feature records and coordinate arrays
```

Boundary payload feature records use style `11` (`boundary`), line flags `0`, and WGS84 nanodegree point arrays. The renderer loads only the boundary payload for the selected `--city` place, so the outside-boundary fade is based on that city boundary rather than on every administrative line in the visible tile range.

### Feature Header

Feature header size: 48 bytes.

```text
offset size field
0      4    style_id
4      4    flags
8      4    point_count
12     4    reserved = 0
16     8    min_lon_nano
24     8    min_lat_nano
32     8    max_lon_nano
40     8    max_lat_nano
```

Current feature flags:

```text
bit 0: closed area feature, fillable by the renderer
bit 1: coastline feature, used by the renderer to fill open sea/ocean regions
```

The area flag is set only for closed ways in area-capable styles. Coastline features are not marked as closed areas even if a source way is closed.

## Style IDs

The current layer count is 12.

```text
0  water
1  waterway
2  forest
3  park
4  building
5  motorway
6  primary
7  secondary
8  minor_road
9  path
10 rail
11 boundary
```

Current way classification:

```text
natural=coastline                                      -> water with coastline flag
natural=water, waterway=riverbank, landuse=reservoir/basin,
or water=lake/pond/reservoir/basin                        -> water
any other waterway=*                                   -> waterway
landuse=forest/orchard, natural=wood/scrub/tree_row    -> forest
leisure=park/garden/nature_reserve                     -> park
landuse=grass/meadow/recreation_ground/village_green   -> park
natural=grassland/heath                                -> park
building=* with --buildings                            -> building
highway=motorway/trunk                                 -> motorway
highway=primary                                        -> primary
highway=secondary                                      -> secondary
highway=footway/path/cycleway/track                    -> path
any other highway=*                                    -> minor_road
any railway=*                                          -> rail
```

When `--buildings` is not passed to the builder, `building=*` ways are skipped.

## Feature Assignment To Tiles

The builder resolves every classified way to node coordinates, computes its bbox, converts that bbox to a tile range, and writes one copy of the full feature geometry into every intersecting tile payload.

This means a feature crossing tile boundaries appears in multiple tile payloads. The renderer de-duplicates selected feature copies using a content hash while collecting visible features. It also applies the exact render bbox check before storing projected points, so halo tiles do not draw unrelated distant geometry.

## String Table

The string table contains the concatenated UTF-8 names referenced by place records. Place records store absolute file offsets into this section plus byte lengths. The current writer stores only place names in the string table, after any per-place boundary payloads.

## Builder Behavior

`pbf-to-rpack` streams the source PBF in phases:

```text
collect_places
collect_ways
build_node_lookup
collect_nodes
assign_tiles
write_pack
```

The builder counts all source nodes, ways, and relations in the header. It stores relation-derived place records whose member-way bboxes can be resolved, writes those place boundary member ways into per-place boundary payloads, and stores way-derived render features in tile payloads. It does not store relation-derived multipolygon render geometry in the current format.

Command-line options:

```text
--tile-zoom N     set tile zoom
--tile-halo N     set renderer halo, 0..8
--threads N       use N workers for collect_nodes, default 1
--way-threads N   use N workers for collect_ways, default 1
--buildings       include building footprints
```

`--threads` currently applies to the node-coordinate pass. This pass uses the parallel PBF fileblock stream, a shared read-only node hash, and worker-local source-node counters. Workers fill coordinates for matched node IDs and the main thread merges counters after all workers finish.

`--way-threads` exposes the worker-local way collector. Each worker collects features, refs, place boundary ways, and counters into private buffers, then the main thread appends them into the shared build context with adjusted ref offsets. This path is correct, but it is not the default performance recommendation yet because the generic way/tag parser still performs enough allocation-heavy work to lose to the serial pass on the current benchmark host.

The builder prints `phase_elapsed_ms: NAME VALUE` for each phase. `build_elapsed_ms` covers phases through `assign_tiles`; use wall-clock `time` or the `write_pack` phase line when comparing full conversion time.

Brandenburg `data/brandenburg-260524.osm.pbf`, tile zoom 10, measured on 2026-05-25:

| Options | Real | User | collect_ways | collect_nodes | Notes |
| --- | ---: | ---: | ---: | ---: | --- |
| `--threads 1` | 17.97s | 17.57s | 5758 ms | 6419 ms | serial baseline through the parallel API |
| `--threads 8` | 12.38s | 18.53s | 5666 ms | 943 ms | recommended threaded builder path |
| `--threads 8 --way-threads 8` | 21.86s | 133.10s | 15141 ms | 935 ms | correct output, slower due allocation/tag parsing overhead |

All three runs produced matching high-level pack counts: `places_written=2912`, `tiles_written=108`, `tile_feature_copies=1546975`, `classified_ways=1530306`, and `unique_nodes_needed=9412155`.

## Renderer Behavior

`rpack-render` detects `OSMRPK02` before falling back to other pack readers. For version 2 packs it:

1. resolves `--bbox` directly or scans the place directory for `--city NAME`
2. applies bbox padding for place-derived renders
3. converts the render bbox to a tile range
4. expands the range by `tile_halo`
5. scans the tile directory and selects matching tile records
6. reads selected tile payloads
7. de-duplicates copied features from overlapping tile payloads
8. collects features intersecting the render bbox
9. loads the matched place boundary payload, or falls back to sidecar indexes for older packs
10. draws the configured layers, optional GTFS stops, optional route polyline overlay, and writes PNG output

For `--city`, the renderer draws the selected place boundary as style `11` and can fade pixels outside that boundary without reading the source PBF or sidecar indexes. If a pack predates embedded boundary payloads, node, way, and relation index paths can still be supplied or inferred to resolve the same overlay from sidecar indexes.

The PNG writer uses adaptive scanline filters, fixed-Huffman deflate, and an indexed palette by default. `--png-rgb` writes exact RGB pixels instead of indexed-color PNGs.