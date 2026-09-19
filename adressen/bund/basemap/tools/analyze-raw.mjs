import { createReadStream } from "node:fs";
import process from "node:process";
import readline from "node:readline";

const [inputPath] = process.argv.slice(2);
if (!inputPath) {
  console.error("Usage: node analyze-raw.mjs <sorted-raw.tsv>");
  process.exit(2);
}

const states = {};
let records = 0;
let blankLocality = 0;
let blankDistrict = 0;
let blankStreet = 0;
let blankHouseNumber = 0;
let invalidCoordinates = 0;
let minimumLongitude = Infinity;
let maximumLongitude = -Infinity;
let minimumLatitude = Infinity;
let maximumLatitude = -Infinity;
let previousHouseKey = null;
let currentHouseKeyCount = 0;
let duplicateHouseKeys = 0;
let duplicateHouseKeyOccurrences = 0;

function finishHouseKey() {
  if (currentHouseKeyCount > 1) {
    duplicateHouseKeys += 1;
    duplicateHouseKeyOccurrences += currentHouseKeyCount - 1;
  }
}

const input = readline.createInterface({
  input: createReadStream(inputPath),
  crlfDelay: Infinity,
});

for await (const line of input) {
  if (!line) continue;
  const fields = line.split("\t");
  if (fields.length !== 7) {
    throw new Error(`Expected 7 fields, got ${fields.length}`);
  }

  const [state, locality, district, street, houseNumber, lonText, latText] =
    fields;
  const longitude = Number(lonText);
  const latitude = Number(latText);
  records += 1;
  states[state] = (states[state] ?? 0) + 1;
  if (!locality) blankLocality += 1;
  if (!district) blankDistrict += 1;
  if (!street) blankStreet += 1;
  if (!houseNumber) blankHouseNumber += 1;
  if (
    !Number.isFinite(longitude) ||
    !Number.isFinite(latitude) ||
    longitude < -180 ||
    longitude > 180 ||
    latitude < -90 ||
    latitude > 90
  ) {
    invalidCoordinates += 1;
  } else {
    minimumLongitude = Math.min(minimumLongitude, longitude);
    maximumLongitude = Math.max(maximumLongitude, longitude);
    minimumLatitude = Math.min(minimumLatitude, latitude);
    maximumLatitude = Math.max(maximumLatitude, latitude);
  }

  const houseKey = fields.slice(0, 5).join("\t");
  if (houseKey === previousHouseKey) {
    currentHouseKeyCount += 1;
  } else {
    finishHouseKey();
    previousHouseKey = houseKey;
    currentHouseKeyCount = 1;
  }
}
finishHouseKey();

console.log(
  JSON.stringify(
    {
      input: inputPath,
      records,
      states: Object.fromEntries(
        Object.entries(states).sort(([a], [b]) => a.localeCompare(b)),
      ),
      blank_locality: blankLocality,
      blank_district: blankDistrict,
      blank_street: blankStreet,
      blank_house_number: blankHouseNumber,
      invalid_coordinates: invalidCoordinates,
      longitude_min: Number.isFinite(minimumLongitude)
        ? minimumLongitude
        : null,
      longitude_max: Number.isFinite(maximumLongitude)
        ? maximumLongitude
        : null,
      latitude_min: Number.isFinite(minimumLatitude) ? minimumLatitude : null,
      latitude_max: Number.isFinite(maximumLatitude) ? maximumLatitude : null,
      duplicate_house_keys: duplicateHouseKeys,
      duplicate_house_key_occurrences: duplicateHouseKeyOccurrences,
    },
    null,
    2,
  ),
);
