#!/bin/sh
set -eu

build_dir=${1:?build directory required}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work="$build_dir/libre-hkde-test"

mkdir -p "$work"
"$build_dir/libre-hkde" \
  "$root/tests/fixtures/libre_hkde_addresses.csv" \
  "$root/tests/fixtures/libre_hkde_municipalities.csv" \
  "$work/output.csv" \
  "$work/report.txt"

test "$(wc -l < "$work/output.csv" | tr -d ' ')" = 4
awk -F';' '
  NF != 24 { exit 1 }
  NR > 1 && ($2 != "" || $3 != "" || $18 != "32") { exit 1 }
' "$work/output.csv"
grep -Fq 'N;;;09;Bayern;1;;62;;000;München;0000;Altstadt-Lehel;00000;Marienplatz;1;;32;691603.777;5334757.804;80331;München;;' "$work/output.csv"
grep -Fq 'N;;;01;Schleswig-Holstein;0;;02;;000;Kiel;0000;;12345;Rathausplatz;1;ab;32;573813.023;6020087.016;24103;Kiel;;' "$work/output.csv"
grep -Fq 'N;;;13;Mecklenburg-Vorpommern;0;;00;;000;;0000;Warnemünde;00000;Am Wasser;0;ab;32;701526.254;6008154.068;;Rostock;;' "$work/output.csv"
cmp "$root/tests/expected/libre_hkde_report.txt" "$work/report.txt"
echo "libre-hkde fixture test passed"
