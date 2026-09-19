import { createReadStream, createWriteStream, readFileSync } from "node:fs";
import { once } from "node:events";
import { createGunzip, createGzip } from "node:zlib";
import process from "node:process";
import { parse } from "csv-parse";

const [addressesPath, shapePath, dbfPath, outputPath, reportPath] =
  process.argv.slice(2);

if (!addressesPath || !shapePath || !dbfPath || !outputPath || !reportPath) {
  console.error(
    "Usage: node assign-municipalities.mjs <adressen.csv.gz> <VG250_GEM.shp> <VG250_GEM.dbf> <assignments.csv.gz> <report.json>",
  );
  process.exit(2);
}

const GRID_SIZE = 20_000;
const OUTPUT_HEADER = [
  "bund_id",
  "ags",
  "gemeindename",
  "regbezschl",
  "kreisschl",
  "gmdschl",
  "zuordnung",
];

function readDbf(filePath) {
  const data = readFileSync(filePath);
  const recordCount = data.readUInt32LE(4);
  const headerLength = data.readUInt16LE(8);
  const recordLength = data.readUInt16LE(10);
  const fields = [];
  let fieldOffset = 1;

  for (let offset = 32; offset < headerLength - 1; offset += 32) {
    const terminator = data.indexOf(0, offset);
    const name = data.toString("ascii", offset, terminator);
    const length = data[offset + 16];
    fields.push({ name, offset: fieldOffset, length });
    fieldOffset += length;
  }

  function value(recordOffset, name) {
    const field = fields.find((candidate) => candidate.name === name);
    if (!field) throw new Error(`Missing DBF field ${name}`);
    return data
      .toString(
        "utf8",
        recordOffset + field.offset,
        recordOffset + field.offset + field.length,
      )
      .trim();
  }

  return Array.from({ length: recordCount }, (_, index) => {
    const offset = headerLength + index * recordLength;
    if (data[offset] === 0x2a) return null;
    return {
      ags: value(offset, "AGS"),
      name: value(offset, "GEN"),
      geometryFlag: value(offset, "GF"),
      stateCode: value(offset, "SN_L"),
      governmentDistrictCode: value(offset, "SN_R"),
      countyCode: value(offset, "SN_K"),
      municipalityCode: value(offset, "SN_G"),
    };
  });
}

function readShape(filePath, attributes) {
  const data = readFileSync(filePath);
  if (data.readInt32BE(0) !== 9994) throw new Error("Invalid SHP file code");
  if (data.readInt32LE(32) !== 5) {
    throw new Error(`Expected Polygon SHP, got type ${data.readInt32LE(32)}`);
  }

  const polygons = [];
  let offset = 100;
  let recordIndex = 0;
  while (offset < data.length) {
    const contentLength = data.readInt32BE(offset + 4) * 2;
    const contentOffset = offset + 8;
    const shapeType = data.readInt32LE(contentOffset);
    const attributesForRecord = attributes[recordIndex];
    if (shapeType !== 0 && shapeType !== 5) {
      throw new Error(`Unsupported SHP record type ${shapeType}`);
    }

    if (shapeType === 5 && attributesForRecord) {
      const minX = data.readDoubleLE(contentOffset + 4);
      const minY = data.readDoubleLE(contentOffset + 12);
      const maxX = data.readDoubleLE(contentOffset + 20);
      const maxY = data.readDoubleLE(contentOffset + 28);
      const partCount = data.readInt32LE(contentOffset + 36);
      const pointCount = data.readInt32LE(contentOffset + 40);
      const partsOffset = contentOffset + 44;
      const pointsOffset = partsOffset + partCount * 4;
      const partStarts = Array.from({ length: partCount + 1 }, (_, index) =>
        index === partCount
          ? pointCount
          : data.readInt32LE(partsOffset + index * 4),
      );
      const rings = [];
      for (let part = 0; part < partCount; part += 1) {
        const ring = [];
        for (
          let point = partStarts[part];
          point < partStarts[part + 1];
          point += 1
        ) {
          ring.push([
            data.readDoubleLE(pointsOffset + point * 16),
            data.readDoubleLE(pointsOffset + point * 16 + 8),
          ]);
        }
        rings.push(ring);
      }
      polygons.push({
        ...attributesForRecord,
        minX,
        minY,
        maxX,
        maxY,
        rings,
      });
    }

    recordIndex += 1;
    offset = contentOffset + contentLength;
  }

  if (recordIndex !== attributes.length) {
    throw new Error(
      `SHP/DBF record mismatch: ${recordIndex} != ${attributes.length}`,
    );
  }
  return polygons;
}

