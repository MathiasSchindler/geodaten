import { createReadStream, createWriteStream } from "node:fs";
import { mkdir } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import readline from "node:readline";
import { once } from "node:events";

const [inputPath, outputDirectory] = process.argv.slice(2);
if (!inputPath || !outputDirectory) {
  console.error(
    "Usage: node split-raw-by-state.mjs <sorted-raw.tsv> <output-directory>",
  );
  process.exit(2);
}

await mkdir(outputDirectory, { recursive: true });
const streams = new Map();
const counts = {};

function streamFor(state) {
  if (!/^[A-Z]{2}$/.test(state)) {
    throw new Error(`Invalid state code: ${state}`);
  }
  if (!streams.has(state)) {
    streams.set(
      state,
      createWriteStream(path.join(outputDirectory, `${state}.tsv`)),
    );
  }
  return streams.get(state);
}

const input = readline.createInterface({
  input: createReadStream(inputPath),
  crlfDelay: Infinity,
});

for await (const line of input) {
  if (!line) continue;
  const state = line.slice(0, line.indexOf("\t"));
  const stream = streamFor(state);
  if (!stream.write(`${line}\n`)) {
    await once(stream, "drain");
  }
  counts[state] = (counts[state] ?? 0) + 1;
}

await Promise.all(
  [...streams.values()].map(
    (stream) =>
      new Promise((resolve, reject) => {
        stream.end(resolve);
        stream.on("error", reject);
      }),
  ),
);

console.log(
  JSON.stringify(
    Object.fromEntries(
      Object.entries(counts).sort(([a], [b]) => a.localeCompare(b)),
    ),
    null,
    2,
  ),
);
