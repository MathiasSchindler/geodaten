#!/bin/bash
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <state-raw.tsv> <state-code> <destination-directory>" >&2
  exit 2
fi

raw=$1
state_code=$2
destination=$3
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work="$destination/work-basemap"

mkdir -p "$work"

unsorted="$work/adressen-unsortiert.csv"
header="$work/header.csv"
body="$work/adressen-sortiert-ohne-kopf.csv"
final="$work/adressen.csv"

node "$tools/build-csv.mjs" "$raw" "$unsorted" "$state_code" \
  > "$destination/build-statistics-basemap.json"
head -n 1 "$unsorted" > "$header"
tail -n +2 "$unsorted" |
  LC_ALL=C sort -T "$work" > "$body"
cat "$header" "$body" > "$final"
gzip -n -9 -c "$final" > "$destination/adressen.csv.gz"
gzip -t "$destination/adressen.csv.gz"

{
  printf 'country_code=%s\n' "$state_code"
  printf 'records=%s\n' "$(( $(wc -l < "$final" | tr -d ' ') - 1 ))"
  printf 'csv_columns=%s\n' "$(head -n 1 "$final" | awk -F',' '{print NF}')"
  printf 'output_gzip_bytes=%s\n' "$(wc -c < "$destination/adressen.csv.gz" | tr -d ' ')"
  printf 'sha256=%s\n' "$(shasum -a 256 "$destination/adressen.csv.gz" | awk '{print $1}')"
} > "$destination/export-statistik-basemap.txt"
