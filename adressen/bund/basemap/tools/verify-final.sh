#!/bin/bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 <snapshot-directory> <expected-records>" >&2
  exit 2
fi

snapshot=$1
expected_records=$2
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work="$snapshot/work"
archive="$snapshot/adressen.csv.gz"
final="$work/adressen.csv"
body="$work/adressen-sortiert-ohne-kopf.csv"

gzip -t "$archive"
cmp "$final" <(gzip -dc "$archive")
LC_ALL=C sort -c "$body"
cmp "$archive" <(gzip -n -9 -c "$final")

header_hash=$(
  {
    IFS= read -r header
    printf '%s\n' "$header"
    cat >/dev/null
  } < <(gzip -dc "$archive") |
    shasum -a 256 |
    awk '{print $1}'
)
[ "$header_hash" = "5c53123212b86420d14b7e1851c4644a6a3f63eb6b7334a78ae2772d04361ad2" ]

node "$tools/validate-csv.mjs" "$archive" "$expected_records" \
  > "$snapshot/validation-statistics.json"
