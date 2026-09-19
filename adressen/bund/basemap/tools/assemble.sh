#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <snapshot-directory>" >&2
  exit 2
fi

snapshot=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work="$snapshot/work"
chunks="$snapshot/chunks"

mkdir -p "$work"

raw_sorted="$work/adressen-basemap-roh-sortiert.tsv"
csv_unsorted="$work/adressen-unsortiert.csv"
csv_header="$work/header.csv"
csv_body_sorted="$work/adressen-sortiert-ohne-kopf.csv"
csv_final="$work/adressen.csv"

if [ ! -s "$raw_sorted" ]; then
  raw_sorted_tmp="$raw_sorted.tmp"
  rm -f "$raw_sorted_tmp"
  gzip -t "$chunks"/*.tsv.gz
  gzip -dc "$chunks"/*.tsv.gz |
    LC_ALL=C sort -u -T "$work" > "$raw_sorted_tmp"
  mv "$raw_sorted_tmp" "$raw_sorted"
fi

node "$tools/analyze-raw.mjs" "$raw_sorted" \
  > "$snapshot/raw-statistics.json"
node "$tools/split-raw-by-state.mjs" "$raw_sorted" "$work/states" \
  > "$snapshot/state-record-counts.json"
node "$tools/build-csv.mjs" "$raw_sorted" "$csv_unsorted" \
  > "$snapshot/build-statistics.json"

head -n 1 "$csv_unsorted" > "$csv_header"
tail -n +2 "$csv_unsorted" |
  LC_ALL=C sort -T "$work" > "$csv_body_sorted"
cat "$csv_header" "$csv_body_sorted" > "$csv_final"
gzip -n -9 -c "$csv_final" > "$snapshot/adressen.csv.gz"

{
  printf 'raw_unique_records=%s\n' "$(wc -l < "$raw_sorted" | tr -d ' ')"
  printf 'csv_records=%s\n' "$(( $(wc -l < "$csv_final" | tr -d ' ') - 1 ))"
  printf 'csv_columns=%s\n' "$(head -n 1 "$csv_final" | awk -F',' '{print NF}')"
  printf 'output_gzip_bytes=%s\n' "$(wc -c < "$snapshot/adressen.csv.gz" | tr -d ' ')"
  printf 'sha256=%s\n' "$(shasum -a 256 "$snapshot/adressen.csv.gz" | awk '{print $1}')"
} > "$snapshot/assembly-statistics.txt"
