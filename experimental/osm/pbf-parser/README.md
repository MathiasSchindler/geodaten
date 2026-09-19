# pbf-parser

This project contains small freestanding C tools for reading and processing OpenStreetMap PBF files. The current focus is on inspecting PBF metadata, streaming OSM entities, running lookups, building render packs for map output, and building route packs for routing.

The tools are written for static Linux x86_64 builds without the standard C library. They use a small local runtime, direct platform syscall wrappers, and project-local parsing and compression code instead of external libraries.

## Current Status

The active render-pack path is the `OSMRPK02` version 2 pipeline:

```sh
make
./build/freestanding-linux-x86_64/pbf-to-rpack data/brandenburg-260524.osm.pbf data/brandenburg.rpack
./build/freestanding-linux-x86_64/rpack-info data/brandenburg.rpack
./build/freestanding-linux-x86_64/rpack-render data/brandenburg.rpack build/potsdam.png --city Potsdam
```

The authoritative implemented render-pack spec is `docs/osmrpack_format.md`. The active builder is `src/tools/pbf_to_rpack.c`, the active renderer is `src/tools/rpack_render.c`, and `rpack-info` understands the same v2 header.

Directory status:

- `src/` and `docs/` contain the active tools and implemented format documentation.
- `actual/` contains future stable-format design notes, not the format currently emitted by the tools.
- `browser/` is a browser prototype that consumes packs produced by the active top-level tools.

## Scope

The repository currently provides:

- `pbf-info`, which reads OSM PBF fileblocks and reports summary counts and metadata.
- `osm-lookup`, which streams nodes, ways, and relations and filters them by type, tags, IDs, names, and bounding boxes.
- `osm-addresses`, which extracts address tags into TSV rows containing state, city, suburb, street, house number, and postcode fields.
- `osm-buildings`, which extracts building/address candidates into TSV rows with address fields, building tags, levels/flats hints, and centroid/bbox coordinates.
- `osm-postal`, which extracts German `boundary=postal_code` relations into a self-contained GeoJSON `FeatureCollection` of PLZ polygons suitable for point-in-polygon assignment.
- `osm-postal-fixture`, which writes the small synthetic `.osm.pbf` used by the `osm-postal` regression test.
- `pbf-to-rpack`, which builds `OSMRPK02` render-pack files from `.osm.pbf` extracts for faster repeated city/bbox rendering.
- `rpack-info`, which prints render-pack header and directory metadata.
- `rpack-render`, which renders PNGs from `.rpack` files using `--city` or `--bbox` without scanning the source PBF.
- `pbf-to-rte`, an `OSMRTE01` route-pack converter that writes the binary container, source counts, metric tile metadata, bounds, sorted tile directory records, address records, and tiled walking graph payloads.
- `rte-info`, which inspects `OSMRTE01` route-pack files, prints header and section metadata, checks whether address and GTFS sections are present, and can look up the route tile for a latitude/longitude pair.
- `rte-route`, which resolves two walking-route endpoint addresses from an `OSMRTE01` route pack, loads the endpoint tile neighborhood, snaps to walking graph nodes, and runs Dijkstra from the `.rte` file.
- `test-thread`, a small validation tool for the Linux nolibc threading layer.
- `test-font`, a small smoke tool for the vendored TrueType renderer used by future map labels.

The project is not intended to be a complete GIS engine. It is a compact parser and tool collection for exploring OSM PBF data with minimal runtime dependencies.

## Architecture

The code is split into platform, runtime, shared parser, and tool layers:

- `src/arch` contains architecture-specific process startup and syscall entry code.
- `src/platform` contains operating-system wrappers for files, I/O, memory mapping, threads, time, identity, and process operations.
- `src/shared/runtime` contains the small runtime functions used instead of libc.
- `src/shared/compression` contains the local zlib/deflate implementation used for PBF blobs.
- `src/shared/pbf.c` and `src/shared/pbf.h` contain the OSM PBF and protobuf streaming parser.
- `src/shared/osm_index.c` and `src/shared/osm_index.h` contain legacy index readers used by older lookup/extraction helpers.
- `src/shared/fontrender` contains the vendored freestanding TrueType parser/rasterizer core from `~/fontrender`.
- `src/shared/fontrender_runtime.c` installs this project's memory, file, and logging hooks for the font renderer.
- `src/tools` contains the command-line tools built on top of the shared layers.

