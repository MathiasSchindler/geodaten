import { createHash } from "node:crypto";
import { createReadStream, createWriteStream } from "node:fs";
import { mkdir, rm } from "node:fs/promises";
import { once } from "node:events";
import path from "node:path";
import process from "node:process";
import readline from "node:readline";
import { DatabaseSync } from "node:sqlite";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

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

const STATE_SOURCE_LICENSE = {
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

const [state, basemapPath, stateCsvPath, outputDirectory] =
  process.argv.slice(2);
if (
  !STATE_NAMES[state] ||
  !basemapPath ||
  !stateCsvPath ||
  !outputDirectory
) {
  console.error(
    "Usage: node consolidate-state.mjs <state> <basemap.tsv> <state.csv.gz|-> <output-directory>",
  );
  process.exit(2);
}
const basemapOnly = stateCsvPath === "-";
if (!basemapOnly && !STATE_SOURCE_LICENSE[state]) {
  throw new Error(`No state-source license configured for ${state}`);
}

await mkdir(outputDirectory, { recursive: true });
const databasePath = path.join(outputDirectory, `${state}.sqlite`);
await rm(databasePath, { force: true });

function clean(value) {
  return String(value ?? "").trim().replaceAll("\t", " ").replaceAll("\n", " ");
}

function normalized(value) {
  return clean(value)
    .toLocaleLowerCase("de-DE")
    .replaceAll("ä", "ae")
    .replaceAll("ö", "oe")
    .replaceAll("ü", "ue")
    .replaceAll("ß", "ss")
    .normalize("NFKD")
    .replace(/\p{M}/gu, "")
    .replace(/str\.?(?=\s|$)/gu, "strasse")
    .replace(/[^a-z0-9]+/gu, "");
}

function coordinate(value, label, sourceId) {
  const text = clean(value);
  if (!text) {
    throw new Error(`Missing ${label} for state source record ${sourceId}`);
  }
  const parsed = Number(text.replace(",", "."));
  if (!Number.isFinite(parsed)) {
    throw new Error(
      `Invalid ${label} ${JSON.stringify(text)} for state source record ${sourceId}`,
    );
  }
  return parsed;
}

function csv(value) {
  const text = String(value ?? "");
  return `"${text.replaceAll('"', '""')}"`;
}

function splitHouseNumber(value) {
  const match = clean(value).match(/^(\d+)(.*)$/u);
  return match ? [match[1], match[2].trim()] : [clean(value), ""];
}

function shaId(prefix, value) {
  return `${prefix}-${createHash("sha256")
    .update(value)
    .digest("hex")
    .slice(0, 24)}`;
}

function basemapId(record) {
  return shaId(
    "BKG",
    [
      record.state,
      record.locality,
      record.district,
      record.street,
      record.houseNumber,
      record.longitude,
      record.latitude,
    ].join("\u001f"),
  );
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

async function writeRow(stream, row) {
  if (!stream.write(`${row.map(csv).join(",")}\n`)) {
    await once(stream, "drain");
  }
}

function basemapMainRow(record, bundId) {
  const [houseNumber, suffix] = splitHouseNumber(record.houseNumber);
  return [
    `${record.street}${record.houseNumber ? ` ${record.houseNumber}` : ""}, ${record.locality}`,
    STATE_NAMES[state],
    STATE_CODES[state],
    "",
    record.locality,
    "",
    record.district,
    record.street,
    houseNumber,
    suffix,
    record.longitude,
    record.latitude,
    bundId,
    [
      state,
      record.locality,
      record.district,
      record.street,
      record.houseNumber,
    ].join(";"),
  ];
}

function parseBasemapLine(line) {
  const fields = line.split("\t");
  if (fields.length !== 7) {
    throw new Error(`Expected 7 basemap fields, got ${fields.length}`);
  }
  const [
    recordState,
    locality,
    district,
    street,
    houseNumber,
    longitude,
    latitude,
  ] = fields;
  if (recordState !== state) {
    throw new Error(`Expected basemap state ${state}, got ${recordState}`);
  }
  return {
    state,
    locality,
    district,
    street,
    houseNumber,
    longitude,
    latitude,
  };
}

const mainPath = path.join(outputDirectory, `${state}-adressen-unsortiert.csv`);
const provenancePath = path.join(
  outputDirectory,
  `${state}-provenienz-unsortiert.csv`,
);
const conflictPath = path.join(
  outputDirectory,
  `${state}-konflikte-unsortiert.csv`,
);
const main = createWriteStream(mainPath);
const provenance = createWriteStream(provenancePath);
const conflicts = createWriteStream(conflictPath);
await writeRow(main, MAIN_HEADER);
await writeRow(provenance, PROVENANCE_HEADER);
await writeRow(conflicts, CONFLICT_HEADER);

const stats = {
  state_code: state,
  mode: basemapOnly ? "basemap_only" : "state_primary_with_basemap",
  state_records: 0,
  basemap_records: 0,
  matched_full_key_25m: 0,
  matched_street_house_5m: 0,
  ambiguous_full_key: 0,
  ambiguous_street_house: 0,
  basemap_only_records: 0,
  state_only_records: 0,
  consolidated_records: 0,
  records_with_postcode: 0,
  district_filled_from_basemap: 0,
  coordinates_replaced_from_basemap: 0,
};

if (basemapOnly) {
  const input = readline.createInterface({
    input: createReadStream(basemapPath),
    crlfDelay: Infinity,
  });
  for await (const line of input) {
    if (!line) continue;
    const record = parseBasemapLine(line);
    const bkgId = basemapId(record);
    const bundId = shaId("BUND", `BASEMAP\u001f${bkgId}`);
    await writeRow(main, basemapMainRow(record, bundId));
    await writeRow(provenance, [
      bundId,
      state,
      "BASEMAP",
      "",
      "",
      bkgId,
      "CC-BY-4.0",
      "nur_basemap",
      "",
      "",
      "",
      "BASEMAP",
      record.district ? "BASEMAP" : "",
      "keine_plz;keine_fachliche_quell_id",
    ]);
    stats.basemap_records += 1;
    stats.basemap_only_records += 1;
    stats.consolidated_records += 1;
  }
} else {
  const database = new DatabaseSync(databasePath);
  database.exec(`
    PRAGMA journal_mode = OFF;
    PRAGMA synchronous = OFF;
    PRAGMA temp_store = MEMORY;
    PRAGMA cache_size = -200000;
    CREATE TABLE state_records (
      id INTEGER PRIMARY KEY,
      source_id TEXT NOT NULL,
      source_house_key TEXT NOT NULL,
      row_json TEXT NOT NULL,
      norm_postal TEXT NOT NULL,
      norm_municipality TEXT NOT NULL,
      norm_street TEXT NOT NULL,
      norm_house TEXT NOT NULL,
      longitude REAL NOT NULL,
      latitude REAL NOT NULL,
      matched_bkg_id TEXT,
      match_rule TEXT,
      match_distance REAL,
      basemap_district TEXT,
      basemap_longitude REAL,
      basemap_latitude REAL
    );
  `);
  const insert = database.prepare(`
    INSERT INTO state_records (
      source_id, source_house_key, row_json, norm_postal,
      norm_municipality, norm_street, norm_house, longitude, latitude
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
  `);

  const stateParser = createReadStream(stateCsvPath)
    .pipe(createGunzip())
    .pipe(parse({ columns: true, bom: true, relax_quotes: false }));
  database.exec("BEGIN");
  for await (const row of stateParser) {
    const sourceId = clean(row.datensatznummer);
    const house = clean(
      `${row.hausnummer ?? ""}${row.hausnummernzusatz ?? ""}`,
    );
    insert.run(
      sourceId,
      clean(row.hausschluessel),
      JSON.stringify(MAIN_HEADER.map((field) => clean(row[field]))),
      normalized(row.ortsname_post),
      normalized(row.gemeindename),
      normalized(row.strassenname),
      normalized(house),
      coordinate(row.longitude_wgs84, "longitude", sourceId),
      coordinate(row.latitude_wgs84, "latitude", sourceId),
    );
    stats.state_records += 1;
    if (stats.state_records % 100_000 === 0) {
      database.exec("COMMIT; BEGIN");
    }
  }
  database.exec("COMMIT");
  database.exec(`
    CREATE INDEX state_match_idx
      ON state_records(norm_street, norm_house, latitude, longitude);
  `);

  const candidates = database.prepare(`
    SELECT id, source_id, longitude, latitude
    FROM state_records
    WHERE matched_bkg_id IS NULL
      AND norm_street = ?
      AND norm_house = ?
      AND latitude BETWEEN ? AND ?
      AND longitude BETWEEN ? AND ?
      AND (
        (? <> '' AND norm_postal = ?)
        OR (? <> '' AND norm_municipality = ?)
      )
  `);
  const candidatesWithoutLocality = database.prepare(`
    SELECT id, source_id, longitude, latitude
    FROM state_records
    WHERE matched_bkg_id IS NULL
      AND norm_street = ?
      AND norm_house = ?
      AND latitude BETWEEN ? AND ?
      AND longitude BETWEEN ? AND ?
  `);
  const updateMatch = database.prepare(`
    UPDATE state_records
    SET matched_bkg_id = ?, match_rule = ?, match_distance = ?,
        basemap_district = ?, basemap_longitude = ?, basemap_latitude = ?
    WHERE id = ?
  `);

  function nearbyRows(statement, record, thresholdMeters, withLocality) {
    const latitude = Number(record.latitude);
    const longitude = Number(record.longitude);
    const latitudeDelta = thresholdMeters / 111_320;
    const longitudeDelta =
      thresholdMeters /
      (111_320 * Math.max(0.2, Math.cos((latitude * Math.PI) / 180)));
    const args = [
      normalized(record.street),
      normalized(record.houseNumber),
      latitude - latitudeDelta,
      latitude + latitudeDelta,
      longitude - longitudeDelta,
      longitude + longitudeDelta,
    ];
    if (withLocality) {
      const locality = normalized(record.locality);
      args.push(locality, locality, locality, locality);
    }
    return statement
      .all(...args)
      .map((row) => ({
        ...row,
        distance: distanceMeters(
          { longitude, latitude },
          { longitude: row.longitude, latitude: row.latitude },
        ),
      }))
      .filter((row) => row.distance <= thresholdMeters)
      .sort((a, b) => a.distance - b.distance || a.id - b.id);
  }

  const basemapInput = readline.createInterface({
    input: createReadStream(basemapPath),
    crlfDelay: Infinity,
  });
  database.exec("BEGIN");
  for await (const line of basemapInput) {
    if (!line) continue;
    const record = parseBasemapLine(line);
    const bkgId = basemapId(record);
    stats.basemap_records += 1;

    let matchRows = nearbyRows(candidates, record, 25, true);
    let rule = "ort_strasse_hausnummer_25m";
    let ambiguousStage = "";
    if (matchRows.length > 1) {
      stats.ambiguous_full_key += 1;
      ambiguousStage = "ort_strasse_hausnummer_25m";
    } else if (matchRows.length === 0) {
      matchRows = nearbyRows(
        candidatesWithoutLocality,
        record,
        5,
        false,
      );
      rule = "strasse_hausnummer_5m";
      if (matchRows.length > 1) {
        stats.ambiguous_street_house += 1;
        ambiguousStage = "strasse_hausnummer_5m";
      }
    }

    if (matchRows.length === 1) {
      const match = matchRows[0];
      updateMatch.run(
        bkgId,
        rule,
        match.distance,
        record.district,
        Number(record.longitude),
        Number(record.latitude),
        match.id,
      );
      if (rule === "ort_strasse_hausnummer_25m") {
        stats.matched_full_key_25m += 1;
      } else {
        stats.matched_street_house_5m += 1;
      }
    } else {
      const bundId = shaId("BUND", `BASEMAP\u001f${bkgId}`);
      await writeRow(main, basemapMainRow(record, bundId));
      await writeRow(provenance, [
        bundId,
        state,
        "BASEMAP",
        "",
        "",
        bkgId,
        "CC-BY-4.0",
        ambiguousStage ? "mehrdeutig_nicht_vereinigt" : "nur_basemap",
        ambiguousStage,
        matchRows.length ? matchRows[0].distance.toFixed(3) : "",
        "",
        "BASEMAP",
        record.district ? "BASEMAP" : "",
        ambiguousStage
          ? "mehrere_land_kandidaten;keine_plz;keine_fachliche_quell_id"
          : "keine_plz;keine_fachliche_quell_id",
      ]);
      if (ambiguousStage) {
        await writeRow(conflicts, [
          state,
          bkgId,
          record.locality,
          record.street,
          record.houseNumber,
          record.longitude,
          record.latitude,
          ambiguousStage,
          matchRows.length,
          matchRows.map((candidate) => candidate.source_id).join("|"),
        ]);
      }
      stats.basemap_only_records += 1;
      stats.consolidated_records += 1;
    }

    if (stats.basemap_records % 100_000 === 0) {
      database.exec("COMMIT; BEGIN");
      console.error(
        JSON.stringify({
          state,
          basemap_records: stats.basemap_records,
          matched:
            stats.matched_full_key_25m +
            stats.matched_street_house_5m,
          basemap_only: stats.basemap_only_records,
        }),
      );
    }
  }
  database.exec("COMMIT");

  const allStateRows = database.prepare(`
    SELECT source_id, source_house_key, row_json, matched_bkg_id,
           match_rule, match_distance, basemap_district,
           basemap_longitude, basemap_latitude
    FROM state_records
    ORDER BY id
  `);
  for (const record of allStateRows.iterate()) {
    const row = JSON.parse(record.row_json);
    const sourceIdentity = [
      state,
      record.source_id,
      record.source_house_key,
      row[10],
      row[11],
    ].join("\u001f");
    const bundId = shaId("BUND", `LAND\u001f${sourceIdentity}`);
    const matched = Boolean(record.matched_bkg_id);
    let districtSource = row[6] ? `LAND-${state}` : "";
    const quality = [];

    if (!row[6] && matched && record.basemap_district) {
      row[6] = record.basemap_district;
      districtSource = "BASEMAP";
      stats.district_filled_from_basemap += 1;
      quality.push("ortsteil_aus_basemap");
    }
    if (state === "NI" && matched) {
      row[10] = Number(record.basemap_longitude).toFixed(7);
      row[11] = Number(record.basemap_latitude).toFixed(7);
      stats.coordinates_replaced_from_basemap += 1;
      quality.push("koordinate_aus_basemap");
    }

    row[12] = bundId;
    await writeRow(main, row);
    await writeRow(provenance, [
      bundId,
      state,
      matched ? `LAND-${state}|BASEMAP` : `LAND-${state}`,
      record.source_id,
      record.source_house_key,
      record.matched_bkg_id ?? "",
      matched
        ? `${STATE_SOURCE_LICENSE[state]}|CC-BY-4.0`
        : STATE_SOURCE_LICENSE[state],
      matched ? "land_und_basemap" : "nur_land",
      record.match_rule ?? "",
      record.match_distance == null
        ? ""
        : Number(record.match_distance).toFixed(3),
      row[3] ? `LAND-${state}` : "",
      state === "NI" && matched ? "BASEMAP" : `LAND-${state}`,
      districtSource,
      [
        ...quality,
        ...(row[3] ? [] : ["keine_plz"]),
      ].join(";"),
    ]);
    if (row[3]) stats.records_with_postcode += 1;
    if (!matched) stats.state_only_records += 1;
    stats.consolidated_records += 1;
  }
  database.close();
}

await Promise.all(
  [main, provenance, conflicts].map(
    (stream) =>
      new Promise((resolve, reject) => {
        stream.end(resolve);
        stream.on("error", reject);
      }),
  ),
);

await rm(databasePath, { force: true });
console.log(JSON.stringify(stats, null, 2));
