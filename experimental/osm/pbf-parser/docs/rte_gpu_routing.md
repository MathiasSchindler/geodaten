# RTE GPU Routing Experiment

This document sketches a separate GPU-oriented routing experiment. The existing `rte-route` CPU tool and `OSMRTE01` route-pack format remain the stable path. GPU routing should live behind separate tools and may use CUDA/NVIDIA-specific build rules without changing the freestanding CPU tools.

## Goals

- Keep `rte-route` as the canonical dependency-free CPU router.
- Add a separate experimental `rte-gpu-route` tool for NVIDIA/CUDA machines.
- Allow a GPU-specific pack format that is shaped for bulk parallel scans rather than CPU cache locality.
- Start with Berlin/Brandenburg-scale data from `data/GTFS/` plus `data/brandenburg-260524.osm.pbf`, then validate Germany-wide scale with `data/gtfs-deutschland/` and `data/germany-260524.osm.pbf`.
- Compare every GPU result against `rte-route` for arrival time, number of rides, walking distance, and printed route legs.

## Non-Goals

- Do not make the default build depend on CUDA.
- Do not replace `OSMRTE01` or `rte-route`.
- Do not move address parsing, human-readable output formatting, or route explanation onto the GPU initially.
- Do not require the GPU path to preserve the exact CPU data layout.

## Proposed Tools

```text
pbf-to-rtegpu   OSM PBF + GTFS CSV -> GPU-shaped .rtegpu pack
rte-gpu-info    Inspect .rtegpu headers and array counts
rte-gpu-route   Query .rtegpu with CUDA kernels for transit relaxation
```

A useful bootstrap path is also possible:

```text
rte-to-rtegpu   OSMRTE01 .rte -> .rtegpu pack
```

That converter would let the experiment reuse already verified `.rte` builds while the direct raw-data builder is still evolving. The direct builder should remain the long-term target.

## Why a Different Format

The CPU route pack stores compact records that are convenient to parse and memory-map on the CPU. The GPU wants large, aligned arrays where thousands of threads run the same operation with predictable memory access.

The current CPU transit search already has a promising shape: each round scans all GTFS stop events in trip order and relaxes reachable stops. On the GPU, that can become one block or warp per trip. Each block scans a trip's events sequentially, while millions of trips run in parallel.

## RTEGPU01 Layout

All fields are little-endian. Hot arrays should be aligned to at least 128 bytes. Coordinates use signed `e7` WGS84 integers like `OSMRTE01`.

```text
RTEGPU01 file
  header                         256 bytes
  section directory              fixed records
  stop arrays                    structure-of-arrays
  route arrays                   compact route metadata
  service calendar bitsets       date-indexed active flags
  trip arrays                    one record per trip, with event range
  event arrays                   structure-of-arrays, grouped by trip
  transfer graph                 CSR stop-to-stop walking transfers
  address/search data            CPU-readable, optional at first
  string table                   route/stop labels for output
```

### Header

```text
offset size field
0      8    magic = "RTEGPU01"
8      4    version = 1
12     4    header_size = 256
16     4    endian_marker = 0x01020304
20     4    flags
24     8    file_size
32     8    build_unix_time
40     8    stop_count
48     8    route_count
56     8    service_count
64     8    trip_count
72     8    event_count
80     8    transfer_edge_count
88     8    section_directory_offset
96     4    section_count
100    4    section_record_size
104    4    coord_scale = 10000000
108    4    max_rounds_hint
112    4    first_service_date
116    4    service_date_count
120    136  reserved
```

### Stop Arrays

```text
stop_lat_e7[stop_count]      i32
stop_lon_e7[stop_count]      i32
stop_mode_mask[stop_count]   u32
stop_name_offset[stop_count] u32
stop_name_size[stop_count]   u32
```

The GPU path uses latitude/longitude for access and egress candidate scans. Human-readable names stay CPU-side.

### Trip Arrays

