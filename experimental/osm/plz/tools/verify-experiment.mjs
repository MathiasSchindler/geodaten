import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { readFile } from "node:fs/promises";
import process from "node:process";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

const [assignmentsPath, addressesPath, statisticsPath, expectedRecordsText] =
  process.argv.slice(2);
if (!assignmentsPath || !addressesPath || !statisticsPath || !expectedRecordsText) {
  console.error(
    "Usage: node verify-experiment.mjs <assignments.csv.gz> <addresses.csv.gz> <statistics.json> <expected-records>",
  );
  process.exit(2);
}
const expectedRecords = Number(expectedRecordsText);
const expectedHeader = [
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
const statistics = JSON.parse(await readFile(statisticsPath, "utf8"));
const parser = createReadStream(assignmentsPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }));
const addressParser = createReadStream(addressesPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }))[Symbol.asyncIterator]();
await addressParser.next();
let records = 0;
let officialRecords = 0;
let matching = 0;
let conflicting = 0;
let noPolygon = 0;
let multiplePostcodes = 0;
let rowNumber = 0;
for await (const row of parser) {
  rowNumber += 1;
  if (rowNumber === 1) {
    if (JSON.stringify(row) !== JSON.stringify(expectedHeader)) {
      throw new Error(`Unexpected assignment header: ${JSON.stringify(row)}`);
    }
    continue;
  }
  if (row.length !== expectedHeader.length) {
    throw new Error(`Row ${rowNumber}: expected 9 columns, got ${row.length}`);
  }
  if (!/^BUND-[0-9a-f]{24}$/.test(row[0])) {
    throw new Error(`Row ${rowNumber}: invalid ID ${row[0]}`);
  }
  const address = await addressParser.next();
  if (address.done || address.value[12] !== row[0]) {
    throw new Error(`Row ${rowNumber}: assignment does not match address input`);
  }
  if (row[2] && !/^\d{5}$/.test(row[2])) {
    throw new Error(`Row ${rowNumber}: invalid official postcode`);
  }
  if (row[3] && !/^\d{5}$/.test(row[3])) {
    throw new Error(`Row ${rowNumber}: invalid OSM postcode`);
  }
  if (!["innenzelle", "grenzzelle"].includes(row[7])) {
    throw new Error(`Row ${rowNumber}: invalid location class`);
  }
  records += 1;
  if (row[2]) {
    officialRecords += 1;
    if (row[8] === "stimmt") matching += 1;
    else if (row[8] === "widerspruch") conflicting += 1;
    else if (row[8] === "kein_polygon") noPolygon += 1;
    else if (row[8] === "mehrere_plz") multiplePostcodes += 1;
    else throw new Error(`Row ${rowNumber}: invalid validation status`);
  } else if (row[8] !== "nicht_pruefbar") {
    throw new Error(`Row ${rowNumber}: unexpected validation status`);
  }
}
if (!(await addressParser.next()).done) {
  throw new Error("Address input contains additional records");
}
if (
  records !== expectedRecords ||
  records !== statistics.addresses ||
  officialRecords !== statistics.validation.official_records ||
  matching !== statistics.validation.matching ||
  conflicting !== statistics.validation.conflicting ||
  noPolygon !== statistics.validation.no_polygon ||
  multiplePostcodes !== statistics.validation.multiple_postcodes
) {
  throw new Error("Assignment and statistics counts differ");
}
const checksum = await new Promise((resolve, reject) => {
  const hash = createHash("sha256");
  createReadStream(assignmentsPath)
    .on("data", (chunk) => hash.update(chunk))
    .on("end", () => resolve(hash.digest("hex")))
    .on("error", reject);
});
console.log(
  JSON.stringify(
    {
      records,
      ids_match_address_input: true,
      official_records: officialRecords,
      matching,
      conflicting,
      no_polygon: noPolygon,
      multiple_postcodes: multiplePostcodes,
      assignment_sha256: checksum,
      validation_passed: true,
    },
    null,
    2,
  ),
);
