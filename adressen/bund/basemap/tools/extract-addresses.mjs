import { createHash } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import {
  access,
  mkdir,
  readFile,
  rename,
  unlink,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { pipeline } from "node:stream/promises";
import { createGzip } from "node:zlib";
import vtt from "vtt";

const TILE_URL =
  "https://sgx.geodatenzentrum.de/gdz_basemapde_vektor/tiles/v2/bm_web_de_3857/{z}/{x}/{y}.pbf";
const DEFAULT_CHUNK_SIZE = 500;
const DEFAULT_CONCURRENCY = 12;
const MAX_ATTEMPTS = 7;

const [tilesPath, outputDirectory] = process.argv.slice(2);
const chunkSize = Number(process.env.CHUNK_SIZE || DEFAULT_CHUNK_SIZE);
const concurrency = Number(process.env.CONCURRENCY || DEFAULT_CONCURRENCY);
const shardCount = Number(process.env.SHARD_COUNT || 1);
const shardIndex = Number(process.env.SHARD_INDEX || 0);

if (!tilesPath || !outputDirectory) {
  console.error(
    "Usage: node extract-addresses.mjs <tiles.txt> <output-directory>",
  );
  process.exit(2);
}

if (
  !Number.isInteger(chunkSize) ||
  chunkSize < 1 ||
  !Number.isInteger(concurrency) ||
  concurrency < 1 ||
  !Number.isInteger(shardCount) ||
  shardCount < 1 ||
  !Number.isInteger(shardIndex) ||
  shardIndex < 0 ||
  shardIndex >= shardCount
) {
  throw new Error(
    "CHUNK_SIZE, CONCURRENCY, SHARD_COUNT, and SHARD_INDEX are invalid",
  );
}

const chunksDirectory = path.join(outputDirectory, "chunks");
await mkdir(chunksDirectory, { recursive: true });

const tiles = (await readFile(tilesPath, "utf8"))
  .trim()
  .split("\n")
  .filter(Boolean)
  .map((line) => {
    const [z, x, y] = line.split("/").map(Number);
    if (![z, x, y].every(Number.isInteger) || z !== 15) {
      throw new Error(`Invalid tile coordinate: ${line}`);
    }
    return { z, x, y };
  });

function sanitize(value) {
  return String(value ?? "")
    .replaceAll("\t", " ")
    .replaceAll("\r", " ")
    .replaceAll("\n", " ");
}

function tilePointToWgs84(point, tile, extent) {
  const size = extent * 2 ** tile.z;
  const globalX = point[0] + extent * tile.x;
  const globalY = point[1] + extent * tile.y;
  const longitude = (globalX * 360) / size - 180;
  const latitude =
    (360 / Math.PI) *
      Math.atan(
        Math.exp((180 - (globalY * 360) / size) * (Math.PI / 180)),
      ) -
    90;
  return [longitude.toFixed(7), latitude.toFixed(7)];
}

async function fetchTile(tile) {
  const url = TILE_URL.replace("{z}", tile.z)
    .replace("{x}", tile.x)
    .replace("{y}", tile.y);

  for (let attempt = 1; attempt <= MAX_ATTEMPTS; attempt += 1) {
    try {
      const response = await fetch(url, {
        headers: {
          Accept: "application/octet-stream,application/x-protobuf",
          Referer: "https://basemap.de/",
          "User-Agent": "basemap-address-export/1.0",
        },
        signal: AbortSignal.timeout(60_000),
      });

      if (response.status === 404) {
        return { tile, rows: [], bytes: 0, status: 404 };
      }
      if (!response.ok) {
        throw new Error(`HTTP ${response.status} ${response.statusText}`);
      }

      const body = Buffer.from(await response.arrayBuffer());
      const layers = vtt.unpack(body);
      const addressLayer = layers.find((layer) => layer.name === "Adresse");
      if (!addressLayer) {
        return { tile, rows: [], bytes: body.length, status: response.status };
      }

      const rows = addressLayer.features.map((feature) => {
        const point = feature.geometry?.[0]?.[0];
        if (!point || point.length < 2) {
          throw new Error(
            `Address feature without point geometry in ${tile.z}/${tile.x}/${tile.y}`,
          );
        }
        const properties = feature.properties ?? {};
        const [longitude, latitude] = tilePointToWgs84(
          point,
          tile,
          addressLayer.extent,
        );
        return [
          sanitize(properties.land),
          sanitize(properties.ort),
          sanitize(properties.ortsteil),
          sanitize(properties.strasse),
          sanitize(properties.hausnummer),
          longitude,
          latitude,
        ].join("\t");
      });

      return { tile, rows, bytes: body.length, status: response.status };
    } catch (error) {
      if (attempt === MAX_ATTEMPTS) {
        throw new Error(
          `Failed ${tile.z}/${tile.x}/${tile.y} after ${MAX_ATTEMPTS} attempts: ${error.message}`,
          { cause: error },
        );
      }
      const delay = Math.min(30_000, 1_000 * 2 ** (attempt - 1));
      await new Promise((resolve) => setTimeout(resolve, delay));
    }
  }

  throw new Error("Unreachable retry state");
}

async function mapConcurrent(items, limit, mapper) {
  const results = new Array(items.length);
  let nextIndex = 0;

  async function worker() {
    while (nextIndex < items.length) {
      const index = nextIndex;
      nextIndex += 1;
      results[index] = await mapper(items[index]);
    }
  }

  await Promise.all(
    Array.from({ length: Math.min(limit, items.length) }, () => worker()),
  );
  return results;
}

async function exists(filePath) {
  try {
    await access(filePath);
    return true;
  } catch {
    return false;
  }
}

let totalRows = 0;
let totalBytes = 0;
let completedTiles = 0;
let emptyTiles = 0;
let notFoundTiles = 0;
const startedAt = new Date().toISOString();
const chunkCount = Math.ceil(tiles.length / chunkSize);

for (let chunkIndex = 0; chunkIndex < chunkCount; chunkIndex += 1) {
  if (chunkIndex % shardCount !== shardIndex) continue;
  const chunkName = String(chunkIndex).padStart(5, "0");
  const chunkPath = path.join(chunksDirectory, `${chunkName}.tsv.gz`);
  const statsPath = path.join(chunksDirectory, `${chunkName}.json`);

  if ((await exists(chunkPath)) && (await exists(statsPath))) {
    const stats = JSON.parse(await readFile(statsPath, "utf8"));
    totalRows += stats.records;
    totalBytes += stats.downloaded_bytes;
    completedTiles += stats.tiles;
    emptyTiles += stats.empty_tiles;
    notFoundTiles += stats.not_found_tiles;
    continue;
  }

  const chunkTiles = tiles.slice(
    chunkIndex * chunkSize,
    (chunkIndex + 1) * chunkSize,
  );
  const results = await mapConcurrent(chunkTiles, concurrency, fetchTile);
  const rows = results.flatMap((result) => result.rows);
  const downloadedBytes = results.reduce(
    (sum, result) => sum + result.bytes,
    0,
  );
  const chunkEmptyTiles = results.filter(
    (result) => result.status !== 404 && result.rows.length === 0,
  ).length;
  const chunkNotFoundTiles = results.filter(
    (result) => result.status === 404,
  ).length;

  const temporaryRaw = `${chunkPath}.tmp`;
  const temporaryGzip = `${chunkPath}.writing`;
  await writeFile(temporaryRaw, rows.length ? `${rows.join("\n")}\n` : "");
  await pipeline(
    createReadStream(temporaryRaw),
    createGzip({ level: 9, mtime: 0 }),
    createWriteStream(temporaryGzip),
  );
  await rename(temporaryGzip, chunkPath);
  await unlink(temporaryRaw);

  const chunkStats = {
    chunk: chunkIndex,
    first_tile: `${chunkTiles[0].z}/${chunkTiles[0].x}/${chunkTiles[0].y}`,
    last_tile: `${chunkTiles.at(-1).z}/${chunkTiles.at(-1).x}/${chunkTiles.at(-1).y}`,
    tiles: chunkTiles.length,
    empty_tiles: chunkEmptyTiles,
    not_found_tiles: chunkNotFoundTiles,
    records: rows.length,
    downloaded_bytes: downloadedBytes,
    sha256: createHash("sha256")
      .update(await readFile(chunkPath))
      .digest("hex"),
  };
  await writeFile(statsPath, `${JSON.stringify(chunkStats, null, 2)}\n`);

  totalRows += rows.length;
  totalBytes += downloadedBytes;
  completedTiles += chunkTiles.length;
  emptyTiles += chunkEmptyTiles;
  notFoundTiles += chunkNotFoundTiles;

  const elapsedSeconds =
    (Date.now() - new Date(startedAt).getTime()) / 1_000 || 1;
  console.error(
    JSON.stringify({
      chunk: chunkIndex + 1,
      chunks: chunkCount,
      completed_tiles: completedTiles,
      total_tiles: tiles.length,
      records: totalRows,
      downloaded_mib: Number((totalBytes / 1024 / 1024).toFixed(1)),
      tiles_per_second: Number((completedTiles / elapsedSeconds).toFixed(2)),
    }),
  );
}

const summary = {
  started_at: startedAt,
  completed_at: new Date().toISOString(),
  tile_url: TILE_URL,
  planned_tiles: tiles.length,
  completed_tiles: completedTiles,
  empty_tiles: emptyTiles,
  not_found_tiles: notFoundTiles,
  records_before_exact_deduplication: totalRows,
  downloaded_bytes: totalBytes,
  chunk_size: chunkSize,
  concurrency,
  shard_count: shardCount,
  shard_index: shardIndex,
};
await writeFile(
  path.join(
    outputDirectory,
    shardCount === 1
      ? "extraction-statistics.json"
      : `extraction-statistics-worker-${shardIndex}.json`,
  ),
  `${JSON.stringify(summary, null, 2)}\n`,
);
console.log(JSON.stringify(summary, null, 2));
