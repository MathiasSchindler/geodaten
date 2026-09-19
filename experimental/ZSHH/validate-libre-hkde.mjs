import { createReadStream } from "node:fs";
import { writeFile } from "node:fs/promises";
import { createGunzip } from "node:zlib";
import readline from "node:readline";
import process from "node:process";

const [inputPath, outputPath] = process.argv.slice(2);
if (!inputPath || !outputPath) {
  console.error(
    "Usage: node validate-libre-hkde.mjs <libreHKDE.csv.gz> <report.json>",
  );
  process.exit(2);
}

const HEADER =
  "nba;oid;qua;landschl;land;regbezschl;regbez;kreisschl;kreis;gmdschl;gmd;ottschl;ott;strschl;str;hnr;adz;zone;ostwert;nordwert;postplz;postonm;postonmzus;postott";
const statistics = {
  records: 0,
  fields_per_record: 24,
  empty_oid: 0,
  empty_quality: 0,
  empty_municipality: 0,
  empty_postcode: 0,
  empty_postal_name: 0,
  empty_house_number_code: 0,
  invalid_utf8_replacement_characters: 0,
  minimum_easting: Infinity,
  maximum_easting: -Infinity,
  minimum_northing: Infinity,
  maximum_northing: -Infinity,
};

const input = readline.createInterface({
  input: createReadStream(inputPath).pipe(createGunzip()),
  crlfDelay: Infinity,
});

let lineNumber = 0;
for await (const line of input) {
  lineNumber += 1;
  if (lineNumber === 1) {
    if (line !== HEADER) throw new Error("Unexpected header");
    continue;
  }
  const fields = line.split(";");
  if (fields.length !== 24) {
    throw new Error(`Line ${lineNumber}: expected 24 fields, got ${fields.length}`);
  }
  const [
    nba,
    oid,
    quality,
    stateCode,
    stateName,
    governmentDistrictCode,
    governmentDistrictName,
    countyCode,
    countyName,
    municipalityCode,
    municipalityName,
    districtCode,
    ,
    streetCode,
    street,
    houseNumber,
    ,
    zone,
    easting,
    northing,
    postcode,
    postalName,
    postalNameSupplement,
    postalDistrict,
  ] = fields;
  if (
    nba !== "N" ||
    oid !== "" ||
    quality !== "" ||
    !/^\d{2}$/.test(stateCode) ||
    !stateName ||
    !/^\d$/.test(governmentDistrictCode) ||
    governmentDistrictName !== "" ||
    !/^\d{2}$/.test(countyCode) ||
    countyName !== "" ||
    !/^\d{3}$/.test(municipalityCode) ||
    !/^\d{4}$/.test(districtCode) ||
    !/^\d{5}$/.test(streetCode) ||
    !street ||
    !/^\d+$/.test(houseNumber) ||
    zone !== "32" ||
    !/^\d{6}\.\d{3}$/.test(easting) ||
    !/^\d{7}\.\d{3}$/.test(northing) ||
    (postcode && !/^\d{5}$/.test(postcode)) ||
    !postalName ||
    postalNameSupplement !== "" ||
    postalDistrict !== ""
  ) {
    throw new Error(`Line ${lineNumber}: invalid libreHKDE field`);
  }

  const numericEasting = Number(easting);
  const numericNorthing = Number(northing);
  statistics.records += 1;
  statistics.empty_oid += oid === "" ? 1 : 0;
  statistics.empty_quality += quality === "" ? 1 : 0;
  statistics.empty_municipality += municipalityName === "" ? 1 : 0;
  statistics.empty_postcode += postcode === "" ? 1 : 0;
  statistics.empty_postal_name += postalName === "" ? 1 : 0;
  statistics.empty_house_number_code += houseNumber === "0" ? 1 : 0;
  statistics.invalid_utf8_replacement_characters += line.includes("\ufffd") ? 1 : 0;
  statistics.minimum_easting = Math.min(
    statistics.minimum_easting,
    numericEasting,
  );
  statistics.maximum_easting = Math.max(
    statistics.maximum_easting,
    numericEasting,
  );
  statistics.minimum_northing = Math.min(
    statistics.minimum_northing,
    numericNorthing,
  );
  statistics.maximum_northing = Math.max(
    statistics.maximum_northing,
    numericNorthing,
  );
}

if (statistics.invalid_utf8_replacement_characters !== 0) {
  throw new Error("Output contains invalid UTF-8 replacement characters");
}
statistics.valid = true;
statistics.generated_at = new Date().toISOString();
await writeFile(outputPath, `${JSON.stringify(statistics, null, 2)}\n`);
console.log(JSON.stringify(statistics, null, 2));
