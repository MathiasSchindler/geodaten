import { createReadStream, createWriteStream } from "node:fs";
import { writeFile } from "node:fs/promises";
import { once } from "node:events";
import process from "node:process";
import { createGunzip, createGzip } from "node:zlib";
import { parse } from "csv-parse";

const [assignmentsPath, addressesPath, outputPath, statisticsPath] =
  process.argv.slice(2);
if (!assignmentsPath || !addressesPath || !outputPath || !statisticsPath) {
  console.error(
    "Usage: node analyze-disagreements.mjs <assignments.csv.gz> <addresses.csv.gz> <disagreements.csv.gz> <statistics.json>",
  );
  process.exit(2);
}

const OUTPUT_HEADER = [
  "bund_id",
  "bundesland",
  "vollstaendige_adresse",
  "amtliche_plz",
  "osm_plz",
  "osm_relationen",
  "longitude_wgs84",
  "latitude_wgs84",
  "polygon_treffer",
  "unterschiedliche_plz",
  "lageklasse",
  "validierung",
];

function csv(value) {
  const text = String(value ?? "");
  return `"${text.replaceAll('"', '""')}"`;
}

const assignments = createReadStream(assignmentsPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }))[Symbol.asyncIterator]();
const addresses = createReadStream(addressesPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }))[Symbol.asyncIterator]();
await assignments.next();
await addresses.next();

const gzip = createGzip({ level: 9, mtime: 0 });
const output = createWriteStream(outputPath);
gzip.pipe(output);
gzip.write(`${OUTPUT_HEADER.map(csv).join(",")}\n`);

const pairCounts = new Map();
const byState = {};
let records = 0;
while (true) {
  const [assignmentResult, addressResult] = await Promise.all([
    assignments.next(),
    addresses.next(),
  ]);
  if (assignmentResult.done || addressResult.done) {
    if (assignmentResult.done !== addressResult.done) {
      throw new Error("Assignment and address inputs have different lengths");
    }
    break;
  }
  const assignment = assignmentResult.value;
  const address = addressResult.value;
  if (assignment[0] !== address[12]) {
    throw new Error(`Input alignment mismatch at ${assignment[0]}`);
  }
  const distinctPostcodes = Number(assignment[6]);
  const category =
    distinctPostcodes === 0
      ? "kein_polygon"
      : distinctPostcodes > 1
        ? "mehrere_plz"
        : assignment[8] === "widerspruch"
          ? "widerspruch"
          : "";
  if (!category) {
    continue;
  }
  records += 1;
  const state = assignment[1];
  byState[state] ??= {
    records: 0,
    widerspruch: 0,
    kein_polygon: 0,
    mehrere_plz: 0,
    innenzelle: 0,
    grenzzelle: 0,
  };
  byState[state].records += 1;
  byState[state][category] += 1;
  byState[state][assignment[7]] += 1;
  const pairKey = [state, assignment[2], assignment[3], category].join("\t");
  pairCounts.set(pairKey, (pairCounts.get(pairKey) ?? 0) + 1);
  const row = [
    assignment[0],
    state,
    address[0],
    assignment[2],
    assignment[3],
    assignment[4],
    address[10],
    address[11],
    assignment[5],
    assignment[6],
    assignment[7],
    category,
  ];
  if (!gzip.write(`${row.map(csv).join(",")}\n`)) {
    await once(gzip, "drain");
  }
}
gzip.end();
await once(output, "finish");

const postcodePairs = [...pairCounts.entries()]
  .map(([key, count]) => {
    const [state, officialPostcode, osmPostcode, category] = key.split("\t");
    return { state, official_postcode: officialPostcode, osm_postcode: osmPostcode, category, count };
  })
  .sort(
    (a, b) =>
      b.count - a.count ||
      a.state.localeCompare(b.state) ||
      a.official_postcode.localeCompare(b.official_postcode) ||
      a.osm_postcode.localeCompare(b.osm_postcode),
  );

await writeFile(
  statisticsPath,
  `${JSON.stringify(
    {
      records,
      by_state: Object.fromEntries(
        Object.entries(byState).sort(([a], [b]) => a.localeCompare(b)),
      ),
      postcode_pairs: postcodePairs,
    },
    null,
    2,
  )}\n`,
);
