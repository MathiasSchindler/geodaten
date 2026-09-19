# RTE GPU Route API

This document explains the experimental GPU routing tools and the HTTP API exposed by `rte-gpu-route --api`. It is meant for someone who wants to run the tools, send route requests, and understand what the responses mean without reading the source code first.

The CPU `rte-route` tool and the `OSMRTE01` `.rte` format remain the canonical route path. The GPU tools are a separate CUDA/NVIDIA experiment built around a GPU-shaped `.rtegpu` pack.

## Tools

Build the GPU tools explicitly:

```sh
make rte-gpu-tools
```

The default freestanding build does not require CUDA. The GPU binaries are built under `build/cuda-linux-x86_64/`.

| Tool | Purpose |
| --- | --- |
| `rte-to-rtegpu` | Converts an existing `OSMRTE01` `.rte` pack into a GPU-shaped `RTEGPU01` `.rtegpu` pack. |
| `rte-gpu-info` | Prints and validates counts/sections from a `.rtegpu` pack. |
| `rte-gpu-route` | Runs route queries against a `.rtegpu` pack using CUDA kernels for the transit scan. |

Typical pack preparation:

```sh
./build/cuda-linux-x86_64/rte-to-rtegpu \
  data/germany-gtfs.rte \
  data/germany-gtfs.rtegpu

./build/cuda-linux-x86_64/rte-gpu-info \
  data/germany-gtfs.rtegpu
```

For fast address lookup, build the sidecar index once:

```sh
./build/cuda-linux-x86_64/rte-gpu-route \
  data/germany-gtfs.rtegpu \
  --rte data/germany-gtfs.rte \
  --build-address-index
```

The default sidecar path is `FILE.rte.addridx`, for example `data/germany-gtfs.rte.addridx`.

## Route Modes

`rte-gpu-route` supports several ways to ask for a route:

```text
FILE.rtegpu --from-stop N --to-stop N
FILE.rtegpu --from-latlon LAT,LON --to-latlon LAT,LON
FILE.rtegpu --rte FILE.rte --from ADDRESS --to ADDRESS
FILE.rtegpu --rte FILE.rte --interactive
FILE.rtegpu --rte FILE.rte --tui
FILE.rtegpu --rte FILE.rte --api
```

Short version of when to use each mode:

| Mode | Best For |
| --- | --- |
| Stop index | Testing known stop-to-stop queries without address lookup. |
| Coordinates | Integrations that already have GPS/WGS84 coordinates. This skips address resolution. |
| Address CLI | One-off address-to-address route requests. |
| `--interactive` | Batch/resident stdin queries where the pack should load only once. |
| `--tui` | Local terminal form for manual route testing. |
| `--api` | Local HTTP/JSON integration and browser-based manual testing. |

## Starting the API

Start the API server with both the `.rtegpu` pack and the source `.rte` pack:

```sh
./build/cuda-linux-x86_64/rte-gpu-route \
  data/germany-gtfs.rtegpu \
  --rte data/germany-gtfs.rte \
  --api
```

By default it binds to `127.0.0.1:8765` and prints a readiness line like this:

```text
rte-gpu-route: api ready http://127.0.0.1:8765/ load_ms=256.170 host_to_device_ms=29.290 endpoint=POST /route
```

The server loads the `.rtegpu` pack and copies the hot arrays to the GPU once at startup. Each request then reuses that resident GPU context, so response timing normally reports `load: 0.0` and `host_to_device: 0.0`.

Bind options:

```sh
./build/cuda-linux-x86_64/rte-gpu-route \
  data/germany-gtfs.rtegpu \
  --rte data/germany-gtfs.rte \
  --api \
  --api-host 127.0.0.1 \
  --api-port 8765
```

Keep the default localhost binding for normal use. The built-in server has no authentication, no TLS, and no request concurrency model beyond handling accepted connections in process. If it must be reachable from another machine, put it behind a trusted local network boundary or a proper reverse proxy.

## Endpoints

| Method | Path | Description |
| --- | --- | --- |
| `GET` | `/` | Minimal browser form for manual testing. |
| `GET` | `/health` | Health check with pack metadata. |
| `POST` | `/route` | Main JSON route endpoint. |
| `POST` | `/api/route` | Alias for `/route`. |

Health check:

```sh
curl -sS http://127.0.0.1:8765/health
```

Example response:

```json
{"status":"ok","format":"RTEGPU01","stops":688005,"trips":1643599}
```

## Route Requests

All route requests are JSON objects sent to `POST /route` or `POST /api/route` with `Content-Type: application/json`.

### Address Request

Use `from` and `to` when the caller has human-readable German addresses:

```sh
curl -sS -X POST http://127.0.0.1:8765/route \
  -H 'Content-Type: application/json' \
  --data '{
    "from": "Friedrich-Ebert-Straße 24, Potsdam",
    "to": "Gassenwiesen 3, Villingendorf",
    "depart": "2026-06-03T08:00:00"
  }'
```

Address mode uses the `.rte` address dictionary. If the sidecar index is present and current, exact lookup is typically sub-millisecond. If the sidecar misses or is stale, the API falls back to the tolerant full scan, which can take hundreds of milliseconds on the Germany pack.

Address matching understands common German variants such as umlaut/ASCII spellings, shortened place prefixes, fuzzy street names, and house-number lists such as `18,20` when the query asks for `18`.

### Coordinate Request

Use decimal WGS84 coordinates when the caller already knows the origin and destination points:

```sh
curl -sS -X POST http://127.0.0.1:8765/route \
  -H 'Content-Type: application/json' \
  --data '{
    "from_lat": 52.3980,
    "from_lon": 13.0580,
    "to_lat": 48.1985,
    "to_lon": 8.5900,
    "depart": "2026-06-03T08:00:00"
  }'
```

You can also send integer e7 coordinates:

```json
{
  "from_lat_e7": 523980000,
  "from_lon_e7": 130580000,
  "to_lat_e7": 481985000,
  "to_lon_e7": 85900000,
  "depart": "2026-06-03T08:00:00"
}
```

Coordinate requests skip address lookup entirely. This saves little compared with a sidecar hit, but it avoids the expensive fallback address scan and is the best API shape for callers that already geocode addresses elsewhere.

### Stop-Index Request

Use stop indexes for tests, benchmarks, or internal tooling:

```sh
curl -sS -X POST http://127.0.0.1:8765/route \
  -H 'Content-Type: application/json' \
  --data '{
    "from_stop_index": 109476,
    "to_stop_index": 304206,
    "depart": "2026-06-03T18:00:00"
  }'
```

Stop indexes are pack-local indexes from the `.rtegpu` file. They are useful for repeatable validation, but they are not stable user-facing IDs.

### Departure Time

The easiest format is:

```json
{"depart":"2026-06-03T08:00:00"}
```

Seconds are optional:

```json
{"depart":"2026-06-03T08:00"}
```

If `depart` is omitted, the API uses the local current date and time at request handling time.

The lower-level fields `depart_date` and `depart_seconds` are also accepted together:

```json
{"depart_date":20260603,"depart_seconds":28800}
```

`depart_seconds` is seconds after local midnight. The requested date must be covered by the service calendar in the `.rtegpu` pack.

### Optional Request Fields

| Field | Type | Meaning |
| --- | --- | --- |
| `address_index_only` | boolean | For address requests, require the sidecar index and fail instead of falling back to the full scan. Useful for latency-sensitive UI calls. |

Do not mix address fields with stop-index or coordinate fields in the same request. Use exactly one origin/destination style per request.

## Route Responses

Successful route responses are JSON objects. The shape is intentionally close to CLI `--json` output.

Top-level fields:

| Field | Meaning |
| --- | --- |
| `format` | Pack format, currently `RTEGPU01`. |
| `query_kind` | Routing algorithm label. |
| `resident_mode` | Always `true` for API responses. |
| `api_mode` | Always `true` for API responses. |
| `query` | Parsed request fields. |
| `address_mode` | Whether this was an address request. Coordinate and stop-index requests are `false`. |
| `address_index` | Sidecar lookup status, usage flag, and entry count. |
| `counts` | Pack counts such as stops, trips, events, and transfer edges. |
| `result` | Best-route summary. |
| `verification` | CPU verifier status. The API currently returns `enabled: false`. |
| `plan` | Reconstructed route legs when a plan was found. |
| `timing_ms` | Timings for address lookup, pack load, host-to-device copy, GPU kernel, and CPU scan. |

Result object:

```json
{
  "status": "found",
  "candidate_origin_stops": 72,
  "candidate_destination_stops": 8,
  "best_stop_index": 304206,
  "best_arrival_sec": 61992,
  "walk_from_stop_m": 282
}
```

`best_arrival_sec` is seconds after local midnight. Values can exceed `86400` for arrivals after midnight on the next service day.

Plan legs can have these `kind` values:

| Kind | Meaning |
| --- | --- |
| `access_walk` | Walk from the request origin to the first transit stop. Includes request origin coordinates when known and first-stop coordinates. |
| `ride` | Vehicle ride on a GTFS route. Includes mode, route index/name, board stop, alight stop, stop coordinates, and times. |
| `transfer_walk` | Walk between transit stops during the route. Includes both stop coordinates. |
| `egress_walk` | Walk from the final transit stop to the requested destination. Includes final-stop coordinates and destination coordinates when known. |