The parser reads PBF fileblocks, decodes `OSMHeader` and `OSMData` blobs, inflates zlib-compressed payloads, parses protobuf fields, and exposes decoded OSM entities through streaming callbacks. Consumers can skip node tags or prefilter way and relation tags when they only need coordinates, IDs, renderable ways, or tag-only records.

## Build

The default build uses `gcc-16` and writes static binaries to `build/freestanding-linux-x86_64/`:

```sh
make all
```

The build output and local PBF data files are intentionally ignored by git.

`make clean` removes the whole build directory. Recreate the binaries with `make`, then recreate any needed `.rpack` or `.rte` files.

On native Windows ARM64 with LLVM-MinGW, build all tools as freestanding PE
binaries without a C runtime dependency:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-windows-arm64.ps1
```

Pass `-Test` to run the synthetic postal regression test after building. The
outputs are written to `build/freestanding-windows-arm64/`.

## Data

Place local `.osm.pbf` files under `data/` when running the tools. Example:

```sh
./build/freestanding-linux-x86_64/pbf-info data/example.osm.pbf
```

Residential building/address candidates can be exported as TSV for a city-sized bbox. Bbox arguments use the same `MINLON,MINLAT,MAXLON,MAXLAT` order as `osm-lookup`.

```sh
./build/freestanding-linux-x86_64/osm-buildings data/germany-260524.osm.pbf potsdam_buildings.tsv \
	--bbox 12.85,52.30,13.25,52.55
```

For map rendering, build a render pack once and render from it:

```sh
./build/freestanding-linux-x86_64/pbf-to-rpack --tile-zoom 10 --threads 8 data/germany-260524.osm.pbf build/germany.rpack
./build/freestanding-linux-x86_64/rpack-render build/germany.rpack city.png --city Berlin --width 1600 --height 1200 --profile
```

`rpack-render` renders directly from the pack-contained place directory, tile payloads, and embedded per-place boundary payloads. It loads `styles/osmrender-default.conf` when present, and `--style FILE` can override the map colors and stroke widths for one render. When no dimensions are supplied, or when only `--width` or `--height` is supplied, it derives the missing image size from the resolved map aspect ratio. For `--city`, it draws the matching administrative boundary and fades pixels outside it from the `.rpack` data; distant exclave components are excluded from the default viewport and can be shown with `--exclave-insets`. A route overlay can be drawn with `--route-polyline FILE`, where the file contains one `lon,lat` point per line. The pack format is documented in `docs/osmrpack_format.md`.

## Routing

The route-pack converter builds the query-time routing cache from a PBF extract:

```sh
make -B build/freestanding-macos-arm64/pbf-to-rte
build/freestanding-macos-arm64/pbf-to-rte --tile-size-m 4000 --threads 2 \
	data/brandenburg-260525.osm.pbf build/brandenburg.rte
```

Add `--gtfs DIR` to embed public-transport stops, routes, service calendars, trips, and stop-time events into the same `OSMRTE01` file:

```sh
build/freestanding-macos-arm64/pbf-to-rte --tile-size-m 4000 --threads 2 \
	--gtfs data/GTFS \
	data/brandenburg-260525.osm.pbf build/brandenburg-gtfs.rte
```

This milestone writes an `OSMRTE01` file with real metric route tiles, tiled walking nodes, edge-offset arrays, directed walking edges, minimal snap-grid records, a first global address section for exact street/house lookup, and an optional embedded GTFS transit payload. Optimized route-pattern indexes and address dictionaries are still future work. The target format is documented in `docs/OSMRTE01.md`.

Inspect a generated route pack:

```sh
make -B build/freestanding-macos-arm64/rte-info
build/freestanding-macos-arm64/rte-info build/brandenburg-tiles.rte --sections
build/freestanding-macos-arm64/rte-info build/brandenburg-tiles.rte \
	--address "Friedrich-Engels-Straße 22"
build/freestanding-macos-arm64/rte-info build/brandenburg-tiles.rte \
	--address "Friedrich-Ebert-Straße 4, Potsdam"
build/freestanding-macos-arm64/rte-info build/brandenburg-tiles.rte \
	--tile 52.3906,13.0645
