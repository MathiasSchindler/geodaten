import { createReadStream, createWriteStream } from "node:fs";
import { readFile } from "node:fs/promises";
import { once } from "node:events";
import process from "node:process";
import { createBrotliDecompress, createGunzip, createGzip } from "node:zlib";
import { parse } from "csv-parse";

const [polygonPath, addressesPath, outputPath, statisticsPath] =
  process.argv.slice(2);
if (!polygonPath || !addressesPath || !outputPath || !statisticsPath) {
  console.error(
    "Usage: node assign-postcodes.mjs <postcodes.topojson.br|postcodes.geojson> <addresses.csv.gz> <assignments.csv.gz> <statistics.json>",
  );
  process.exit(2);
}

const ADDRESS_HEADER = [
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
const OUTPUT_HEADER = [
  "bund_id",
  "bundesland",
  "amtliche_plz",
  "osm_plz",
  "osm_relationen",
  "polygon_treffer",
  "unterschiedliche_plz",
  "lageklasse",
  "validierung",
];
const STATE_BY_KEY = {
  "01": "SH",
  "02": "HH",
  "03": "NI",
  "04": "HB",
  "05": "NW",
  "06": "HE",
  "07": "RP",
  "08": "BW",
  "09": "BY",
  "10": "SL",
  "11": "BE",
  "12": "BB",
  "13": "MV",
  "14": "SN",
  "15": "ST",
  "16": "TH",
};
const FINE_GRID = 0.01;
const INDEX_GRID = 0.1;
const GRID_OFFSET = 100_000;
const GRID_FACTOR = 250_000;

function csv(value) {
  const text = String(value ?? "");
  return `"${text.replaceAll('"', '""')}"`;
}

function gridKey(longitude, latitude, size) {
  const x = Math.floor(longitude / size);
  const y = Math.floor(latitude / size);
  return (x + GRID_OFFSET) * GRID_FACTOR + y + GRID_OFFSET;
}

function arcCoordinates(topology, arcIndex) {
  const reverse = arcIndex < 0;
  const index = reverse ? ~arcIndex : arcIndex;
  const source = topology.arcs[index];
  let coordinates;
  if (topology.transform) {
    let x = 0;
    let y = 0;
    coordinates = source.map(([deltaX, deltaY]) => {
      x += deltaX;
      y += deltaY;
      return [
        x * topology.transform.scale[0] + topology.transform.translate[0],
        y * topology.transform.scale[1] + topology.transform.translate[1],
      ];
    });
  } else {
    coordinates = source;
  }
  return reverse ? [...coordinates].reverse() : coordinates;
}

function ringCoordinates(topology, arcIndexes) {
  const ring = [];
  for (const arcIndex of arcIndexes) {
    const coordinates = arcCoordinates(topology, arcIndex);
    ring.push(...(ring.length ? coordinates.slice(1) : coordinates));
  }
  return ring;
}

function geometryPolygons(topology, geometry) {
  const source =
    geometry.type === "Polygon" ? [geometry.arcs] : geometry.arcs;
  return source.map((polygon) =>
    polygon.map((arcIndexes) => ringCoordinates(topology, arcIndexes)),
  );
}

function bboxFor(polygons) {
  let minimumLongitude = Infinity;
  let minimumLatitude = Infinity;
  let maximumLongitude = -Infinity;
  let maximumLatitude = -Infinity;
  for (const polygon of polygons) {
    for (const ring of polygon) {
      for (const [longitude, latitude] of ring) {
        minimumLongitude = Math.min(minimumLongitude, longitude);
        minimumLatitude = Math.min(minimumLatitude, latitude);
        maximumLongitude = Math.max(maximumLongitude, longitude);
        maximumLatitude = Math.max(maximumLatitude, latitude);
      }
    }
  }
  return [
    minimumLongitude,
    minimumLatitude,
    maximumLongitude,
    maximumLatitude,
  ];
}

function pointOnSegment(x, y, x1, y1, x2, y2) {
  const cross = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1);
  if (Math.abs(cross) > 1e-11) return false;
  return (
    x >= Math.min(x1, x2) - 1e-12 &&
    x <= Math.max(x1, x2) + 1e-12 &&
    y >= Math.min(y1, y2) - 1e-12 &&
    y <= Math.max(y1, y2) + 1e-12
  );
}

