import { createReadStream, createWriteStream } from "node:fs";
import { writeFile } from "node:fs/promises";
import { once } from "node:events";
import process from "node:process";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

const [addressesPath, assignmentsPath, outputPath, statisticsPath] =
  process.argv.slice(2);
if (!addressesPath || !assignmentsPath || !outputPath || !statisticsPath) {
  console.error(
    "Usage: node build-enriched-addresses.mjs <addresses.csv.gz> <assignments.csv.gz> <output.csv> <statistics.json>",
  );
  process.exit(2);
}

const ADDRESS_HEADER = [
  "vollstaendige_adresse",
  "bundesland",
  "landesschluessel",
  "postleitzahl",
  "ortsname_post",
  "gemeindename",
  "ortsteilname",
  "strassenname",
  "hausnummer",
  "hausnummernzusatz",
  "longitude_wgs84",
  "latitude_wgs84",
  "datensatznummer",
  "hausschluessel",
];
const ASSIGNMENT_HEADER = [
  "bund_id",
  "bundesland",
  "amtliche_plz",
  "osm_plz",
  "osm_relationen",
  "polygon_treffer",
  "unterschiedliche_plz",
  "lageklasse",
  "validierung",
];

function csv(value) {
  const text = String(value ?? "");
  return `"${text.replaceAll('"', '""')}"`;
}

function displayAddress(row) {
  const houseNumber = `${row[8]}${row[9]}`;
  return `${row[7]}${houseNumber ? ` ${houseNumber}` : ""}, ${row[3]} ${row[4]}`;
}

const addresses = createReadStream(addressesPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }))[Symbol.asyncIterator]();
const assignments = createReadStream(assignmentsPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }))[Symbol.asyncIterator]();

const addressHeader = await addresses.next();
const assignmentHeader = await assignments.next();
if (
  addressHeader.done ||
  JSON.stringify(addressHeader.value) !== JSON.stringify(ADDRESS_HEADER) ||
  assignmentHeader.done ||
  JSON.stringify(assignmentHeader.value) !== JSON.stringify(ASSIGNMENT_HEADER)
) {
  throw new Error("Unexpected input header");
}

const output = createWriteStream(outputPath);
output.write(`${ADDRESS_HEADER.map(csv).join(",")}\n`);
const statistics = {
  records: 0,
  official_postcodes_preserved: 0,
  osm_postcodes_added: 0,
  unresolved_without_postcode: 0,
  records_with_postcode: 0,
  records_without_postcode: 0,
  by_state: {},
};

while (true) {
  const [addressResult, assignmentResult] = await Promise.all([
    addresses.next(),
    assignments.next(),
  ]);
  if (addressResult.done || assignmentResult.done) {
    if (addressResult.done !== assignmentResult.done) {
      throw new Error("Address and assignment inputs have different lengths");
    }
    break;
  }
  const row = addressResult.value;
  const assignment = assignmentResult.value;
  if (row.length !== ADDRESS_HEADER.length || row[12] !== assignment[0]) {
    throw new Error(`Input alignment mismatch at record ${statistics.records + 1}`);
  }
  const state = assignment[1];
  statistics.by_state[state] ??= {
    records: 0,
    official_postcodes_preserved: 0,
    osm_postcodes_added: 0,
    unresolved_without_postcode: 0,
  };
  const stateStatistics = statistics.by_state[state];
  statistics.records += 1;
  stateStatistics.records += 1;

  if (row[3]) {
    if (row[3] !== assignment[2]) {
      throw new Error(`Official postcode mismatch for ${row[12]}`);
    }
    statistics.official_postcodes_preserved += 1;
    stateStatistics.official_postcodes_preserved += 1;
  } else if (assignment[3] && Number(assignment[6]) === 1) {
    row[3] = assignment[3];
    row[0] = displayAddress(row);
    statistics.osm_postcodes_added += 1;
    stateStatistics.osm_postcodes_added += 1;
  } else {
    statistics.unresolved_without_postcode += 1;
    stateStatistics.unresolved_without_postcode += 1;
  }
  if (row[3]) statistics.records_with_postcode += 1;
  else statistics.records_without_postcode += 1;

  if (!output.write(`${row.map(csv).join(",")}\n`)) {
    await once(output, "drain");
  }
}

output.end();
await once(output, "finish");
await writeFile(statisticsPath, `${JSON.stringify(statistics, null, 2)}\n`);
