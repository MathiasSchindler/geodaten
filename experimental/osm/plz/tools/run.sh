#!/bin/bash
set -euo pipefail

tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(CDPATH= cd -- "$tools/../../../.." && pwd)
snapshot=${1:-"$project/experimental/osm/plz/2026-02"}
addresses=${2:-"$project/adressen/bund/konsolidiert/2026-08-28/adressen.csv.gz"}
source="$snapshot/source"
topology="$source/postleitzahlen.topojson.br"

mkdir -p "$source"
npm ci --prefix "$tools" --ignore-scripts

if [ ! -s "$topology" ]; then
  curl --fail --location --retry 5 --retry-all-errors \
    --output "$topology" \
    "https://github.com/yetzt/postleitzahlen/releases/download/2026.02/postleitzahlen.topojson.br"
fi

if [ ! -s "$source/release.json" ]; then
  curl --fail --location --retry 5 --retry-all-errors \
    --output "$source/release.json" \
    "https://api.github.com/repos/yetzt/postleitzahlen/releases/288842581"
fi
if [ ! -s "$source/README.md" ]; then
  curl --fail --location --retry 5 --retry-all-errors \
    --output "$source/README.md" \
    "https://raw.githubusercontent.com/yetzt/postleitzahlen/5b68a065bb39a10acebbec6b93337cd671e5a441/README.md"
fi
if [ ! -s "$source/LICENSE.md" ]; then
  curl --fail --location --retry 5 --retry-all-errors \
    --output "$source/LICENSE.md" \
    "https://raw.githubusercontent.com/yetzt/postleitzahlen/5b68a065bb39a10acebbec6b93337cd671e5a441/LICENSE.md"
fi

actual_source_hash=$(shasum -a 256 "$topology" | awk '{print $1}')
expected_source_hash=4b327eda92e9a0fcf5299d0d50b329347e7e980d1c2f85806c3cc76840ae58e0
if [ "$actual_source_hash" != "$expected_source_hash" ]; then
  echo "Unexpected TopoJSON checksum: $actual_source_hash" >&2
  exit 1
fi

node "$tools/assign-postcodes.mjs" \
  "$topology" \
  "$addresses" \
  "$snapshot/plz-zuordnung.csv.gz" \
  "$snapshot/auswertung.json"

expected_records=$(( $(gzip -dc "$addresses" | wc -l | tr -d ' ') - 1 ))
node "$tools/verify-experiment.mjs" \
  "$snapshot/plz-zuordnung.csv.gz" \
  "$addresses" \
  "$snapshot/auswertung.json" \
  "$expected_records" \
  > "$snapshot/validierung.json"

node "$tools/analyze-disagreements.mjs" \
  "$snapshot/plz-zuordnung.csv.gz" \
  "$addresses" \
  "$snapshot/abweichungen.csv.gz" \
  "$snapshot/abweichungsanalyse.json"