function pointInRing(x, y, ring) {
  let inside = false;
  for (let index = 0, previous = ring.length - 1; index < ring.length; previous = index++) {
    const [x1, y1] = ring[previous];
    const [x2, y2] = ring[index];
    if (pointOnSegment(x, y, x1, y1, x2, y2)) return true;
    if (
      (y1 > y) !== (y2 > y) &&
      x < ((x2 - x1) * (y - y1)) / (y2 - y1) + x1
    ) {
      inside = !inside;
    }
  }
  return inside;
}

function pointInFeature(longitude, latitude, feature) {
  const [minimumLongitude, minimumLatitude, maximumLongitude, maximumLatitude] =
    feature.bbox;
  if (
    longitude < minimumLongitude ||
    longitude > maximumLongitude ||
    latitude < minimumLatitude ||
    latitude > maximumLatitude
  ) {
    return false;
  }
  return feature.polygons.some(
    (polygon) =>
      pointInRing(longitude, latitude, polygon[0]) &&
      !polygon.slice(1).some((hole) => pointInRing(longitude, latitude, hole)),
  );
}

function addBoundaryCells(boundaryCells, x1, y1, x2, y2) {
  for (
    let x = Math.floor(Math.min(x1, x2) / FINE_GRID);
    x <= Math.floor(Math.max(x1, x2) / FINE_GRID);
    x += 1
  ) {
    for (
      let y = Math.floor(Math.min(y1, y2) / FINE_GRID);
      y <= Math.floor(Math.max(y1, y2) / FINE_GRID);
      y += 1
    ) {
      boundaryCells.add(
        (x + GRID_OFFSET) * GRID_FACTOR + y + GRID_OFFSET,
      );
    }
  }
}

const polygonDocument = JSON.parse(
  await new Promise((resolve, reject) => {
    const chunks = [];
    const input = createReadStream(polygonPath);
    const decoded = polygonPath.endsWith(".br")
      ? input.pipe(createBrotliDecompress())
      : input;
    decoded
      .on("data", (chunk) => chunks.push(chunk))
      .on("end", () => resolve(Buffer.concat(chunks).toString("utf8")))
      .on("error", reject);
  }),
);
let sourceGeometries;
let polygonsForGeometry;
if (polygonDocument.type === "FeatureCollection") {
  sourceGeometries = polygonDocument.features.map((feature) => ({
    ...feature.geometry,
    id: feature.id,
    properties: feature.properties,
  }));
  polygonsForGeometry = (geometry) =>
    geometry.type === "Polygon"
      ? [geometry.coordinates]
      : geometry.coordinates;
} else {
  const geometryCollection = Object.values(polygonDocument.objects ?? {}).find(
    (object) => object.type === "GeometryCollection",
  );
  if (!geometryCollection) {
    throw new Error("Expected a GeoJSON FeatureCollection or TopoJSON GeometryCollection");
  }
  sourceGeometries = geometryCollection.geometries;
  polygonsForGeometry = (geometry) =>
    geometryPolygons(polygonDocument, geometry);
}

const features = [];
const spatialIndex = new Map();
const boundaryCells = new Set();
const invalidPolygonPostcodes = [];

