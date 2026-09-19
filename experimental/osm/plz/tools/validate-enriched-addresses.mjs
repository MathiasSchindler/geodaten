import { createReadStream } from "node:fs";
import process from "node:process";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

const [inputPath, expectedRecordsText, expectedWithPostcodeText] =
  process.argv.slice(2);
if (!inputPath || !expectedRecordsText || !expectedWithPostcodeText) {
  console.error(
    "Usage: node validate-enriched-addresses.mjs <adressen.csv.gz> <records> <records-with-postcode>",
  );
  process.exit(2);
}
const expectedRecords = Number(expectedRecordsText);
const expectedWithPostcode = Number(expectedWithPostcodeText);
const expectedHeader = [
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

let records = 0;
let recordsWithPostcode = 0;
let recordsWithoutPostcode = 0;
let invalidCoordinates = 0;
let rowNumber = 0;
const states = {};
const parser = createReadStream(inputPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }));
for await (const row of parser) {
  rowNumber += 1;
  if (rowNumber === 1) {
    if (JSON.stringify(row) !== JSON.stringify(expectedHeader)) {
      throw new Error(`Unexpected header: ${JSON.stringify(row)}`);
    }
    continue;
  }
  if (row.length !== expectedHeader.length) {
    throw new Error(`Row ${rowNumber}: expected 14 columns, got ${row.length}`);
  }
  if (!/^BUND-[0-9a-f]{24}$/.test(row[12])) {
    throw new Error(`Row ${rowNumber}: invalid ID ${row[12]}`);
  }
  if (row[3] && !/^\d{5}$/.test(row[3])) {
    throw new Error(`Row ${rowNumber}: invalid postcode ${row[3]}`);
  }
  if (row[3] && !row[0].includes(row[3])) {
    throw new Error(`Row ${rowNumber}: display address lacks postcode`);
  }
  const longitude = Number(row[10]);
  const latitude = Number(row[11]);
  if (
    !Number.isFinite(longitude) ||
    !Number.isFinite(latitude) ||
    longitude < 5 ||
    longitude > 16 ||
    latitude < 47 ||
    latitude > 56
  ) {
    invalidCoordinates += 1;
  }
  records += 1;
  states[row[2]] = (states[row[2]] ?? 0) + 1;
  if (row[3]) recordsWithPostcode += 1;
  else recordsWithoutPostcode += 1;
}
if (records !== expectedRecords || recordsWithPostcode !== expectedWithPostcode) {
  throw new Error(
    `Unexpected counts: ${records} records, ${recordsWithPostcode} with postcode`,
  );
}
if (invalidCoordinates !== 0) {
  throw new Error(`${invalidCoordinates} records have invalid coordinates`);
}
console.log(
  JSON.stringify(
    {
      records,
      records_with_postcode: recordsWithPostcode,
      records_without_postcode: recordsWithoutPostcode,
      invalid_coordinates: invalidCoordinates,
      states,
      validation_passed: true,
    },
    null,
    2,
  ),
);
