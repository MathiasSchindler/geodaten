# Berlin Browser Route Lab

This folder contains a browser-only prototype for Berlin routing and map rendering.

Generated data files:

- `berlin.rte.xz` - xz-compressed OSMRTE01 walking route pack built from `data/berlin-260524.osm.pbf`
- `berlin.rpack.xz` - xz-compressed OSMRPACK render pack built from `data/berlin-260524.osm.pbf`

WASM modules:

- `wasm/xzdec.js` / `wasm/xzdec.wasm`
- `wasm/rte-route.js` / `wasm/rte-route.wasm`
- `wasm/rpack-render.js` / `wasm/rpack-render.wasm`

Rebuild the WASM modules with:

```sh
sh browser/build-wasm.sh
```

Rebuild the Berlin packs with:

```sh
make build/freestanding-linux-x86_64/pbf-to-rte build/freestanding-linux-x86_64/pbf-to-rpack
build/freestanding-linux-x86_64/pbf-to-rte --tile-size-m 2000 --threads 2 \
  data/berlin-260524.osm.pbf browser/berlin.rte
build/freestanding-linux-x86_64/pbf-to-rpack --threads 2 --way-threads 2 --buildings \
  data/berlin-260524.osm.pbf browser/berlin.rpack
xz -6 -k -f browser/berlin.rte
xz -6 -k -f browser/berlin.rpack
```

The browser fetches the `.xz` files and inflates them with the xz decoder WebAssembly module before writing the raw `.rte` and `.rpack` data into the routing and rendering WebAssembly file systems. `browser/build-wasm.sh` builds the xz decoder module from the upstream xz source release into `browser/wasm/`.

The map and route data is rendered from [OpenStreetMap](https://www.openstreetmap.org/) and its contributors, provided by [Geofabrik](https://download.geofabrik.de/europe/germany.html), and licensed under the [Open Data Commons Open Database License](https://opendatacommons.org/licenses/odbl/).

Run a local static server from the repository root:

```sh
python3 -m http.server 8080
```

Then open `http://localhost:8080/browser/`.

The app renders the Berlin map on load. Use the mouse wheel over the map to zoom in and out around the cursor. Press `Find Route` to route between the form values, or click the map once to set the start point and a second time to set the target point. The second click starts routing immediately. Clicked points are written as `lat,lon` coordinates and routed through the same walking graph snapper as address results.

After a route is shown, drag either endpoint marker to update the coordinate and rerender the route. Route map rendering expands the route bounding box to match the visible map aspect ratio, so mostly vertical or horizontal routes remain undistorted.