for (const geometry of sourceGeometries) {
  if (!["Polygon", "MultiPolygon"].includes(geometry.type)) continue;
  const postcode = String(geometry.properties?.postcode ?? "");
  if (!/^\d{5}$/.test(postcode)) {
    invalidPolygonPostcodes.push(postcode);
    continue;
  }
  const polygons = polygonsForGeometry(geometry);
  const bbox = bboxFor(polygons);
  const feature = {
    postcode,
    relation: String(
      geometry.properties?.relation_id ??
        geometry.properties?.rel ??
        geometry.id ??
        "",
    ),
    polygons,
    bbox,
  };
  const featureIndex = features.push(feature) - 1;
  const [minimumLongitude, minimumLatitude, maximumLongitude, maximumLatitude] =
    bbox;
  for (
    let x = Math.floor(minimumLongitude / INDEX_GRID);
    x <= Math.floor(maximumLongitude / INDEX_GRID);
    x += 1
  ) {
    for (
      let y = Math.floor(minimumLatitude / INDEX_GRID);
      y <= Math.floor(maximumLatitude / INDEX_GRID);
      y += 1
    ) {
      const key = (x + GRID_OFFSET) * GRID_FACTOR + y + GRID_OFFSET;
      const indexes = spatialIndex.get(key);
      if (indexes) indexes.push(featureIndex);
      else spatialIndex.set(key, [featureIndex]);
    }
  }
  for (const polygon of polygons) {
    for (const ring of polygon) {
      for (let index = 0; index < ring.length; index += 1) {
        const [x1, y1] = ring[index];
        const [x2, y2] = ring[(index + 1) % ring.length];
        addBoundaryCells(boundaryCells, x1, y1, x2, y2);
      }
    }
  }
}

function findMatches(longitude, latitude) {
  const indexes =
    spatialIndex.get(gridKey(longitude, latitude, INDEX_GRID)) ?? [];
  return indexes
    .map((index) => features[index])
    .filter((feature) => pointInFeature(longitude, latitude, feature));
}

const interiorCache = new Map();
function matchesForPoint(longitude, latitude) {
  const fineKey = gridKey(longitude, latitude, FINE_GRID);
  if (boundaryCells.has(fineKey)) {
    return { matches: findMatches(longitude, latitude), locationClass: "grenzzelle" };
  }
  let matches = interiorCache.get(fineKey);
  if (!matches) {
    matches = findMatches(longitude, latitude);
    interiorCache.set(fineKey, matches);
  }
  return { matches, locationClass: "innenzelle" };
}

function emptyValidationCounts() {
  return {
    official_records: 0,
    matching: 0,
    conflicting: 0,
    no_polygon: 0,
    multiple_postcodes: 0,
    coverage: 0,
    accuracy_when_unique: 0,
    accuracy_overall: 0,
    interior: { official_records: 0, matching: 0, conflicting: 0, no_polygon: 0, multiple_postcodes: 0 },
    boundary: { official_records: 0, matching: 0, conflicting: 0, no_polygon: 0, multiple_postcodes: 0 },
  };
}

const statistics = {
  source_polygon_geometries: sourceGeometries.length,
  usable_polygon_geometries: features.length,
  invalid_polygon_postcodes: invalidPolygonPostcodes,
  boundary_grid_size_degrees: FINE_GRID,
  addresses: 0,
  unique_postcode_assignment: 0,
  no_polygon: 0,
  multiple_postcodes: 0,
  multiple_relations_same_postcode: 0,
  interior_cell_assignments: 0,
  boundary_cell_assignments: 0,
  validation: emptyValidationCounts(),
  by_state: {},
};

function recordValidation(target, result, locationClass) {
  target.official_records += 1;
  const bucket = locationClass === "grenzzelle" ? target.boundary : target.interior;
  bucket.official_records += 1;
  bucket[result] += 1;
  target[result] += 1;
}

const gzip = createGzip({ level: 9, mtime: 0 });
const output = createWriteStream(outputPath);
gzip.pipe(output);
gzip.write(`${OUTPUT_HEADER.map(csv).join(",")}\n`);

const parser = createReadStream(addressesPath)
  .pipe(createGunzip())
  .pipe(parse({ bom: true, relax_quotes: false }));
