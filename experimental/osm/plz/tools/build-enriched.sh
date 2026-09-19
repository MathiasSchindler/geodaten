#!/bin/bash
set -euo pipefail

tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project=$(CDPATH= cd -- "$tools/../../../.." && pwd)
source_addresses=${1:-"$project/adressen/bund/konsolidiert/2026-08-28/adressen.csv.gz"}
assignments=${2:-"$project/experimental/osm/plz/2026-02/plz-zuordnung.csv.gz"}
destination=${3:-"$project/experimental/osm/plz"}
work="$destination/work-enriched"

mkdir -p "$work"
node "$tools/build-enriched-addresses.mjs" \
  "$source_addresses" \
  "$assignments" \
  "$work/adressen-unsortiert.csv" \
  "$destination/adressen-statistik.json"

jq -e '
  .records == 23252486 and
  .official_postcodes_preserved == 8528354 and
  .osm_postcodes_added == 14723718 and
  .unresolved_without_postcode == 414
' "$destination/adressen-statistik.json" >/dev/null

head -n 1 "$work/adressen-unsortiert.csv" > "$work/header.csv"
tail -n +2 "$work/adressen-unsortiert.csv" |
  LC_ALL=C sort -T "$work" > "$work/adressen-sortiert-ohne-kopf.csv"
cat "$work/header.csv" "$work/adressen-sortiert-ohne-kopf.csv" \
  > "$work/adressen.csv"
gzip -n -9 -c "$work/adressen.csv" > "$destination/adressen.csv.gz"

records=$(jq '.records' "$destination/adressen-statistik.json")
records_with_postcode=$(
  jq '.records_with_postcode' "$destination/adressen-statistik.json"
)
node "$tools/validate-enriched-addresses.mjs" \
  "$destination/adressen.csv.gz" \
  "$records" \
  "$records_with_postcode" \
  > "$destination/adressen-validierung.json"

gzip -t "$destination/adressen.csv.gz"
LC_ALL=C sort -c "$work/adressen-sortiert-ohne-kopf.csv"
cmp "$destination/adressen.csv.gz" <(gzip -n -9 -c "$work/adressen.csv")

{
  printf 'records=%s\n' "$records"
  printf 'official_postcodes_preserved=%s\n' \
    "$(jq '.official_postcodes_preserved' "$destination/adressen-statistik.json")"
  printf 'osm_postcodes_added=%s\n' \
    "$(jq '.osm_postcodes_added' "$destination/adressen-statistik.json")"
  printf 'records_with_postcode=%s\n' "$records_with_postcode"
  printf 'records_without_postcode=%s\n' \
    "$(jq '.records_without_postcode' "$destination/adressen-statistik.json")"
  printf 'source_address_sha256=%s\n' \
    "$(shasum -a 256 "$source_addresses" | awk '{print $1}')"
  printf 'assignment_sha256=%s\n' \
    "$(shasum -a 256 "$assignments" | awk '{print $1}')"
  printf 'address_sha256=%s\n' \
    "$(shasum -a 256 "$destination/adressen.csv.gz" | awk '{print $1}')"
  printf 'validation=passed\n'
} > "$destination/adressen-export-statistik.txt"
