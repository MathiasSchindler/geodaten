import { createReadStream, createWriteStream } from "node:fs";
import process from "node:process";
import { createGunzip } from "node:zlib";
import { parse } from "csv-parse";

const [mode, inputPath, stateCode, outputPrefix] = process.argv.slice(2);
if (!["basemap", "state"].includes(mode) || !inputPath || !stateCode || !outputPrefix) {
  console.error(
    "Usage: node generate-comparison-keys.mjs <basemap|state> <input> <state-code> <output-prefix>",
  );
  process.exit(2);
}

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
    .replace(/str\.(?=\s|$)/gu, "strasse")
    .replace(/[^a-z0-9]+/gu, "");
}

function key(locality, street, houseNumber) {
  return [normalized(locality), normalized(street), normalized(houseNumber)].join(
    "\t",
  );
}

const outputs = {
  primary: createWriteStream(`${outputPrefix}.primary.keys`),
  primaryCoordinates: createWriteStream(`${outputPrefix}.primary.coords`),
  secondary:
    mode === "state"
      ? createWriteStream(`${outputPrefix}.secondary.keys`)
      : null,
  secondaryCoordinates:
    mode === "state"
      ? createWriteStream(`${outputPrefix}.secondary.coords`)
      : null,
  union: createWriteStream(`${outputPrefix}.union.keys`),
  unionCoordinates: createWriteStream(`${outputPrefix}.union.coords`),
};

function writeKey(target, locality, street, houseNumber) {
  if (!locality || !street || !houseNumber) return;
  target.write(`${key(locality, street, houseNumber)}\n`);
}

function writeCoordinate(
  target,
  locality,
  street,
  houseNumber,
  longitude,
  latitude,
) {
  if (!locality || !street || !houseNumber || !longitude || !latitude) return;
  target.write(
    `${key(locality, street, houseNumber)}\t${clean(longitude)}\t${clean(latitude)}\n`,
  );
}

let records = 0;
let usablePrimary = 0;
let usableSecondary = 0;
let blankStreet = 0;
let blankHouseNumber = 0;
let blankPrimaryLocality = 0;

if (mode === "basemap") {
  const readline = await import("node:readline");
  const input = readline.createInterface({
    input: createReadStream(inputPath),
    crlfDelay: Infinity,
  });

  for await (const line of input) {
    if (!line) continue;
    const fields = line.split("\t");
    if (fields.length !== 7) {
      throw new Error(`Expected 7 fields, got ${fields.length}`);
    }
    const [state, locality, , street, houseNumber, longitude, latitude] = fields;
    if (state !== stateCode) continue;
    records += 1;
    if (!street) blankStreet += 1;
    if (!houseNumber) blankHouseNumber += 1;
    if (!locality) blankPrimaryLocality += 1;
    if (locality && street && houseNumber) {
      writeKey(outputs.primary, locality, street, houseNumber);
      writeKey(outputs.union, locality, street, houseNumber);
      writeCoordinate(
        outputs.primaryCoordinates,
        locality,
        street,
        houseNumber,
        longitude,
        latitude,
      );
      writeCoordinate(
        outputs.unionCoordinates,
        locality,
        street,
        houseNumber,
        longitude,
        latitude,
      );
      usablePrimary += 1;
    }
  }
} else {
  const parser = createReadStream(inputPath)
    .pipe(createGunzip())
    .pipe(parse({ columns: true, bom: true, relax_quotes: false }));

  for await (const row of parser) {
    records += 1;
    const street = clean(row.strassenname);
    const houseNumber = clean(
      `${row.hausnummer ?? ""}${row.hausnummernzusatz ?? ""}`,
    );
    const postalLocality = clean(row.ortsname_post);
    const municipality = clean(row.gemeindename);
    const longitude = clean(row.longitude_wgs84);
    const latitude = clean(row.latitude_wgs84);
    if (!street) blankStreet += 1;
    if (!houseNumber) blankHouseNumber += 1;
    if (!postalLocality) blankPrimaryLocality += 1;

    if (postalLocality && street && houseNumber) {
      writeKey(outputs.primary, postalLocality, street, houseNumber);
      writeKey(outputs.union, postalLocality, street, houseNumber);
      writeCoordinate(
        outputs.primaryCoordinates,
        postalLocality,
        street,
        houseNumber,
        longitude,
        latitude,
      );
      writeCoordinate(
        outputs.unionCoordinates,
        postalLocality,
        street,
        houseNumber,
        longitude,
        latitude,
      );
      usablePrimary += 1;
    }
    if (municipality && street && houseNumber) {
      writeKey(outputs.secondary, municipality, street, houseNumber);
      writeKey(outputs.union, municipality, street, houseNumber);
      writeCoordinate(
        outputs.secondaryCoordinates,
        municipality,
        street,
        houseNumber,
        longitude,
        latitude,
      );
      if (normalized(municipality) !== normalized(postalLocality)) {
        writeCoordinate(
          outputs.unionCoordinates,
          municipality,
          street,
          houseNumber,
          longitude,
          latitude,
        );
      }
      usableSecondary += 1;
    }
  }
}

await Promise.all(
  Object.values(outputs)
    .filter(Boolean)
    .map(
      (stream) =>
        new Promise((resolve, reject) => {
          stream.end(resolve);
          stream.on("error", reject);
        }),
    ),
);

console.log(
  JSON.stringify(
    {
      mode,
      input: inputPath,
      state_code: stateCode,
      records,
      usable_primary: usablePrimary,
      usable_secondary: usableSecondary,
      blank_street: blankStreet,
      blank_house_number: blankHouseNumber,
      blank_primary_locality: blankPrimaryLocality,
    },
    null,
    2,
  ),
);