function wgs84ToUtm32(longitude, latitude) {
  const a = 6_378_137;
  const flattening = 1 / 298.257222101;
  const scale = 0.9996;
  const eccentricitySquared = flattening * (2 - flattening);
  const secondEccentricitySquared =
    eccentricitySquared / (1 - eccentricitySquared);
  const radians = Math.PI / 180;
  const phi = latitude * radians;
  const lambda = longitude * radians;
  const centralMeridian = 9 * radians;
  const sinPhi = Math.sin(phi);
  const cosPhi = Math.cos(phi);
  const tanPhi = Math.tan(phi);
  const n = a / Math.sqrt(1 - eccentricitySquared * sinPhi * sinPhi);
  const t = tanPhi * tanPhi;
  const c = secondEccentricitySquared * cosPhi * cosPhi;
  const alpha = cosPhi * (lambda - centralMeridian);
  const e4 = eccentricitySquared ** 2;
  const e6 = eccentricitySquared ** 3;
  const meridian =
    a *
    ((1 - eccentricitySquared / 4 - (3 * e4) / 64 - (5 * e6) / 256) *
      phi -
      ((3 * eccentricitySquared) / 8 +
        (3 * e4) / 32 +
        (45 * e6) / 1024) *
        Math.sin(2 * phi) +
      ((15 * e4) / 256 + (45 * e6) / 1024) * Math.sin(4 * phi) -
      ((35 * e6) / 3072) * Math.sin(6 * phi));

  return [
    500_000 +
      scale *
        n *
        (alpha +
          ((1 - t + c) * alpha ** 3) / 6 +
          ((5 -
            18 * t +
            t ** 2 +
            72 * c -
            58 * secondEccentricitySquared) *
            alpha ** 5) /
            120),
    scale *
      (meridian +
        n *
          tanPhi *
          (alpha ** 2 / 2 +
            ((5 - t + 9 * c + 4 * c ** 2) * alpha ** 4) / 24 +
            ((61 -
              58 * t +
              t ** 2 +
              600 * c -
              330 * secondEccentricitySquared) *
              alpha ** 6) /
              720)),
  ];
}

function gridKey(x, y) {
  return `${Math.floor(x / GRID_SIZE)},${Math.floor(y / GRID_SIZE)}`;
}

function buildGrid(polygons) {
  const grid = new Map();
  for (let index = 0; index < polygons.length; index += 1) {
    const polygon = polygons[index];
    const minGridX = Math.floor(polygon.minX / GRID_SIZE);
    const maxGridX = Math.floor(polygon.maxX / GRID_SIZE);
    const minGridY = Math.floor(polygon.minY / GRID_SIZE);
    const maxGridY = Math.floor(polygon.maxY / GRID_SIZE);
    for (let x = minGridX; x <= maxGridX; x += 1) {
      for (let y = minGridY; y <= maxGridY; y += 1) {
        const key = `${x},${y}`;
        const entries = grid.get(key);
        if (entries) entries.push(index);
        else grid.set(key, [index]);
      }
    }
  }
  return grid;
}

function pointOnSegment(x, y, x1, y1, x2, y2) {
  const cross = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1);
  const tolerance =
    1e-9 * Math.max(1, Math.abs(x2 - x1), Math.abs(y2 - y1));
  if (Math.abs(cross) > tolerance) return false;
  return (
    x >= Math.min(x1, x2) - tolerance &&
    x <= Math.max(x1, x2) + tolerance &&
    y >= Math.min(y1, y2) - tolerance &&
    y <= Math.max(y1, y2) + tolerance
  );
}

function pointInPolygon(x, y, polygon) {
  let inside = false;
  for (const ring of polygon.rings) {
    for (let current = 0, previous = ring.length - 1; current < ring.length; previous = current, current += 1) {
      const [x1, y1] = ring[previous];
      const [x2, y2] = ring[current];
      if (pointOnSegment(x, y, x1, y1, x2, y2)) return "boundary";
      if (
        (y1 > y) !== (y2 > y) &&
        x < ((x2 - x1) * (y - y1)) / (y2 - y1) + x1
      ) {
        inside = !inside;
      }
    }
  }
  return inside ? "inside" : "outside";
}

