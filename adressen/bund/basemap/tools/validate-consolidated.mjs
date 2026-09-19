import { createReadStream, createWriteStream } from "node:fs";
import { once } from "node:events";
import process from "node:process";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

const MAIN_HEADER = [
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
const PROVENANCE_HEADER = [
  "bund_id",
  "bundesland_code",
  "quellen",
  "land_datensatznummer",
  "land_hausschluessel",
  "basemap_datensatznummer",
  "lizenzen",
  "abgleich_status",
  "abgleich_regel",
  "abgleich_distanz_meter",
  "plz_herkunft",
  "koordinaten_herkunft",
  "ortsteil_herkunft",
  "qualitaetshinweise",
];
const CONFLICT_HEADER = [
  "bundesland_code",
  "basemap_datensatznummer",
  "ort",
  "strasse",
  "hausnummer",
  "longitude_wgs84",
  "latitude_wgs84",
  "abgleich_stufe",
  "kandidatenanzahl",
  "kandidaten_land_datensatznummern",
];
const STATE_CODES = {
  SH: ["Schleswig-Holstein", "01"],
  HH: ["Hamburg", "02"],
  NI: ["Niedersachsen", "03"],
  HB: ["Bremen", "04"],
  NW: ["Nordrhein-Westfalen", "05"],
  HE: ["Hessen", "06"],
  RP: ["Rheinland-Pfalz", "07"],
  BW: ["Baden-Württemberg", "08"],
  BY: ["Bayern", "09"],
  SL: ["Saarland", "10"],
  BE: ["Berlin", "11"],
  BB: ["Brandenburg", "12"],
  MV: ["Mecklenburg-Vorpommern", "13"],
  SN: ["Sachsen", "14"],
  ST: ["Sachsen-Anhalt", "15"],
  TH: ["Thüringen", "16"],
};
const CODE_BY_KEY = Object.fromEntries(
  Object.entries(STATE_CODES).map(([code, [, key]]) => [key, code]),
);
const STATE_LICENSE = {
  BB: "DL-DE-BY-2.0",
  BE: "DL-DE-BY-2.0",
  BW: "DL-DE-BY-2.0",
  HB: "CC-BY-4.0",
  HE: "DL-DE-ZERO-2.0",
  HH: "DL-DE-BY-2.0",
  NI: "CC-BY-4.0",
  NW: "DL-DE-ZERO-2.0",
  RP: "DL-DE-BY-2.0",
  SH: "CC-BY-4.0",
  SL: "DL-DE-BY-2.0",
  SN: "DL-DE-BY-2.0",
  ST: "DL-DE-BY-2.0",
  TH: "DL-DE-BY-2.0",
};
const POSTCODE_STATES = new Set(["BB", "BE", "HB", "HH", "NW", "SH", "SL", "SN"]);

const [mainPath, provenancePath, conflictPath, mainIdsPath, provenanceIdsPath] =
  process.argv.slice(2);
if (
  !mainPath ||
  !provenancePath ||
  !conflictPath ||
  !mainIdsPath ||
  !provenanceIdsPath
) {
  console.error(
    "Usage: node validate-consolidated.mjs <adressen.csv.gz> <provenienz.csv.gz> <konflikte.csv.gz> <main-ids> <provenance-ids>",
  );
  process.exit(2);
}

function parserFor(inputPath) {
  return createReadStream(inputPath)
    .pipe(createGunzip())
    .pipe(parse({ bom: true, relax_quotes: false }));
}

function assertHeader(actual, expected, label) {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error(`${label}: unexpected header ${JSON.stringify(actual)}`);
  }
}

function assertCoordinate(longitudeText, latitudeText, label) {
  const longitude = Number(longitudeText);
  const latitude = Number(latitudeText);
  if (
    !Number.isFinite(longitude) ||
    !Number.isFinite(latitude) ||
    longitude < 5 ||
    longitude > 16 ||
    latitude < 47 ||
    latitude > 56
  ) {
    throw new Error(`${label}: invalid coordinate ${longitudeText},${latitudeText}`);
  }
}