```

Packs built with `--gtfs` should report `addresses_present: yes` and `gtfs_present: yes`. Address lookup does not require a tile coordinate; `--tile` is only a low-level tile-directory probe. Address queries use `street house` and may add `, city/suburb/postcode` to disambiguate. The inspector folds `Straße` and `Strasse` together for lookup.

The first route-pack walking CLI resolves endpoints from `.rte` data:

```sh
make -B build/freestanding-macos-arm64/rte-route
build/freestanding-macos-arm64/rte-route data/brandenburg.rte \
	"Friedrich-Engels-Straße 22, Potsdam" \
	"Friedrich-Ebert-Straße 4, Potsdam"
```

When the pack was built with GTFS, `rte-route` also evaluates public transport with walking access, walking transfers between nearby stops, and multiple scheduled vehicle legs. If neither `--depart` nor `--arrive` is provided, the query uses the current time as `--depart now`:

```sh
build/freestanding-macos-arm64/rte-route build/brandenburg-gtfs.rte \
	"Friedrich-Engels-Straße 22, Potsdam" \
	"Friedrich-Ebert-Straße 4, Potsdam" \
	--depart 2026-05-27T11:00
```

Pass `--transit` to print only the public-transport itinerary and skip loading or printing the full walking route. Human output keeps successful diagnostic key/value fields such as resolved address metadata and tile/graph counters behind `--verbose`; JSON output is unchanged. The transit suggestion reports the access walk, each vehicle leg with mode and line, transfer walks, the final walk, and total time. Departure-time queries use multi-leg scheduled routing; arrive-by queries keep the compact single-leg fallback until reverse transfer routing is added.

For the Potsdam sample above, the current route pack resolves node and building-way addresses, loads a small tile neighborhood, and reports `route_status: found`. `rte-route` keeps the machine-readable key/value fields and also prints a human-readable walking summary with ANSI colors by default. Use `--no-color` when capturing output for scripts or logs.

Use `--json` to emit newline-delimited JSON events instead of the human/key-value text output. JSON mode disables terminal color and follows the `newos.tool.v1` envelope documented in `docs/json-output.md`. Successful walking routes stream `metadata`, `address`, `tile_context`, `graph_loaded`, `route_status`, `route`, `route_point`, and `route_step` events; GTFS-enabled packs also emit a `transit_plan` event, using the current departure time unless `--depart` or `--arrive` is supplied. With `--transit --json`, the stream skips the walking graph events and emits only endpoint metadata/address events plus `transit_plan`. Each route point includes latitude/longitude and cumulative distance, while each step includes action, distance, direction where available, and start/end coordinates. Diagnostics are written as JSON events on stderr.

```sh
build/freestanding-macos-arm64/rte-route data/brandenburg.rte \
	"Friedrich-Engels-Straße 22, Potsdam" \
	"Friedrich-Ebert-Straße 4, Potsdam" \
	--json
```

The macOS route CLI can also render a PNG map for a successful route:

```sh
build/freestanding-macos-arm64/rte-route data/brandenburg.rte \
	"Friedrich-Engels-Straße 22, Potsdam" \
	"Friedrich-Ebert-Straße 4, Potsdam" \
	--map build/route.png
```

`--map` writes the route path to a temporary `lon,lat` polyline and invokes the sibling `rpack-render` binary with a render pack. Pass `--rpack FILE.rpack` to choose the map layer explicitly; otherwise the tool first looks for a matching `.rpack` next to the `.rte`, then for known local packs such as `build/brandenburg-260525.rpack` and `data/germany.rpack`. Use `--width N` and/or `--height N` to control map dimensions; if only one dimension is provided, the renderer derives the other from the selected viewport. Cross-city bbox maps default to `--width 1600` and derive height from the route bbox. When both endpoints are in the same city, the map uses the renderer's `--city` viewport and still prints the computed route bbox as `map_bbox`.

The human-readable directions are currently coarse graph directions such as “continue generally north-west for 450 m”; edge street names and fuller snap-grid edge snapping remain future work. The builder currently duplicates cross-tile walking segments into both endpoint tiles and writes minimal snap-grid headers; endpoint snapping in `rte-route` uses nearest graph nodes.

## Fonts

The project vendors the freestanding TrueType backend from `~/fontrender` under `src/shared/fontrender`. The core remains dependency-free and is connected to this runtime by `fontrender_runtime_install()`. Full map labels are still future work.

A smoke test can load a `.ttf` and rasterize one glyph:

```sh
./build/freestanding-linux-x86_64/test-font /path/to/font.ttf A 32
```

It prints glyph metrics and the number of non-empty bitmap pixels.

## Postal Code Polygons

`osm-postal` turns the German `boundary=postal_code` relations of an `.osm.pbf`
extract into one self-contained GeoJSON `FeatureCollection` that can be used for
point-in-polygon PLZ assignment without any further data source.

```sh
./build/freestanding-linux-x86_64/osm-postal data/germany-260524.osm.pbf build/plz.geojson \
	--report build/plz_report.tsv
