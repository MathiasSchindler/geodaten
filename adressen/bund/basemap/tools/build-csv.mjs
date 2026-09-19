import { createHash } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import process from "node:process";
import readline from "node:readline";

const STATE_NAMES = {
  BB: "Brandenburg",
  BE: "Berlin",
  BW: "Baden-Württemberg",
  BY: "Bayern",
  HB: "Bremen",
  HE: "Hessen",
  HH: "Hamburg",
  MV: "Mecklenburg-Vorpommern",
  NI: "Niedersachsen",
  NW: "Nordrhein-Westfalen",
  RP: "Rheinland-Pfalz",
  SH: "Schleswig-Holstein",
  SL: "Saarland",
  SN: "Sachsen",
  ST: "Sachsen-Anhalt",
  TH: "Thüringen",
};

const STATE_CODES = {
  SH: "01",
  HH: "02",
  NI: "03",
  HB: "04",
  NW: "05",
  HE: "06",
  RP: "07",
  BW: "08",
  BY: "09",
  SL: "10",
  BE: "11",
  BB: "12",
  MV: "13",
  SN: "14",
  ST: "15",
  TH: "16",
};

const HEADER = [
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

const [inputPath, outputPath, onlyState = ""] = process.argv.slice(2);
if (!inputPath || !outputPath) {
  console.error(
    "Usage: node build-csv.mjs <sorted-raw.tsv> <output.csv> [state-code]",
  );
  process.exit(2);
}
if (onlyState && !STATE_NAMES[onlyState]) {
  throw new Error(`Unknown state code: ${onlyState}`);
}

function csv(value) {
  const text = String(value ?? "");
  return `"${text.replaceAll('"', '""')}"`;
}

function splitHouseNumber(value) {
  const match = value.match(/^(\d+)(.*)$/u);
  if (!match) {
    return [value, ""];
  }
  return [match[1], match[2].trim()];
}

const input = readline.createInterface({
  input: createReadStream(inputPath),
  crlfDelay: Infinity,
});
const output = createWriteStream(outputPath);
output.write(`${HEADER.map(csv).join(",")}\n`);

let records = 0;
let blankHouseNumbers = 0;
let unknownStates = 0;

for await (const line of input) {
  if (!line) continue;
  const fields = line.split("\t");
  if (fields.length !== 7) {
    throw new Error(`Expected 7 fields, got ${fields.length}: ${line}`);
  }

  const [state, locality, district, street, sourceHouseNumber, lon, lat] =
    fields;
  if (onlyState && state !== onlyState) continue;
  if (!STATE_NAMES[state]) {
    unknownStates += 1;
    throw new Error(`Unknown state code in source: ${state}`);
  }

  const [houseNumber, houseNumberSuffix] =
    splitHouseNumber(sourceHouseNumber);
  if (!sourceHouseNumber) blankHouseNumbers += 1;

  const displayAddress =
    `${street}${sourceHouseNumber ? ` ${sourceHouseNumber}` : ""}, ${locality}`;
  const houseKey = [
    state,
    locality,
    district,
    street,
    sourceHouseNumber,
  ].join(";");
  const recordId = `BKG-${createHash("sha256")
    .update(
      [
        state,
        locality,
        district,
        street,
        sourceHouseNumber,
        lon,
        lat,
      ].join("\u001f"),
    )
    .digest("hex")
    .slice(0, 24)}`;

  const row = [
    displayAddress,
    STATE_NAMES[state],
    STATE_CODES[state],
    "",
    locality,
    "",
    district,
    street,
    houseNumber,
    houseNumberSuffix,
    lon,
    lat,
    recordId,
    houseKey,
  ];
  output.write(`${row.map(csv).join(",")}\n`);
  records += 1;
}

await new Promise((resolve, reject) => {
  output.end(resolve);
  output.on("error", reject);
});

console.log(
  JSON.stringify(
    {
      input: inputPath,
      output: outputPath,
      state_filter: onlyState || null,
      records,
      blank_house_numbers: blankHouseNumbers,
      unknown_states: unknownStates,
    },
    null,
    2,
  ),
);
