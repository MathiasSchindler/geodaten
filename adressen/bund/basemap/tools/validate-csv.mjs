import { createReadStream } from "node:fs";
import process from "node:process";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

const EXPECTED_HEADER = [
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

const [inputPath, expectedRecordsText = ""] = process.argv.slice(2);
if (!inputPath) {
  console.error("Usage: node validate-csv.mjs <adressen.csv.gz> [records]");
  process.exit(2);
}
const expectedRecords = expectedRecordsText
  ? Number(expectedRecordsText)
  : null;

const parser = createReadStream(inputPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }));

let rowNumber = 0;
let records = 0;
let blankPostcode = 0;
let blankLocality = 0;
let blankStreet = 0;
let blankHouseNumber = 0;
let invalidCoordinates = 0;
const states = {};

for await (const row of parser) {
  rowNumber += 1;
  if (row.length !== EXPECTED_HEADER.length) {
    throw new Error(`Row ${rowNumber}: expected 14 columns, got ${row.length}`);
  }
  if (rowNumber === 1) {
    if (JSON.stringify(row) !== JSON.stringify(EXPECTED_HEADER)) {
      throw new Error(`Unexpected CSV header: ${JSON.stringify(row)}`);
    }
    continue;
  }

  records += 1;
  states[row[2]] = (states[row[2]] ?? 0) + 1;
  if (!row[3]) blankPostcode += 1;
  if (!row[4]) blankLocality += 1;
  if (!row[7]) blankStreet += 1;
  if (!row[8]) blankHouseNumber += 1;
  const longitude = Number(row[10]);
  const latitude = Number(row[11]);
  if (
    !Number.isFinite(longitude) ||
    !Number.isFinite(latitude) ||
    longitude < -180 ||
    longitude > 180 ||
    latitude < -90 ||
    latitude > 90
  ) {
    invalidCoordinates += 1;
  }
  if (!/^BKG-[0-9a-f]{24}$/.test(row[12])) {
    throw new Error(`Row ${rowNumber}: invalid derived record ID ${row[12]}`);
  }
}

if (expectedRecords != null && records !== expectedRecords) {
  throw new Error(`Expected ${expectedRecords} records, got ${records}`);
}

console.log(
  JSON.stringify(
    {
      input: inputPath,
      csv_columns: EXPECTED_HEADER.length,
      records,
      states: Object.fromEntries(
        Object.entries(states).sort(([a], [b]) => a.localeCompare(b)),
      ),
      blank_postcode: blankPostcode,
      blank_locality: blankLocality,
      blank_street: blankStreet,
      blank_house_number: blankHouseNumber,
      invalid_coordinates: invalidCoordinates,
    },
    null,
    2,
  ),
);