```

On macOS the same tool is built with `make macos-postal-tools` into
`build/freestanding-macos-arm64/`.

### How it works

The tool makes three streaming passes over the file, because relation members
reference ways and ways reference nodes:

1. Relations. Every relation tagged `boundary=postal_code` is captured together
   with its tags and its `outer`/`inner` way members. This pass determines the
   set of way IDs that are needed.
2. Ways. Only the ways referenced by a captured relation are kept, and only
   their node reference lists. This pass determines the set of node IDs that
   are needed.
3. Nodes. Only the referenced nodes are kept, as a longitude/latitude pair in
   fixed-point 1e-7 degree units.

Referenced IDs are stored in sorted, deduplicated arrays. Lookups use a
monotonic cursor as long as the file delivers IDs in ascending order (which
normal planet extracts do) and fall back to binary search otherwise, so no
hashing and no unbounded memory growth is required.

All geometry math is done in integer 1e-7 degree units with `__int128`
intermediates. No floating point is used anywhere, so the output is bit-for-bit
reproducible for a given input file.

### Ring assembly

Member ways are fragments, and OSM guarantees nothing about their order or
direction. For each role (`outer` and `inner` separately) the tool builds a
table of the two endpoints of every open fragment, sorts it by node ID, and then
grows a ring by repeatedly looking up the fragment that shares the current open
end, reversing that fragment when it is attached by its last node. Already
closed ways become rings directly. A member with an empty role is treated as
`outer`, following the usual OSM multipolygon convention.

A ring is only accepted when it closes, has at least four positions, and encloses
a non-zero area. Outer rings are then forced to counter-clockwise and inner rings
to clockwise winding, as required by RFC 7946.

Inner rings are assigned to the smallest outer ring that contains them, using an
exact integer crossing-number test on three sample vertices. A relation whose
inner ring cannot be placed is reported as broken rather than emitted.

### Which relations count as German

Foreign postal areas cross the border into planet extracts, and the tool decides
using OSM tags and geometry only, never an external boundary file:

- A relation carrying `postal_code:DE` is accepted on that tag alone. This is the
  explicit, unambiguous signal and it also selects the postcode value.
- Otherwise the relation is rejected when it carries a tag that marks it as
  belonging to another country: `addr:country`, `country`, `is_in:country_code`,
  `ISO3166-1` or `ISO3166-1:alpha2` with a value other than `DE`, `ISO3166-2`
  that does not start with `DE-`, any `postal_code:XX` for a country other than
  `DE`, or one of the national reference schemes `ref:INSEE`, `ref:ISTAT`,
  `ref:BAG` and `ref:NUTS`.
- Otherwise the relation must lie entirely inside the acceptance box
  `5.5,47.0,15.5,55.2`, which is the bounding box of Germany. Because the test
  requires *full* containment, a foreign area that merely touches the box is
  still rejected. `--bbox MINLON,MINLAT,MAXLON,MAXLAT` overrides the box and
  `--no-bbox-filter` disables this stage.

`--require-de-tag` reduces the rule to the first bullet alone.

The postcode is taken from `postal_code:DE`, else `postal_code`, else `ref`, and
must be exactly five digits that do not start with `00`.

### Output

The output is a standards-compliant GeoJSON `FeatureCollection` with a `bbox`.
Each feature is a `Polygon` when the relation has exactly one outer ring and a
`MultiPolygon` otherwise, and carries the relation ID plus audit properties:

```json
{"postcode":"10115","postcode_source":"postal_code:DE","relation_id":100,
 "name":"10115 Berlin Mitte","outer_rings":1,"inner_rings":1,
 "coordinate_count":10,"member_ways":6,"member_nodes":0,"member_relations":0,
 "has_postal_code_de_tag":true}