async function writeLine(stream, value) {
  if (!stream.write(`${value}\n`)) {
    await once(stream, "drain");
  }
}

async function close(stream) {
  stream.end();
  await once(stream, "finish");
}

const mainIds = createWriteStream(mainIdsPath);
const mainByState = {};
let mainRecords = 0;
let recordsWithPostcode = 0;
let rowNumber = 0;
for await (const row of parserFor(mainPath)) {
  rowNumber += 1;
  if (rowNumber === 1) {
    assertHeader(row, MAIN_HEADER, "adressen.csv.gz");
    continue;
  }
  if (row.length !== MAIN_HEADER.length) {
    throw new Error(`Main row ${rowNumber}: expected 14 columns, got ${row.length}`);
  }
  const state = CODE_BY_KEY[row[2]];
  if (!state || STATE_CODES[state][0] !== row[1]) {
    throw new Error(`Main row ${rowNumber}: invalid state ${row[1]} / ${row[2]}`);
  }
  if (!/^BUND-[0-9a-f]{24}$/.test(row[12])) {
    throw new Error(`Main row ${rowNumber}: invalid ID ${row[12]}`);
  }
  if (row[3] && !/^\d{5}$/.test(row[3])) {
    throw new Error(`Main row ${rowNumber}: invalid postcode ${row[3]}`);
  }
  assertCoordinate(row[10], row[11], `Main row ${rowNumber}`);
  mainRecords += 1;
  mainByState[state] = (mainByState[state] ?? 0) + 1;
  if (row[3]) recordsWithPostcode += 1;
  await writeLine(mainIds, row[12]);
}
await close(mainIds);

const provenanceIds = createWriteStream(provenanceIdsPath);
const provenanceByState = {};
let provenanceRecords = 0;
let provenanceWithPostcode = 0;
let previousId = "";
rowNumber = 0;
for await (const row of parserFor(provenancePath)) {
  rowNumber += 1;
  if (rowNumber === 1) {
    assertHeader(row, PROVENANCE_HEADER, "provenienz.csv.gz");
    continue;
  }
  if (row.length !== PROVENANCE_HEADER.length) {
    throw new Error(
      `Provenance row ${rowNumber}: expected 14 columns, got ${row.length}`,
    );
  }
  const [id, state, sources, stateId, , basemapId, licenses, status, rule, distance, postcodeSource, coordinateSource, districtSource] =
    row;
  if (!/^BUND-[0-9a-f]{24}$/.test(id) || !STATE_CODES[state]) {
    throw new Error(`Provenance row ${rowNumber}: invalid ID or state`);
  }
  if (previousId && id <= previousId) {
    throw new Error(`Provenance row ${rowNumber}: IDs are not unique and sorted`);
  }
  previousId = id;
  const stateSource = `LAND-${state}`;
  const stateLicense = STATE_LICENSE[state];
  if (status === "nur_basemap") {
    if (
      sources !== "BASEMAP" ||
      licenses !== "CC-BY-4.0" ||
      stateId ||
      !/^BKG-[0-9a-f]{24}$/.test(basemapId) ||
      rule ||
      distance ||
      postcodeSource ||
      coordinateSource !== "BASEMAP" ||
      !["", "BASEMAP"].includes(districtSource)
    ) {
      throw new Error(`Provenance row ${rowNumber}: invalid basemap-only provenance`);
    }
  } else if (status === "mehrdeutig_nicht_vereinigt") {
    if (
      sources !== "BASEMAP" ||
      licenses !== "CC-BY-4.0" ||
      stateId ||
      !/^BKG-[0-9a-f]{24}$/.test(basemapId) ||
      !["ort_strasse_hausnummer_25m", "strasse_hausnummer_5m"].includes(rule) ||
      !Number.isFinite(Number(distance)) ||
      Number(distance) < 0 ||
      Number(distance) > (rule === "strasse_hausnummer_5m" ? 5 : 25) ||
      postcodeSource ||
      coordinateSource !== "BASEMAP" ||
      !["", "BASEMAP"].includes(districtSource)
    ) {
      throw new Error(`Provenance row ${rowNumber}: invalid ambiguous provenance`);
    }
  } else if (status === "nur_land") {
    if (
      !stateLicense ||
      sources !== stateSource ||
      licenses !== stateLicense ||
      !stateId ||
      basemapId ||
      rule ||
      distance ||
      coordinateSource !== stateSource
    ) {
      throw new Error(`Provenance row ${rowNumber}: invalid state-only provenance`);
    }
  } else if (status === "land_und_basemap") {
    if (
      !stateLicense ||
      sources !== `${stateSource}|BASEMAP` ||
      licenses !== `${stateLicense}|CC-BY-4.0` ||
      !stateId ||
      !/^BKG-[0-9a-f]{24}$/.test(basemapId) ||
      !["ort_strasse_hausnummer_25m", "strasse_hausnummer_5m"].includes(rule) ||
      !Number.isFinite(Number(distance)) ||
      Number(distance) < 0 ||
      Number(distance) > (rule === "strasse_hausnummer_5m" ? 5 : 25) ||
      ![stateSource, "BASEMAP"].includes(coordinateSource) ||
      !["", stateSource, "BASEMAP"].includes(districtSource)
    ) {
      throw new Error(`Provenance row ${rowNumber}: invalid merged provenance`);
    }
  } else {
    throw new Error(`Provenance row ${rowNumber}: invalid status ${status}`);
  }
  if (["BY", "MV"].includes(state) && sources !== "BASEMAP") {
    throw new Error(`Provenance row ${rowNumber}: ${state} must use basemap only`);
  }
  if (postcodeSource) {
    if (postcodeSource !== stateSource || !POSTCODE_STATES.has(state)) {
      throw new Error(`Provenance row ${rowNumber}: invalid postcode source`);
    }
    provenanceWithPostcode += 1;
  }
  provenanceRecords += 1;
  provenanceByState[state] = (provenanceByState[state] ?? 0) + 1;
  await writeLine(provenanceIds, id);
}
await close(provenanceIds);

