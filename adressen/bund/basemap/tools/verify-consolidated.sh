#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <snapshot-directory>" >&2
  exit 2
fi

snapshot=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work="$snapshot/work/validation"
assembly="$snapshot/work/assembly"
mkdir -p "$work"

for archive in adressen provenienz konflikte; do
  gzip -t "$snapshot/$archive.csv.gz"
  cmp "$assembly/$archive.csv" <(gzip -dc "$snapshot/$archive.csv.gz")
  LC_ALL=C sort -c "$assembly/$archive-sortiert-ohne-kopf.csv"
  cmp "$snapshot/$archive.csv.gz" <(gzip -n -9 -c "$assembly/$archive.csv")
done

node "$tools/validate-consolidated.mjs" \
  "$snapshot/adressen.csv.gz" \
  "$snapshot/provenienz.csv.gz" \
  "$snapshot/konflikte.csv.gz" \
  "$work/main-ids-unsorted.txt" \
  "$work/provenance-ids.txt" \
  > "$snapshot/validierungs-statistik.json"

LC_ALL=C sort -T "$work" "$work/main-ids-unsorted.txt" > "$work/main-ids.txt"
LC_ALL=C sort -c -u "$work/main-ids.txt"
LC_ALL=C sort -c -u "$work/provenance-ids.txt"
cmp "$work/main-ids.txt" "$work/provenance-ids.txt"

jq -e --slurpfile validation "$snapshot/validierungs-statistik.json" '
  .consolidated_records == $validation[0].main_records and
  .records_with_postcode == $validation[0].records_with_postcode and
  (.ambiguous_full_key + .ambiguous_street_house) ==
    $validation[0].conflict_records
' "$snapshot/konsolidierungs-statistik.json" >/dev/null

header_hash=$(
  {
    IFS= read -r header
    printf '%s\n' "$header"
    cat >/dev/null
  } < <(gzip -dc "$snapshot/adressen.csv.gz") |
    shasum -a 256 |
    awk '{print $1}'
)
[ "$header_hash" = "5c53123212b86420d14b7e1851c4644a6a3f63eb6b7334a78ae2772d04361ad2" ]

statistics_tmp="$snapshot/export-statistik.txt.tmp"
grep -v -E '^(validation|validation_sha256)=' \
  "$snapshot/export-statistik.txt" > "$statistics_tmp"
mv "$statistics_tmp" "$snapshot/export-statistik.txt"

printf 'validation=passed\n' >> "$snapshot/export-statistik.txt"
printf 'validation_sha256=%s\n' \
  "$(shasum -a 256 "$snapshot/validierungs-statistik.json" | awk '{print $1}')" \
  >> "$snapshot/export-statistik.txt"