Coordinate fields are emitted in both integer e7 form and decimal degree form. For example, a ride leg contains `board_stop_lat_e7`, `board_stop_lon_e7`, `board_stop_lat`, `board_stop_lon`, `alight_stop_lat_e7`, `alight_stop_lon_e7`, `alight_stop_lat`, and `alight_stop_lon`. Walk legs use analogous prefixes such as `from`, `to`, `from_stop`, and `to_stop`.

Example response excerpt:

```json
{
  "address_mode": true,
  "address_index": {"status":"hit","used":true,"entries":18010030},
  "result": {"status":"found","best_arrival_sec":61992},
  "plan": {
    "status": "found",
    "legs": [
      {"kind":"access_walk","to_stop":"Nauener Tor","to_stop_lat":52.4028200,"to_stop_lon":13.0578430},
      {"kind":"ride","mode":"rail","route_short_name":"RE1","board_stop_lat":52.4007950,"board_stop_lon":13.0673250,"alight_stop_lat":52.5255890,"alight_stop_lon":13.3695480},
      {"kind":"egress_walk","walk_m":282,"from_stop_lat":48.2004680,"from_stop_lon":8.5919500,"to_lat":48.1985000,"to_lon":8.5900000}
    ]
  },
  "timing_ms": {
    "address_resolve": 0.140,
    "load": 0.000,
    "host_to_device": 0.000,
    "gpu_kernel_avg": 8.300,
    "cpu_scan": 0.000
  }
}
```

The excerpt omits many fields for readability. Real responses include stop indexes, full stop names when available, route long names when available, e7 coordinates, departure seconds, and arrival seconds.

## Errors

Bad requests return HTTP `400` with a JSON error object:

```json
{"error":{"message":"request must supply either from/to address strings, stop indexes, or coordinates"}}
```

Common causes:

| Error Cause | Fix |
| --- | --- |
| Missing origin or destination | Send both `from` and `to`, both coordinate pairs, or both stop indexes. |
| Mixed request styles | Do not combine address strings with coordinates or stop indexes. |
| Unknown address | Check spelling, rebuild `.rte.addridx`, or allow fallback scanning by omitting `address_index_only`. |
| Date outside service calendar | Pick a date covered by the `.rtegpu` service calendar. |
| Stop index out of range | Use stop indexes from the same `.rtegpu` pack. |

Unknown paths return HTTP `404` with a JSON error object.

## Performance Expectations

The API is resident: pack loading and GPU upload happen once at startup. Per request, the main costs are:

1. Address lookup, if the request uses `from`/`to` strings.
2. Access and egress stop candidate scanning around the origin/destination.
3. CUDA trip scan and transfer relaxation.
4. Plan reconstruction and JSON formatting.

Address request timing depends heavily on the sidecar:

| Request Type | Expected Address Cost |
| --- | --- |
| Address with sidecar hit | Usually sub-millisecond. |
| Address with fallback full scan | Hundreds of milliseconds on Germany-scale data. |
| Coordinates | Zero address lookup. |
| Stop indexes | Zero address lookup. |

The Germany benchmark with a sidecar hit has measured around 8 ms GPU kernel time per planned route query on the test RTX 4070 Ti SUPER system. Full wall-clock time depends on JSON size, address lookup mode, and whether the process was already resident.

## Operational Notes

- The API is experimental and local-first.
- It is not a replacement for the canonical CPU `rte-route` tool.
- It has no built-in authentication or TLS.
- The default bind address is `127.0.0.1` for a reason.
- Requests are handled by the `rte-gpu-route` process that owns the CUDA context.
- Rebuild `data/germany-gtfs.rte.addridx` after address matching changes if live address lookups should use the newest aliases.
- Keep the `.rte`, `.rtegpu`, and `.rte.addridx` files from the same dataset/build.

## Minimal Client Example

Python example using only the standard library:

```python
import json
import urllib.request

request = urllib.request.Request(
    "http://127.0.0.1:8765/route",
    data=json.dumps({
        "from": "Friedrich-Ebert-Straße 24, Potsdam",
        "to": "Gassenwiesen 3, Villingendorf",
        "depart": "2026-06-03T08:00:00",
    }).encode("utf-8"),
    headers={"Content-Type": "application/json"},
    method="POST",
)

with urllib.request.urlopen(request) as response:
    route = json.load(response)

print(route["result"]["status"])
print(route["result"]["best_arrival_sec"])
for leg in route["plan"]["legs"]:
    print(leg["kind"])
```

For browser/manual use, open:

```text
http://127.0.0.1:8765/
```
