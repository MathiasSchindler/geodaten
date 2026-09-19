#!/bin/bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <snapshot-directory>" >&2
  exit 2
fi

snapshot=$1
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
states="$snapshot/work/states"
work="$snapshot/work/assembly"
mkdir -p "$work"

node "$tools/summarize-consolidation.mjs" "$states" \
  > "$snapshot/konsolidierungs-statistik.json"

assemble_csv() {
  kind=$1
  destination=$2
  first=$(find "$states" -name "??-$kind-unsortiert.csv" -type f |
    LC_ALL=C sort |
    head -n 1)
  if [ -z "$first" ]; then
    echo "No $kind inputs found" >&2
    return 1
  fi

  header="$work/$kind-header.csv"
  body="$work/$kind-sortiert-ohne-kopf.csv"
  final="$work/$kind.csv"
  head -n 1 "$first" > "$header"

  while IFS= read -r file; do
    cmp "$header" <(head -n 1 "$file")
  done < <(find "$states" -name "??-$kind-unsortiert.csv" -type f |
    LC_ALL=C sort)

  while IFS= read -r file; do
    tail -n +2 "$file"
  done < <(find "$states" -name "??-$kind-unsortiert.csv" -type f |
    LC_ALL=C sort) |
    LC_ALL=C sort -T "$work" > "$body"

  cat "$header" "$body" > "$final"
  gzip -n -9 -c "$final" > "$destination"
}

assemble_csv adressen "$snapshot/adressen.csv.gz"
assemble_csv provenienz "$snapshot/provenienz.csv.gz"
assemble_csv konflikte "$snapshot/konflikte.csv.gz"

{
  printf 'records=%s\n' "$(jq '.consolidated_records' "$snapshot/konsolidierungs-statistik.json")"
  printf 'records_with_postcode=%s\n' "$(jq '.records_with_postcode' "$snapshot/konsolidierungs-statistik.json")"
  printf 'records_without_postcode=%s\n' "$(jq '.records_without_postcode' "$snapshot/konsolidierungs-statistik.json")"
  printf 'provenance_records=%s\n' "$(( $(wc -l < "$work/provenienz.csv" | tr -d ' ') - 1 ))"
  printf 'conflict_records=%s\n' "$(( $(wc -l < "$work/konflikte.csv" | tr -d ' ') - 1 ))"
  printf 'csv_columns=14\n'
  printf 'address_sha256=%s\n' "$(shasum -a 256 "$snapshot/adressen.csv.gz" | awk '{print $1}')"
  printf 'provenance_sha256=%s\n' "$(shasum -a 256 "$snapshot/provenienz.csv.gz" | awk '{print $1}')"
  printf 'conflicts_sha256=%s\n' "$(shasum -a 256 "$snapshot/konflikte.csv.gz" | awk '{print $1}')"
} > "$snapshot/export-statistik.txt"
