import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";

const [statesDirectory] = process.argv.slice(2);
if (!statesDirectory) {
  console.error("Usage: node summarize-consolidation.mjs <states-directory>");
  process.exit(2);
}

const stateDirectories = (await readdir(statesDirectory, {
  withFileTypes: true,
}))
  .filter((entry) => entry.isDirectory() && /^[A-Z]{2}$/.test(entry.name))
  .map((entry) => entry.name)
  .sort();
const states = [];

for (const state of stateDirectories) {
  const stats = JSON.parse(
    await readFile(
      path.join(statesDirectory, state, "statistics.json"),
      "utf8",
    ),
  );
  const matched =
    stats.matched_full_key_25m + stats.matched_street_house_5m;
  if (stats.mode === "basemap_only") {
    if (
      stats.state_records !== 0 ||
      stats.basemap_only_records !== stats.basemap_records ||
      stats.consolidated_records !== stats.basemap_records
    ) {
      throw new Error(`Invalid basemap-only accounting for ${state}`);
    }
  } else {
    if (matched + stats.basemap_only_records !== stats.basemap_records) {
      throw new Error(`Invalid basemap accounting for ${state}`);
    }
    if (
      matched + stats.state_only_records !== stats.state_records ||
      stats.state_records + stats.basemap_only_records !==
        stats.consolidated_records
    ) {
      throw new Error(`Invalid state accounting for ${state}`);
    }
  }
  states.push(stats);
}

if (states.length !== 16) {
  throw new Error(`Expected 16 state statistics, got ${states.length}`);
}

const total = (field) =>
  states.reduce((sum, state) => sum + state[field], 0);
const summary = {
  generated_at: new Date().toISOString(),
  states: states.length,
  state_primary_records: total("state_records"),
  basemap_input_records: total("basemap_records"),
  matched_full_key_25m: total("matched_full_key_25m"),
  matched_street_house_5m: total("matched_street_house_5m"),
  matched_total:
    total("matched_full_key_25m") + total("matched_street_house_5m"),
  ambiguous_full_key: total("ambiguous_full_key"),
  ambiguous_street_house: total("ambiguous_street_house"),
  basemap_only_records: total("basemap_only_records"),
  state_only_records: total("state_only_records"),
  consolidated_records: total("consolidated_records"),
  records_with_postcode: total("records_with_postcode"),
  records_without_postcode:
    total("consolidated_records") - total("records_with_postcode"),
  postcode_coverage:
    total("records_with_postcode") / total("consolidated_records"),
  district_filled_from_basemap: total("district_filled_from_basemap"),
  coordinates_replaced_from_basemap: total(
    "coordinates_replaced_from_basemap",
  ),
  by_and_mv_basemap_only: true,
  state_results: states,
};

console.log(JSON.stringify(summary, null, 2));
