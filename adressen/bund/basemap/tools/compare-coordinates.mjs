import { createReadStream } from "node:fs";
import process from "node:process";
import readline from "node:readline";

const [basemapPath, statePath] = process.argv.slice(2);
if (!basemapPath || !statePath) {
  console.error(
    "Usage: node compare-coordinates.mjs <basemap.coords.sorted> <state.coords.sorted>",
  );
  process.exit(2);
}

function parseLine(line) {
  const fields = line.split("\t");
  if (fields.length !== 5) {
    throw new Error(`Expected 5 fields, got ${fields.length}`);
  }
  return {
    key: fields.slice(0, 3).join("\t"),
    longitude: Number(fields[3]),
    latitude: Number(fields[4]),
  };
}

async function nextGroup(iterator, pending) {
  const first = pending.value ?? (await iterator.next());
  if (first.done) return { group: null, pending: { done: true } };

  const item = parseLine(first.value);
  const points = [item];
  while (true) {
    const next = await iterator.next();
    if (next.done) {
      return { group: { key: item.key, points }, pending: next };
    }
    const parsed = parseLine(next.value);
    if (parsed.key !== item.key) {
      return {
        group: { key: item.key, points },
        pending: { value: next },
      };
    }
    points.push(parsed);
  }
}

function distanceMeters(a, b) {
  const radians = Math.PI / 180;
  const lat1 = a.latitude * radians;
  const lat2 = b.latitude * radians;
  const deltaLat = (b.latitude - a.latitude) * radians;
  const deltaLon = (b.longitude - a.longitude) * radians;
  const haversine =
    Math.sin(deltaLat / 2) ** 2 +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(deltaLon / 2) ** 2;
  return 6_371_008.8 * 2 * Math.asin(Math.min(1, Math.sqrt(haversine)));
}

const basemapLines = readline
  .createInterface({
    input: createReadStream(basemapPath),
    crlfDelay: Infinity,
  })
  [Symbol.asyncIterator]();
const stateLines = readline
  .createInterface({
    input: createReadStream(statePath),
    crlfDelay: Infinity,
  })
  [Symbol.asyncIterator]();

let basemapPending = {};
let statePending = {};
let basemapResult = await nextGroup(basemapLines, basemapPending);
let stateResult = await nextGroup(stateLines, statePending);
let matchedKeys = 0;
let matchedBasemapRecords = 0;
let within1m = 0;
let within5m = 0;
let within25m = 0;
let within100m = 0;
let distanceSum = 0;
let maximumDistance = 0;

while (basemapResult.group && stateResult.group) {
  const comparison =
    basemapResult.group.key < stateResult.group.key
      ? -1
      : basemapResult.group.key > stateResult.group.key
        ? 1
        : 0;
  if (comparison < 0) {
    basemapPending = basemapResult.pending;
    basemapResult = await nextGroup(basemapLines, basemapPending);
    continue;
  }
  if (comparison > 0) {
    statePending = stateResult.pending;
    stateResult = await nextGroup(stateLines, statePending);
    continue;
  }

  matchedKeys += 1;
  for (const basemapPoint of basemapResult.group.points) {
    let minimumDistance = Infinity;
    for (const statePoint of stateResult.group.points) {
      minimumDistance = Math.min(
        minimumDistance,
        distanceMeters(basemapPoint, statePoint),
      );
    }
    matchedBasemapRecords += 1;
    distanceSum += minimumDistance;
    maximumDistance = Math.max(maximumDistance, minimumDistance);
    if (minimumDistance <= 1) within1m += 1;
    if (minimumDistance <= 5) within5m += 1;
    if (minimumDistance <= 25) within25m += 1;
    if (minimumDistance <= 100) within100m += 1;
  }

  basemapPending = basemapResult.pending;
  statePending = stateResult.pending;
  basemapResult = await nextGroup(basemapLines, basemapPending);
  stateResult = await nextGroup(stateLines, statePending);
}

console.log(
  JSON.stringify(
    {
      matched_normalized_keys: matchedKeys,
      matched_basemap_records: matchedBasemapRecords,
      within_1_metre: within1m,
      within_5_metres: within5m,
      within_25_metres: within25m,
      within_100_metres: within100m,
      mean_distance_metres:
        matchedBasemapRecords > 0 ? distanceSum / matchedBasemapRecords : null,
      maximum_distance_metres:
        matchedBasemapRecords > 0 ? maximumDistance : null,
    },
    null,
    2,
  ),
);