```

Features are sorted by `(postcode, relation_id)`, so the file is deterministic
and diffable. Writing to `-` sends the GeoJSON to stdout and the statistics to
stderr.

Nothing is emitted for a relation whose geometry could not be reconstructed.
Such a relation is put in one of two classes, which is visible in the `status`
column of the `--report` TSV and in the summary counters:

- `incomplete` — the relation references a way or node that is not in the file.
  National extracts are clipped at the border, so postal areas of neighbouring
  countries that reach into the extract routinely land here, and so do German
  areas that extend past the clip. This is a coverage fact, not a data error, so
  it is counted as `relations_incomplete` and does not fail the run unless
  `--strict-incomplete` is given. Details: `missing_way_member`, `missing_node`.
- `broken` — every referenced member was present, yet the rings could not be
  built. This is real broken data or an unsupported construct. It is counted as
  `relations_broken` and `--strict` turns it into exit code 2. Details:
  `unclosed_ring`, `degenerate_ring`, `inner_ring_without_outer_ring`,
  `no_outer_ring`, `no_way_members`, `way_with_less_than_two_nodes`,
  `unsupported_relation_member`, `out_of_memory`.

Policy exclusions (`no_postal_code_tag`, `invalid_postal_code`,
`foreign_country_tag`, `outside_germany`, `not_postal_code_de_tagged`) are
reported as `excluded` and never fail the run.

### Test

```sh
make macos-test-postal   # or: make test-postal
```

`osm-postal-fixture` writes a small synthetic `.osm.pbf` with uncompressed
blobs, containing eight `boundary=postal_code` relations that cover an outer ring
listed out of order with a reversed fragment, an inner ring built from two
fragments, the `postal_code`/`ref` fallbacks, an unclosable ring, an invalid
four-digit code, a relation outside the German box, a relation inside the box
that carries a foreign reference tag, and a relation referencing a way that is
not in the file. The harness compares the GeoJSON, the audit report, and the
statistics against golden files in `tests/expected/`, checks that two runs are
byte identical, and checks the `--strict`, `--strict-incomplete`,
`--no-bbox-filter`, and `--require-de-tag` behaviour.

### Validation against the real extract

A run over a 5.4 GB Germany extract (2026-08-30) takes about three minutes,
peaks at roughly 140 MB resident, resolves 65699 member ways and 3.55 million
member nodes, and produces 8175 features with 8712 outer and 144 inner rings.
The 76 excluded relations are Austrian areas with four-digit codes, and the 44
incomplete ones are French and Austrian areas clipped at the border. No relation
fails with a geometry error, so `--strict` exits 0.

## libreHKDE Export

`libre-hkde` converts the normalized address CSV and the aligned VG250
municipality assignment into the project's explicitly incomplete,
HK-DE-5.2-structured compatibility profile. It is freestanding C and has no
libc or external runtime dependency.

```sh
make macos-libre-hkde-tools  # or: make libre-hkde-tools
build/freestanding-macos-arm64/libre-hkde \
  adressen.csv basemap-gemeinden.csv libreHKDE.csv export-statistik.txt
```

Use `-` for the address input or output to stream through `gzip`. The tool:

- leaves `oid` and `qua` empty;
- adds only strict VG250 municipality matches;
- parses documented administrative key prefixes when available;
- writes documented zero keys for unavailable keys;
- transforms WGS84 to EPSG:25832 without a math library;
- separates numeric house numbers and suffixes;
- emits UTF-8, CRLF, exactly 24 semicolon-separated fields.

Run `make macos-test-libre-hkde` or `make test-libre-hkde` for the fixture test.
The profile limitations and full-data result are documented in
`../../ZSHH/MACHBARKEIT-LIBREHKDE.md`.

## Documentation

Additional notes are in:

- `docs/threading.md`
- `docs/osm_streaming.md`
- `docs/osmrpack_format.md`
- `docs/OSMRTE01.md`

## Generation And License

The source code of this project is LLM-generated using GPT 5.5.

The project license is CC-0.