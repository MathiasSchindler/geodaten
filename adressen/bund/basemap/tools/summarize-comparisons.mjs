import { readdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";

const [directory] = process.argv.slice(2);
if (!directory) {
  console.error("Usage: node summarize-comparisons.mjs <comparison-directory>");
  process.exit(2);
}

const files = (await readdir(directory))
  .filter((name) => /^[A-Z]{2}\.json$/.test(name))
  .sort();
const results = await Promise.all(
  files.map(async (name) =>
    JSON.parse(await readFile(path.join(directory, name), "utf8")),
  ),
);

await writeFile(
  path.join(directory, "vergleich-gesamt.json"),
  `${JSON.stringify(results, null, 2)}\n`,
);

function percent(value) {
  return value == null ? "–" : `${(value * 100).toFixed(2)} %`;
}

const lines = [
  "# Vergleich basemap.de mit den Länderexporten",
  "",
  "Verglichen werden eindeutige normalisierte Schlüssel aus Ort, Straße und",
  "Hausnummer. Beim Länderexport gilt ein Treffer wahlweise über den",
  "postalischen Ortsnamen oder den Gemeindenamen. Die Koordinatenstatistik",
  "verwendet für jeden passenden basemap-Datensatz die nächstgelegene",
  "Koordinate derselben normalisierten Adresse.",
  "",
  "| Land | basemap eindeutig | Land eindeutig | Treffer | Anteil basemap | Anteil Land | ≤ 5 m | ≤ 25 m |",
  "|---|---:|---:|---:|---:|---:|---:|---:|",
];

for (const result of results) {
  const coordinates = result.coordinate_agreement;
  const matchedCoordinates = coordinates.matched_basemap_records;
  lines.push(
    `| ${result.state_code} | ${result.basemap_unique_normalized_addresses.toLocaleString("de-DE")} | ` +
      `${result.state_unique_normalized_addresses_either_locality.toLocaleString("de-DE")} | ` +
      `${result.matched_either_locality.toLocaleString("de-DE")} | ` +
      `${percent(result.basemap_match_rate_either_locality)} | ` +
      `${percent(result.state_match_rate_either_locality)} | ` +
      `${percent(matchedCoordinates ? coordinates.within_5_metres / matchedCoordinates : null)} | ` +
      `${percent(matchedCoordinates ? coordinates.within_25_metres / matchedCoordinates : null)} |`,
  );
}

lines.push("");
console.log(lines.join("\n"));