let conflictRecords = 0;
rowNumber = 0;
for await (const row of parserFor(conflictPath)) {
  rowNumber += 1;
  if (rowNumber === 1) {
    assertHeader(row, CONFLICT_HEADER, "konflikte.csv.gz");
    continue;
  }
  if (row.length !== CONFLICT_HEADER.length) {
    throw new Error(
      `Conflict row ${rowNumber}: expected 10 columns, got ${row.length}`,
    );
  }
  if (
    !STATE_CODES[row[0]] ||
    !/^BKG-[0-9a-f]{24}$/.test(row[1]) ||
    Number(row[8]) < 2 ||
    row[9].split("|").length !== Number(row[8])
  ) {
    throw new Error(`Conflict row ${rowNumber}: invalid conflict record`);
  }
  assertCoordinate(row[5], row[6], `Conflict row ${rowNumber}`);
  conflictRecords += 1;
}

const stateCountsMatch = Object.keys(STATE_CODES).every(
  (state) => mainByState[state] === provenanceByState[state],
);
if (
  mainRecords !== provenanceRecords ||
  recordsWithPostcode !== provenanceWithPostcode ||
  !stateCountsMatch
) {
  throw new Error(
    `Main and provenance aggregate counts differ: ${JSON.stringify({
      mainRecords,
      provenanceRecords,
      recordsWithPostcode,
      provenanceWithPostcode,
      mainByState,
      provenanceByState,
    })}`,
  );
}

console.log(
  JSON.stringify(
    {
      main_records: mainRecords,
      provenance_records: provenanceRecords,
      conflict_records: conflictRecords,
      records_with_postcode: recordsWithPostcode,
      records_without_postcode: mainRecords - recordsWithPostcode,
      states: mainByState,
      schemas_valid: true,
      ids_valid: true,
      coordinates_valid: true,
      provenance_rules_valid: true,
    },
    null,
    2,
  ),
);
