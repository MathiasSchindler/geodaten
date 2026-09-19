import { readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import tileCover from "@mapbox/tile-cover";

const [maskPath, outputPath] = process.argv.slice(2);

if (!maskPath || !outputPath) {
  console.error("Usage: node plan-tiles.mjs <germany.geojson> <tiles.txt>");
  process.exit(2);
}

const source = JSON.parse(await readFile(maskPath, "utf8"));
const geometries =
  source.type === "FeatureCollection"
    ? source.features.map((feature) => feature.geometry)
    : source.type === "Feature"
      ? [source.geometry]
      : [source];

const uniqueTiles = new Set();
for (const geometry of geometries) {
  for (const [x, y, z] of tileCover.tiles(geometry, {
    min_zoom: 15,
    max_zoom: 15,
  })) {
    uniqueTiles.add(`${z}/${x}/${y}`);
  }
}

const tiles = [...uniqueTiles].sort((a, b) => {
  const aa = a.split("/").map(Number);
  const bb = b.split("/").map(Number);
  return aa[1] - bb[1] || aa[2] - bb[2];
});

await writeFile(outputPath, `${tiles.join("\n")}\n`);
console.log(
  JSON.stringify(
    {
      mask: path.resolve(maskPath),
      zoom: 15,
      tiles: tiles.length,
      first: tiles.at(0),
      last: tiles.at(-1),
    },
    null,
    2,
  ),
);