function squaredDistanceToSegment(x, y, x1, y1, x2, y2) {
  const deltaX = x2 - x1;
  const deltaY = y2 - y1;
  const lengthSquared = deltaX * deltaX + deltaY * deltaY;
  if (lengthSquared === 0) {
    return (x - x1) ** 2 + (y - y1) ** 2;
  }
  const ratio = Math.max(
    0,
    Math.min(1, ((x - x1) * deltaX + (y - y1) * deltaY) / lengthSquared),
  );
  const nearestX = x1 + ratio * deltaX;
  const nearestY = y1 + ratio * deltaY;
  return (x - nearestX) ** 2 + (y - nearestY) ** 2;
}

function squaredDistanceToBounds(x, y, polygon) {
  const deltaX =
    x < polygon.minX ? polygon.minX - x : x > polygon.maxX ? x - polygon.maxX : 0;
  const deltaY =
    y < polygon.minY ? polygon.minY - y : y > polygon.maxY ? y - polygon.maxY : 0;
  return deltaX * deltaX + deltaY * deltaY;
}

function nearestMunicipalities(x, y, state, polygons) {
  const candidates = polygons
    .filter(({ stateCode }) => stateCode === state)
    .map((polygon) => ({
      polygon,
      boundsDistanceSquared: squaredDistanceToBounds(x, y, polygon),
    }))
    .sort(
      (left, right) =>
        left.boundsDistanceSquared - right.boundsDistanceSquared,
    );
  const distances = new Map();
  let secondBestSquared = Number.POSITIVE_INFINITY;

  for (const { polygon, boundsDistanceSquared } of candidates) {
    if (boundsDistanceSquared > secondBestSquared) break;
    let distanceSquared = Number.POSITIVE_INFINITY;
    for (const ring of polygon.rings) {
      for (
        let current = 0, previous = ring.length - 1;
        current < ring.length;
        previous = current, current += 1
      ) {
        distanceSquared = Math.min(
          distanceSquared,
          squaredDistanceToSegment(
            x,
            y,
            ring[previous][0],
            ring[previous][1],
            ring[current][0],
            ring[current][1],
          ),
        );
      }
    }
    const previousDistance = distances.get(polygon.ags)?.distanceSquared;
    if (previousDistance === undefined || distanceSquared < previousDistance) {
      distances.set(polygon.ags, { polygon, distanceSquared });
    }
    const ordered = [...distances.values()].sort(
      (left, right) => left.distanceSquared - right.distanceSquared,
    );
    if (ordered.length >= 2) secondBestSquared = ordered[1].distanceSquared;
  }

  return [...distances.values()]
    .sort((left, right) => left.distanceSquared - right.distanceSquared)
    .slice(0, 2)
    .map(({ polygon, distanceSquared }) => ({
      polygon,
      distance: Math.sqrt(distanceSquared),
    }));
}

function csv(value) {
  const text = String(value ?? "");
  return `"${text.replaceAll('"', '""')}"`;
}

async function writeLine(stream, values) {
  if (!stream.write(`${values.map(csv).join(",")}\n`)) {
    await once(stream, "drain");
  }
}

const attributes = readDbf(dbfPath);
const polygons = readShape(shapePath, attributes);
const grid = buildGrid(polygons);
const output = createGzip({ level: 9, mtime: 0 });
output.pipe(createWriteStream(outputPath));
await writeLine(output, OUTPUT_HEADER);

const report = {
  source_records: 0,
  basemap_only_records: 0,
  unique_assignments: 0,
  boundary_assignments: 0,
  multiple_assignments: 0,
  no_assignment: 0,
  nearest_fallback_within_10m: 0,
  nearest_fallback_within_50m: 0,
  nearest_fallback_within_100m: 0,
  nearest_fallback_within_250m: 0,
  nearest_fallback_over_250m: 0,
  maximum_nearest_distance_meters: 0,
  locality_matches_municipality: 0,
  locality_differs_from_municipality: 0,
  vg250_polygons: polygons.length,
  vg250_land_polygons: polygons.filter(
    ({ geometryFlag }) => geometryFlag === "4",
  ).length,
  vg250_water_polygons: polygons.filter(
    ({ geometryFlag }) => geometryFlag === "2",
  ).length,
  vg250_unique_municipalities: new Set(polygons.map(({ ags }) => ags)).size,
  by_state: {},
  examples: {
    boundary: [],
    multiple: [],
    none: [],
    differing_names: [],
  },
};

function stateStats(state) {
  report.by_state[state] ??= {
    basemap_only_records: 0,
    unique_assignments: 0,
    boundary_assignments: 0,
    multiple_assignments: 0,
    no_assignment: 0,
  };
  return report.by_state[state];
}

function remember(category, value) {
  if (report.examples[category].length < 20) {
    report.examples[category].push(value);
  }
}