let rowNumber = 0;
for await (const row of parser) {
  rowNumber += 1;
  if (rowNumber === 1) {
    if (JSON.stringify(row) !== JSON.stringify(ADDRESS_HEADER)) {
      throw new Error(`Unexpected address header: ${JSON.stringify(row)}`);
    }
    continue;
  }
  if (row.length !== ADDRESS_HEADER.length) {
    throw new Error(`Address row ${rowNumber}: expected 14 columns`);
  }
  const longitude = Number(row[10]);
  const latitude = Number(row[11]);
  if (!Number.isFinite(longitude) || !Number.isFinite(latitude)) {
    throw new Error(`Address row ${rowNumber}: invalid coordinate`);
  }
  const state = STATE_BY_KEY[row[2]];
  if (!state) throw new Error(`Address row ${rowNumber}: invalid state key ${row[2]}`);
  const { matches, locationClass } = matchesForPoint(longitude, latitude);
  const postcodes = [...new Set(matches.map((match) => match.postcode))].sort();
  const relations = [...new Set(matches.map((match) => match.relation))].sort();
  let validation = "nicht_pruefbar";
  if (row[3]) {
    if (postcodes.length === 0) validation = "kein_polygon";
    else if (postcodes.length > 1) validation = "mehrere_plz";
    else if (postcodes[0] === row[3]) validation = "stimmt";
    else validation = "widerspruch";
    const resultMap = {
      stimmt: "matching",
      widerspruch: "conflicting",
      kein_polygon: "no_polygon",
      mehrere_plz: "multiple_postcodes",
    };
    recordValidation(statistics.validation, resultMap[validation], locationClass);
  }
  statistics.addresses += 1;
  statistics.by_state[state] ??= {
    addresses: 0,
    unique_postcode_assignment: 0,
    no_polygon: 0,
    multiple_postcodes: 0,
    validation: emptyValidationCounts(),
  };
  const stateStats = statistics.by_state[state];
  stateStats.addresses += 1;
  if (postcodes.length === 0) {
    statistics.no_polygon += 1;
    stateStats.no_polygon += 1;
  } else if (postcodes.length === 1) {
    statistics.unique_postcode_assignment += 1;
    stateStats.unique_postcode_assignment += 1;
    if (matches.length > 1) statistics.multiple_relations_same_postcode += 1;
  } else {
    statistics.multiple_postcodes += 1;
    stateStats.multiple_postcodes += 1;
  }
  if (row[3]) {
    const resultMap = {
      stimmt: "matching",
      widerspruch: "conflicting",
      kein_polygon: "no_polygon",
      mehrere_plz: "multiple_postcodes",
    };
    recordValidation(stateStats.validation, resultMap[validation], locationClass);
  }
  if (locationClass === "grenzzelle") statistics.boundary_cell_assignments += 1;
  else statistics.interior_cell_assignments += 1;

  const outputRow = [
    row[12],
    state,
    row[3],
    postcodes.length === 1 ? postcodes[0] : "",
    relations.join("|"),
    matches.length,
    postcodes.length,
    locationClass,
    validation,
  ];
  if (!gzip.write(`${outputRow.map(csv).join(",")}\n`)) {
    await once(gzip, "drain");
  }
  if (statistics.addresses % 1_000_000 === 0) {
    console.error(
      JSON.stringify({
        addresses: statistics.addresses,
        unique: statistics.unique_postcode_assignment,
        no_polygon: statistics.no_polygon,
        multiple: statistics.multiple_postcodes,
      }),
    );
  }
}

for (const target of [
  statistics.validation,
  ...Object.values(statistics.by_state).map((state) => state.validation),
]) {
  const uniquelyAssigned =
    target.matching + target.conflicting;
  target.coverage =
    target.official_records === 0
      ? null
      : uniquelyAssigned / target.official_records;
  target.accuracy_when_unique =
    uniquelyAssigned === 0 ? null : target.matching / uniquelyAssigned;
  target.accuracy_overall =
    target.official_records === 0
      ? null
      : target.matching / target.official_records;
}

gzip.end();
await once(output, "finish");
await import("node:fs/promises").then(({ writeFile }) =>
  writeFile(statisticsPath, `${JSON.stringify(statistics, null, 2)}\n`),
);