```text
trip_route_index[trip_count]  u32
trip_service_index[trip_count] u32
trip_mode[trip_count]         u32
trip_event_offset[trip_count] u32
trip_event_count[trip_count]  u32
```

The event range replaces the repeated `trip_index` currently stored in every `OSMRTE01` event. This is both smaller and more convenient for one-block-per-trip kernels.

### Event Arrays

```text
event_stop_index[event_count]  u32
event_arrival_sec[event_count] u32
event_departure_sec[event_count] u32
```

Events are grouped by trip and ordered by stop sequence. Sequence does not need to be stored in the hot GPU array if the builder guarantees order.

### Service Calendar

For a fixed feed date range, store a bitset or byte matrix:

```text
service_active[service_date_count][service_count]
```

The CPU query maps a requested date to `service_date_index`; the GPU kernels read `service_active[date][trip_service_index]`.

### Transfer Graph

Transfers should be precomputed by the builder rather than discovered with a spatial hash during every query.

```text
transfer_offset[stop_count + 1] u32
transfer_to[transfer_edge_count] u32
transfer_walk_sec[transfer_edge_count] u16 or u32
```

This is a CSR graph. A transfer relaxation kernel can run one thread per transfer edge or one block per changed stop. For the first prototype, scanning all transfer edges per round is simpler; a later version can maintain a compact changed-stop frontier.

## Query Flow

CPU work:

1. Resolve origin and destination addresses.
2. Parse departure or arrival time.
3. Load route labels and other output metadata.
4. Launch GPU kernels and copy back the best stop/predecessor arrays.
5. Reconstruct and print route legs in the same style as `rte-route`.

GPU work for departure-time routing:

1. Initialize `arrival[stop]` from origin access walks.
2. Build or load `trip_active[trip]` for the query date.
3. For each round:
   - copy `arrival` to `base_arrival`
   - run a trip scan kernel over all active trips
   - relax alighting stops with `atomicMin(arrival[stop], event_arrival_sec)`
   - store predecessor data for winning relaxations
   - run transfer relaxations over the transfer CSR
4. Scan destination egress candidates and select the best finish score.

The trip scan kernel is the core experiment:

```text
one CUDA block or warp per trip
  if service inactive: return
  board_state = none
  for event in trip_event_range:
    if board_state is set:
      relax event.stop at event.arrival_sec
    if base_arrival[event.stop] + boarding_slack <= event.departure_sec:
      board_state = this stop/event
```

This keeps the sequential dependency inside one trip local, while parallelizing across 1.6M Germany-wide trips.
The current prototype uses a 120 second boarding slack so address access walks and stop-to-stop transfers do not create itineraries that depend on sub-minute boardings.

## Expected Memory Shape

Germany-wide GTFS scale from the verified route pack:

```text
stops:        688,005
routes:        24,977
trips:      1,643,599
events:    33,325,850
```

A compact hot GPU layout should keep the main transit arrays well below 2 GB before transfers:

```text
stops:       ~16-32 MB
trips:       ~32-64 MB
events:      ~400-600 MB
arrival/state arrays: ~20-100 MB per query, depending on predecessor detail
service matrix: small for a one-month feed
transfers:   data-dependent, likely the largest optional section
```

This should fit comfortably on a 16 GB GPU if transfer edges are capped or compressed.

## First Milestones

1. Add `rte-to-rtegpu` to convert the verified transit part of an `.rte` file into `RTEGPU01` arrays.
2. Add `rte-gpu-info` to validate the header and section counts.
3. Add `rte-gpu-route --transit-only` for stop-to-stop or coordinate-to-coordinate routing without address lookup.
4. Compare against `rte-route --json` on a fixed benchmark set:
   - Potsdam to Villingendorf
   - Berlin local route
   - rural Brandenburg to Berlin
   - station-to-station long-distance route
5. Add CPU address lookup and output reconstruction once the GPU transit kernel matches CPU results.
6. Add direct `pbf-to-rtegpu` from raw OSM PBF + GTFS CSV once the pack shape is proven.

## Prototype Status

