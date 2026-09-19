# Upstream Provenance

This directory is a vendored copy of an external repository. This file records
where the code came from and what was changed locally.

## Source

| Field | Value |
| --- | --- |
| Repository | <https://github.com/MathiasSchindler/pbf-parser> |
| Vendored commit | `4b07ae927922cc436cd4f36a731c086e779fdb26` |
| Vendored into | `experimental/osm/pbf-parser` |
| Upstream license | CC0 1.0 Universal (see `LICENSE`) |

The upstream project dedicates its source code to the public domain under
CC0 1.0 Universal. Local changes in this directory are released under the same
terms, so the vendored tree stays uniformly CC0.

Note that OSM data processed by these tools is *not* covered by CC0. Data
extracted from an `.osm.pbf` remains subject to the OpenStreetMap ODbL 1.0
license and its attribution and share-alike requirements. The GeoJSON produced
by `osm-postal` is a derived database in the ODbL sense.

## Local Changes

Everything upstream is unmodified except where listed below. No file outside
`experimental/osm/pbf-parser` is touched by these changes.

### Added

| Path | Purpose |
| --- | --- |
| `src/tools/osm_postal.c` | New `osm-postal` tool: extracts German `boundary=postal_code` relations from an `.osm.pbf` and writes a deterministic GeoJSON `FeatureCollection` of PLZ polygons plus an optional TSV audit report. |
| `src/tools/osm_postal_fixture.c` | New `osm-postal-fixture` tool: writes a small synthetic `.osm.pbf` with uncompressed blobs, used as the regression-test input for `osm-postal`. |
| `tests/run_osm_postal_test.sh` | POSIX-shell regression harness for `osm-postal`. |
| `tests/expected/postal_fixture.geojson` | Golden GeoJSON output for the fixture. |
| `tests/expected/postal_fixture_report.tsv` | Golden audit report for the fixture. |
| `tests/expected/postal_fixture_stats.txt` | Golden summary statistics for the fixture. |
| `UPSTREAM.md` | This file. |

### Modified

| Path | Change |
| --- | --- |
| `Makefile` | Added `OSM_POSTAL_SRCS`, `OSM_POSTAL_FIXTURE_SRCS`, `MACOS_OSM_POSTAL_SRCS` and `MACOS_OSM_POSTAL_FIXTURE_SRCS`; added the four build rules; added both Linux binaries to `LINUX_TOOLS`; added the phony targets `postal-tools`, `macos-postal-tools`, `test-postal` and `macos-test-postal`. |
| `README.md` | Added `osm-postal` and `osm-postal-fixture` to the `## Scope` list and added a `## Postal Code Polygons` section documenting the three passes, the ring assembly, the Germany rule, the output schema and the test. |

The new tools follow the existing conventions of the vendored tree: freestanding
C11, no libc, no external dependencies, `src/shared/runtime` helpers instead of
standard library functions, and the existing `pbf_stream_entities` streaming
API.

## Refreshing From Upstream

Because only the two files listed under *Modified* differ from upstream, a
refresh is a normal merge of the upstream tree over this directory, followed by
re-applying the `Makefile` and `README.md` additions and re-running
`make macos-test-postal` (or `make test-postal` on Linux).