function normalizedName(value) {
  return value
    .toLocaleLowerCase("de-DE")
    .replaceAll("ä", "ae")
    .replaceAll("ö", "oe")
    .replaceAll("ü", "ue")
    .replaceAll("ß", "ss")
    .normalize("NFKD")
    .replace(/\p{M}/gu, "")
    .replace(/[^a-z0-9]+/gu, "");
}

const parser = createReadStream(addressesPath)
  .pipe(createGunzip())
  .pipe(parse({ columns: true, bom: true }));

for await (const record of parser) {
  report.source_records += 1;
  if (record.gemeindename) continue;
  report.basemap_only_records += 1;
  const state = record.landesschluessel;
  const stats = stateStats(state);
  stats.basemap_only_records += 1;
  const longitude = Number(record.longitude_wgs84);
  const latitude = Number(record.latitude_wgs84);
  if (!Number.isFinite(longitude) || !Number.isFinite(latitude)) {
    throw new Error(`Invalid coordinate for ${record.datensatznummer}`);
  }
  const [x, y] = wgs84ToUtm32(longitude, latitude);
  const candidates = grid.get(gridKey(x, y)) ?? [];
  const matches = [];
  let onBoundary = false;
  for (const index of candidates) {
    const polygon = polygons[index];
    if (
      polygon.stateCode !== state ||
      x < polygon.minX ||
      x > polygon.maxX ||
      y < polygon.minY ||
      y > polygon.maxY
    ) {
      continue;
    }
    const result = pointInPolygon(x, y, polygon);
    if (result !== "outside") {
      matches.push(polygon);
      if (result === "boundary") onBoundary = true;
    }
  }

  const uniqueByAgs = new Map(matches.map((match) => [match.ags, match]));
  const uniqueMatches = [...uniqueByAgs.values()];
  const example = {
    bund_id: record.datensatznummer,
    state,
    locality: record.ortsname_post,
    longitude,
    latitude,
    matches: uniqueMatches.map(({ ags, name }) => ({ ags, name })),
  };

  if (uniqueMatches.length === 0) {
    report.no_assignment += 1;
    stats.no_assignment += 1;
    const nearest = nearestMunicipalities(x, y, state, polygons);
    const nearestDistance = nearest[0]?.distance ?? Number.POSITIVE_INFINITY;
    report.maximum_nearest_distance_meters = Math.max(
      report.maximum_nearest_distance_meters,
      nearestDistance,
    );
    if (nearestDistance <= 10) report.nearest_fallback_within_10m += 1;
    if (nearestDistance <= 50) report.nearest_fallback_within_50m += 1;
    if (nearestDistance <= 100) report.nearest_fallback_within_100m += 1;
    if (nearestDistance <= 250) report.nearest_fallback_within_250m += 1;
    else report.nearest_fallback_over_250m += 1;
    example.nearest = nearest.map(({ polygon, distance }) => ({
      ags: polygon.ags,
      name: polygon.name,
      distance_meters: distance,
    }));
    remember("none", example);
    await writeLine(output, [record.datensatznummer, "", "", "", "", "", "keine"]);
    continue;
  }
  if (uniqueMatches.length > 1) {
    report.multiple_assignments += 1;
    stats.multiple_assignments += 1;
    remember("multiple", example);
    await writeLine(output, [record.datensatznummer, "", "", "", "", "", "mehrdeutig"]);
    continue;
  }

  const municipality = uniqueMatches[0];
  report.unique_assignments += 1;
  stats.unique_assignments += 1;
  if (onBoundary) {
    report.boundary_assignments += 1;
    stats.boundary_assignments += 1;
    remember("boundary", example);
  }
  if (
    normalizedName(record.ortsname_post) === normalizedName(municipality.name)
  ) {
    report.locality_matches_municipality += 1;
  } else {
    report.locality_differs_from_municipality += 1;
    remember("differing_names", example);
  }
  await writeLine(output, [
    record.datensatznummer,
    municipality.ags,
    municipality.name,
    municipality.governmentDistrictCode,
    municipality.countyCode,
    municipality.municipalityCode,
    onBoundary ? "grenze-eindeutig" : "eindeutig",
  ]);
}

await new Promise((resolve, reject) => {
  output.end(resolve);
  output.on("error", reject);
});

report.assignment_rate =
  report.unique_assignments / report.basemap_only_records;
report.generated_at = new Date().toISOString();
await import("node:fs/promises").then(({ writeFile }) =>
  writeFile(reportPath, `${JSON.stringify(report, null, 2)}\n`),
);
console.log(JSON.stringify(report, null, 2));
