#!/bin/sh
#
# osm-postal regression test.
#
# Generates the synthetic PBF fixture, runs osm-postal over it, and compares
# the GeoJSON output and the TSV audit report against the committed golden
# files. Also verifies that two consecutive runs are byte identical and that
# the exit code contract of --strict holds.
#
# Usage: sh tests/run_osm_postal_test.sh BUILD_DIR

set -e

BUILD_DIR="$1"
if [ -z "$BUILD_DIR" ]; then
	echo "usage: $0 BUILD_DIR" >&2
	exit 2
fi

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$BUILD_DIR/osm-postal-test"
FIXTURE="$WORK_DIR/postal_fixture.osm.pbf"
OUT="$WORK_DIR/postal.geojson"
OUT2="$WORK_DIR/postal_repeat.geojson"
REPORT="$WORK_DIR/postal_report.tsv"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"

"$BUILD_DIR/osm-postal-fixture" "$FIXTURE" >/dev/null

"$BUILD_DIR/osm-postal" "$FIXTURE" "$OUT" --report "$REPORT" --quiet >"$WORK_DIR/stats.txt"
"$BUILD_DIR/osm-postal" "$FIXTURE" "$OUT2" --quiet >/dev/null

if ! diff -u "$TEST_DIR/expected/postal_fixture.geojson" "$OUT"; then
	echo "FAIL: GeoJSON output does not match the golden file" >&2
	exit 1
fi
if ! diff -u "$TEST_DIR/expected/postal_fixture_report.tsv" "$REPORT"; then
	echo "FAIL: audit report does not match the golden file" >&2
	exit 1
fi
if ! diff -u "$OUT" "$OUT2" >/dev/null; then
	echo "FAIL: two runs produced different output" >&2
	exit 1
fi
if ! diff -u "$TEST_DIR/expected/postal_fixture_stats.txt" "$WORK_DIR/stats.txt"; then
	echo "FAIL: summary statistics do not match the golden file" >&2
	exit 1
fi

# The fixture contains one relation with a broken ring, so --strict must fail.
set +e
"$BUILD_DIR/osm-postal" "$FIXTURE" "$WORK_DIR/strict.geojson" --strict --quiet >/dev/null
STRICT_STATUS=$?
set -e
if [ "$STRICT_STATUS" -eq 0 ]; then
	echo "FAIL: --strict returned 0 despite a broken relation" >&2
	exit 1
fi

# The fixture also references a way that is not in the file, which --strict
# tolerates as an extract-coverage fact but --strict-incomplete does not.
set +e
"$BUILD_DIR/osm-postal" "$FIXTURE" "$WORK_DIR/strict2.geojson" --strict-incomplete --quiet >/dev/null
STRICT_INCOMPLETE_STATUS=$?
set -e
if [ "$STRICT_INCOMPLETE_STATUS" -eq 0 ]; then
	echo "FAIL: --strict-incomplete returned 0 despite an incomplete relation" >&2
	exit 1
fi

# Without the bbox gate the Paris relation becomes acceptable, so the feature
# count grows by one, while the relation with the foreign tag stays out.
"$BUILD_DIR/osm-postal" "$FIXTURE" "$WORK_DIR/nobbox.geojson" --no-bbox-filter --quiet >"$WORK_DIR/nobbox_stats.txt"
NOBBOX_FEATURES=$(grep '^features_written: ' "$WORK_DIR/nobbox_stats.txt" | cut -d' ' -f2)
if [ "$NOBBOX_FEATURES" != "4" ]; then
	echo "FAIL: --no-bbox-filter wrote $NOBBOX_FEATURES features, expected 4" >&2
	exit 1
fi

# --require-de-tag keeps only the relation carrying postal_code:DE.
"$BUILD_DIR/osm-postal" "$FIXTURE" "$WORK_DIR/detag.geojson" --require-de-tag --quiet >"$WORK_DIR/detag_stats.txt"
DETAG_FEATURES=$(grep '^features_written: ' "$WORK_DIR/detag_stats.txt" | cut -d' ' -f2)
if [ "$DETAG_FEATURES" != "1" ]; then
	echo "FAIL: --require-de-tag wrote $DETAG_FEATURES features, expected 1" >&2
	exit 1
fi

echo "osm-postal: all checks passed"