Initial prototype tools are implemented as explicit CUDA targets, outside the default freestanding build:

```sh
make rte-gpu-tools
```

The prototype currently includes:

```text
rte-to-rtegpu   convert an existing OSMRTE01 `.rte` transit section to RTEGPU01
rte-gpu-info    validate and inspect RTEGPU01 counts/sections
rte-gpu-route   CUDA trip scan + transfer relaxation with CPU verification
```

`rte-gpu-route` now supports several query modes:

```text
FILE.rtegpu --from-stop N --to-stop N
FILE.rtegpu --from-latlon LAT,LON --to-latlon LAT,LON
FILE.rtegpu --rte FILE.rte --from ADDRESS --to ADDRESS
FILE.rtegpu --rte FILE.rte --interactive
FILE.rtegpu --rte FILE.rte --tui
FILE.rtegpu --rte FILE.rte --api
```

Address mode resolves addresses from the existing `OSMRTE01` address dictionary on the CPU, then runs the transit arrival search on the GPU-shaped pack. Address resolution scans the fixed address records in parallel by default, capped at 16 worker threads; use `--address-threads N` to override this for benchmarks.

If `--depart` is omitted, the CLI and TUI use the local current date and time as the departure time.

For repeated address queries, build a compact sidecar index once:

```text
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --build-address-index
```

The default sidecar path is `FILE.rte.addridx`; override it with `--address-index FILE`. Normal address queries automatically use the sidecar when it is present and current, then fall back to the threaded full scan if the index is missing, stale, or cannot resolve the query exactly. Text and JSON output report `address_index_status`, `address_index_used`, and the sidecar entry count.

Address matching is intentionally tolerant of common German input variants. The resolver treats umlaut spellings and ASCII spellings as aliases (`Lübeck`/`Luebeck`, `Königstraße`/`Koenigstrasse`), accepts shortened place prefixes such as `Oldenburg` for longer stored place names, matches house-number lists such as `18,20` when the user asks for `18`, and has a relaxed fallback for `place house, larger-place` input when the exact place part does not match the stored administrative fields. Rebuild `.rte.addridx` after this change if the TUI should get the same tolerant aliases without falling back to the full scan:

```text
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --build-address-index
```

`--plan` enables GPU predecessor capture and prints reconstructed route legs. Text plan output uses color by default, matching the CPU tool's opt-out style: route modes/numbers are highlighted in yellow, stop/station names in cyan, and the plan header in magenta. The text and TUI views compact adjacent same-trip ride fragments and chained walking transfers so the displayed itinerary is closer to what a rider expects. Use `--no-color` to suppress ANSI escapes, or `--color` to force them back on.

`--json` emits one structured JSON object instead of text. JSON mode implies `--plan`, disables color, and includes query metadata, counts, best arrival, optional verification details, route legs, stop/request coordinates for each leg, and timing fields. Coordinates are emitted as both integer e7 values and decimal degrees. `--verify` runs the CPU mirror and should be used for validation, but normal route queries skip it to avoid the extra CPU scan.

Verified Germany-wide prototype results on `data/germany-gtfs.rte`:

```text
rte-to-rtegpu data/germany-gtfs.rte data/germany-gtfs.rtegpu
  output: 569MB / 569,595,741 bytes
  stops: 688,005
  services: 5,164
  trips: 1,643,599
  events: 33,325,850
  transfer_edges: 12,477,066
  service_dates: 31

rte-gpu-route data/germany-gtfs.rtegpu --from-stop 109476 --to-stop 304206 --depart 2026-06-03T18:00:00 --iterations 10
  gpu_best_arrival: 18:37:00
  cpu_best_arrival: 18:37:00
  verify_arrival_mismatches: 0
  verify_best_match: yes
  load_ms: ~183
  host_to_device_ms: ~23
  gpu_kernel_avg_ms: ~4.8
  cpu_scan_ms: ~128
```

Address-to-address Germany benchmark on an RTX 4070 Ti SUPER:

```text
/usr/bin/time -p ./build/cuda-linux-x86_64/rte-gpu-route data/germany-gtfs.rtegpu \
  --rte data/germany-gtfs.rte \
  --from "Friedrich-Ebert-Straße 24, Potsdam" \
  --to "Gassenwiesen 3, Villingendorf" \
  --depart 2026-06-03T08:00:00 --iterations 1

real: 1.01s
from_address_matches: 3
to_address_matches: 1
address_threads: 16
candidate_origin_stops: 72
candidate_destination_stops: 8
gpu_best_arrival: 17:13:12
verify_status: skipped
address_resolve_ms: ~467
load_ms: ~196
host_to_device_ms: ~28
gpu_kernel_avg_ms: ~6.7
cpu_scan_ms: 0
```

With `data/germany-gtfs.rte.addridx` built, the same address-to-address plan query measured:

```text
address_index_status: hit
address_index_used: yes
address_index_entries: 18,010,030
from_address_matches: 3
to_address_matches: 1
address_resolve_ms: ~0.14
load_ms: ~245
host_to_device_ms: ~29
gpu_kernel_avg_ms: ~8.3
gpu_best_arrival: 17:13:12
plan_status: found
plan_leg_count: 11
real: 0.70s
```

The Germany sidecar was 550 MB and took about 4.2s to build. A Brandenburg sidecar built from `data/brandenburg.rte` was 37 MB, with 1,186,958 entries, and took about 0.39s to build.

With validation and plan reconstruction enabled, the same query measured `real: 1.28s`:

```text
--verify --plan
gpu_best_arrival: 17:13:12
cpu_best_arrival: 17:13:12
verify_arrival_mismatches: 0
verify_best_match: yes
plan_status: found
plan_leg_count: 11
address_resolve_ms: ~471
gpu_kernel_avg_ms: ~8.4
cpu_scan_ms: ~193
```

The reconstructed route legs follow the expected Potsdam -> Berlin -> Hannover -> Stuttgart -> Oberndorf -> Villingendorf path, ending with bus/rail route `7444` to `Villingendorf Linde` at `17:09` and a final walk arriving `17:13:12`.

Useful output modes:

```text
# Colored text plan, default color behavior
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --from FROM --to TO --plan

# Plain text plan without ANSI escapes
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --from FROM --to TO --plan --no-color

# Machine-readable route result and legs
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --from FROM --to TO --json

# Resident mode: load/copy once, then answer one tab-separated address pair per line
printf 'FROM\tTO\n' | rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --interactive --plan

# Terminal form UI: editable from/to/depart fields and a live fastest-route pane
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --tui

# Local web/API mode: resident GPU context, browser form, JSON route endpoint
rte-gpu-route data/germany-gtfs.rtegpu --rte data/germany-gtfs.rte --api
```

Resident `--interactive` mode keeps the `RTEGPU01` pack loaded and the hot arrays resident on the GPU. It prints a startup line on stderr with the one-time `load_ms` and `host_to_device_ms`; each query then reports `load_ms: 0.000` and `host_to_device_ms: 0.000`. Input is one `FROM<TAB>TO` address pair per line, with `quit` or `exit` ending the session.

Two repeated Potsdam -> Villingendorf resident queries measured:

```text
--interactive --plan --no-color
startup load_ms: ~243
startup host_to_device_ms: ~29
query_1 address_resolve_ms: ~0.15
query_1 gpu_kernel_avg_ms: ~8.33
query_1 gpu_best_arrival: 17:13:12
query_2 address_resolve_ms: ~0.11
query_2 gpu_kernel_avg_ms: ~8.33
query_2 gpu_best_arrival: 17:13:12
real for two queries including startup: 0.81s

--interactive --no-color
startup load_ms: ~233
startup host_to_device_ms: ~28
query_1 gpu_kernel_avg_ms: ~6.99
query_2 gpu_kernel_avg_ms: ~6.68
real for two queries including startup: 0.73s
```

