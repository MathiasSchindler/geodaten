#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <basemap-raw.tsv> <state.csv.gz> <state-code> <output-directory>" >&2
  exit 2
fi

basemap_raw=$1
state_csv=$2
state_code=$3
output=$4
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work="$output/work-$state_code"

mkdir -p "$work"

node "$tools/generate-comparison-keys.mjs" \
  basemap "$basemap_raw" "$state_code" "$work/basemap" \
  > "$work/basemap-statistics.json"
node "$tools/generate-comparison-keys.mjs" \
  state "$state_csv" "$state_code" "$work/state" \
  > "$work/state-statistics.json"

for file in "$work"/*.keys; do
  LC_ALL=C sort -u -T "$work" "$file" > "$file.sorted"
done

for file in "$work"/*.coords; do
  LC_ALL=C sort -T "$work" "$file" > "$file.sorted"
done

LC_ALL=C comm -12 "$work/basemap.union.keys.sorted" "$work/state.union.keys.sorted" \
  > "$work/matched-either.keys"
LC_ALL=C comm -12 "$work/basemap.primary.keys.sorted" "$work/state.primary.keys.sorted" \
  > "$work/matched-postal-locality.keys"
LC_ALL=C comm -12 "$work/basemap.primary.keys.sorted" "$work/state.secondary.keys.sorted" \
  > "$work/matched-municipality.keys"
node "$tools/compare-coordinates.mjs" \
  "$work/basemap.union.coords.sorted" "$work/state.union.coords.sorted" \
  > "$work/coordinate-agreement.json"

basemap_unique=$(wc -l < "$work/basemap.union.keys.sorted" | tr -d ' ')
state_union_unique=$(wc -l < "$work/state.union.keys.sorted" | tr -d ' ')
state_postal_unique=$(wc -l < "$work/state.primary.keys.sorted" | tr -d ' ')
state_municipality_unique=$(wc -l < "$work/state.secondary.keys.sorted" | tr -d ' ')
matched_either=$(wc -l < "$work/matched-either.keys" | tr -d ' ')
matched_postal=$(wc -l < "$work/matched-postal-locality.keys" | tr -d ' ')
matched_municipality=$(wc -l < "$work/matched-municipality.keys" | tr -d ' ')

jq -n \
  --arg state "$state_code" \
  --argjson basemap_unique "$basemap_unique" \
  --argjson state_union_unique "$state_union_unique" \
  --argjson state_postal_unique "$state_postal_unique" \
  --argjson state_municipality_unique "$state_municipality_unique" \
  --argjson matched_either "$matched_either" \
  --argjson matched_postal "$matched_postal" \
  --argjson matched_municipality "$matched_municipality" \
  --slurpfile basemap_stats "$work/basemap-statistics.json" \
  --slurpfile state_stats "$work/state-statistics.json" \
  --slurpfile coordinate_agreement "$work/coordinate-agreement.json" \
  '{
    state_code: $state,
    basemap_unique_normalized_addresses: $basemap_unique,
    state_unique_normalized_addresses_either_locality: $state_union_unique,
    state_unique_normalized_addresses_postal_locality: $state_postal_unique,
    state_unique_normalized_addresses_municipality: $state_municipality_unique,
    matched_either_locality: $matched_either,
    matched_postal_locality: $matched_postal,
    matched_municipality: $matched_municipality,
    basemap_fields: $basemap_stats[0],
    state_fields: $state_stats[0],
    coordinate_agreement: $coordinate_agreement[0],
    basemap_match_rate_either_locality:
      (if $basemap_unique == 0 then null else $matched_either / $basemap_unique end),
    state_match_rate_either_locality:
      (if $state_union_unique == 0 then null else $matched_either / $state_union_unique end)
  }' > "$output/$state_code.json"
