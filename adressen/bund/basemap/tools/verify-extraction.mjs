import { createHash } from "node:crypto";
import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import process from "node:process";

const [tilesPath, snapshotDirectory] = process.argv.slice(2);
const minimumRecords = Number(process.env.MIN_RECORDS || 20_000_000);
if (!tilesPath || !snapshotDirectory) {
  console.error(
    "Usage: node verify-extraction.mjs <tiles.txt> <snapshot-directory>",
  );
  process.exit(2);
}

const plannedTiles = (await readFile(tilesPath, "utf8"))
  .split("\n")
  .filter(Boolean).length;
const chunksDirectory = path.join(snapshotDirectory, "chunks");
const names = await readdir(chunksDirectory);
const statFiles = names.filter((name) => /^\d{5}\.json$/.test(name)).sort();

let completedTiles = 0;
let records = 0;
let downloadedBytes = 0;
let emptyTiles = 0;
let notFoundTiles = 0;

for (let index = 0; index < statFiles.length; index += 1) {
  const expected = `${String(index).padStart(5, "0")}.json`;
  if (statFiles[index] !== expected) {
    throw new Error(
      `Missing or unexpected chunk stats: expected ${expected}, got ${statFiles[index]}`,
    );
  }
  const stats = JSON.parse(
    await readFile(path.join(chunksDirectory, statFiles[index]), "utf8"),
  );
  const gzipPath = path.join(
    chunksDirectory,
    statFiles[index].replace(/\.json$/, ".tsv.gz"),
  );
  const gzip = await readFile(gzipPath);
  const checksum = createHash("sha256").update(gzip).digest("hex");
  if (checksum !== stats.sha256) {
    throw new Error(`Checksum mismatch for ${gzipPath}`);
  }
  completedTiles += stats.tiles;
  records += stats.records;
  downloadedBytes += stats.downloaded_bytes;
  emptyTiles += stats.empty_tiles;
  notFoundTiles += stats.not_found_tiles;
}

if (completedTiles !== plannedTiles) {
  throw new Error(
    `Incomplete extraction: ${completedTiles} of ${plannedTiles} planned tiles`,
  );
}
if (records < minimumRecords) {
  throw new Error(
    `Implausibly small address result: ${records} records (expected at least ${minimumRecords})`,
  );
}

console.log(
  JSON.stringify(
    {
      planned_tiles: plannedTiles,
      verified_chunks: statFiles.length,
      completed_tiles: completedTiles,
      records_before_exact_deduplication: records,
      downloaded_bytes: downloadedBytes,
      empty_tiles: emptyTiles,
      not_found_tiles: notFoundTiles,
      all_chunk_checksums_valid: true,
      plausible_national_record_floor_met: true,
      minimum_expected_records: minimumRecords,
    },
    null,
    2,
  ),
);