Interactive JSON output is newline-delimited JSON: each input line produces one complete JSON object. With `--interactive --json`, the repeated benchmark produced two parse-valid rows with `address_index.status = hit`, `timing_ms.load = 0`, `timing_ms.host_to_device = 0`, `best_arrival_sec = 61992`, and 11 plan legs.

`--tui` is a terminal form mode for live routing. It enters the alternate screen, provides editable `From`, `To`, and `Depart` fields, and renders the fastest route in the lower pane as soon as the current inputs resolve exactly through the address sidecar. The TUI deliberately uses sidecar-only address lookup while editing, so incomplete or misspelled addresses return quickly instead of falling back to the full 18M-record `.rte` scan. If the sidecar misses, press Enter to run one deliberate relaxed full-scan lookup; this can resolve cases such as `Thomasburg 18, Oldenburg` even before the sidecar has been rebuilt with the new alias keys. Use Tab, Up, or Down to move between fields; Ctrl-Q or Esc exits. When `Depart` is selected, Left and Right move the time backward or forward in 5-minute steps. Optional `--from`, `--to`, and `--depart` arguments prefill the form.

`--api` starts a small local HTTP server after loading the `.rtegpu` pack and copying the hot arrays to the GPU. It binds to `127.0.0.1:8765` by default; override this with `--api-host HOST` and `--api-port PORT`. The root path serves a minimal browser form, `/health` returns pack counts, and `POST /route` or `POST /api/route` accepts a JSON route request and replies with the same structured JSON route shape as `--json`, including stop/request coordinates for each plan leg.

Address request example:

```sh
curl -sS -X POST http://127.0.0.1:8765/route \
  -H 'Content-Type: application/json' \
  --data '{
    "from": "Friedrich-Ebert-Straße 24, Potsdam",
    "to": "Gassenwiesen 3, Villingendorf",
    "depart": "2026-06-03T08:00:00"
  }'
```

Stop-index request example:

```sh
curl -sS -X POST http://127.0.0.1:8765/route \
  -H 'Content-Type: application/json' \
  --data '{
    "from_stop_index": 109476,
    "to_stop_index": 304206,
    "depart": "2026-06-03T18:00:00"
  }'
```

Coordinate requests can use either integer e7 fields (`from_lat_e7`, `from_lon_e7`, `to_lat_e7`, `to_lon_e7`) or decimal degree fields (`from_lat`, `from_lon`, `to_lat`, `to_lon`). If `depart` is omitted, the API uses the current local date and time for that request. Normal API address requests use the sidecar when it resolves exactly and otherwise fall back to the tolerant full scan, just like the one-shot CLI.

Pseudo-terminal smoke test on the same Potsdam -> Villingendorf query:

```text
--tui --from "Friedrich-Ebert-Straße 24, Potsdam" --to "Gassenwiesen 3, Villingendorf"
resident load_ms: ~235
startup host_to_device_ms: ~29
address_resolve_ms: ~0.15
gpu_kernel_ms: ~8.54
origin stops: 72
destination stops: 8
arrival: 17:13:12
route legs shown: 11
```

The comparable `rte-route` command previously measured about `real: 5.2s` on the same machine. The GPU tool therefore beats the wall clock for address-to-address routing and can now print route legs, though the CPU `rte-route` tool remains the canonical output path while parity is broadened across more benchmarks.

Current remaining work before this can be considered a drop-in route tool:

1. Broaden route-selection parity checks against `rte-route` on multiple address benchmarks.
2. Move beyond the sidecar by embedding compact address search sections into `RTEGPU01`, or keep the sidecar loaded in the resident query process and TUI.
3. Add a changed-stop/frontier transfer kernel instead of scanning all transfer edges each round.
4. Keep predecessor capture optional so benchmark-only queries avoid unnecessary global writes and host copies.

## Build Placement

CUDA tools should not be part of the default `make all` target. Use explicit experimental targets, for example:

```text
make rte-gpu-tools
make build/cuda-linux-x86_64/rte-gpu-route
```

The default freestanding Linux and macOS toolchains should continue to build without CUDA installed.
