#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <snapshot-directory> <addresses-root>" >&2
  exit 2
fi

snapshot=$1
addresses_root=$2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output="$snapshot/vergleich-laender"

mkdir -p "$output"

for basemap_state in "$snapshot"/work/states/*.tsv; do
  state_code=$(basename "$basemap_state" .tsv)
  state_csv=$(
    find "$addresses_root/$state_code" -type f -name adressen.csv.gz 2>/dev/null |
      LC_ALL=C sort |
      tail -n 1
  )
  if [ -z "$state_csv" ]; then
    echo "Skipping $state_code: no state export" >&2
    continue
  fi

  echo "Comparing $state_code with $state_csv" >&2
  "$tools/compare-keysets.sh" \
    "$basemap_state" "$state_csv" "$state_code" "$output"
  rm -rf "$output/work-$state_code"
done

node "$tools/summarize-comparisons.mjs" "$output" \
  > "$output/vergleich-zusammenfassung.md"
