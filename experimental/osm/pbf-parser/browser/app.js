const ROUTE_PACK_PATH = 'berlin.rte.xz';
const RENDER_PACK_PATH = 'berlin.rpack.xz';
const ROUTE_PACK_FS = '/data/berlin.rte';
const RENDER_PACK_FS = '/data/berlin.rpack';

const state = {
  routeModule: null,
  renderModule: null,
  xzModule: null,
  routeLoaded: false,
  renderLoaded: false,
  currentView: null,
  currentRoutePoints: [],
  nextPointRole: 'from',
  selectedPoints: { from: null, to: null },
  dragRole: null,
  suppressNextClick: false,
  pendingZoomView: null,
  zoomTimer: 0,
  zoomRendering: false,
  routeStdout: [],
  routeStderr: [],
  renderStdout: [],
  renderStderr: [],
  xzStdout: [],
  xzStderr: []
};

const elements = {
  form: document.getElementById('route-form'),
  from: document.getElementById('from-input'),
  to: document.getElementById('to-input'),
  routeButton: document.getElementById('route-button'),
  renderButton: document.getElementById('render-button'),
  mapPane: document.getElementById('map-pane'),
  status: document.getElementById('status-text'),
  progress: document.getElementById('progress'),
  map: document.getElementById('map-image'),
  fromMarker: document.getElementById('from-marker'),
  toMarker: document.getElementById('to-marker'),
  placeholder: document.getElementById('map-placeholder'),
  summary: document.getElementById('summary-list'),
  steps: document.getElementById('steps-list'),
  log: document.getElementById('log-output')
};

function setBusy(isBusy) {
  elements.routeButton.disabled = isBusy;
  elements.renderButton.disabled = isBusy;
}

function setStatus(text, progress = null) {
  elements.status.textContent = text;
  if (progress === null) {
    elements.progress.removeAttribute('value');
  } else {
    elements.progress.value = progress;
  }
}

async function fetchBytes(path, label) {
  setStatus(`Loading ${label}...`, null);
  const response = await fetch(path);
  if (!response.ok) throw new Error(`Could not load ${path}: ${response.status}`);
  const reader = response.body && response.body.getReader ? response.body.getReader() : null;
  const length = Number(response.headers.get('Content-Length') || '0');
  if (!reader) return new Uint8Array(await response.arrayBuffer());

  const chunks = [];
  let loaded = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
    loaded += value.length;
    if (length > 0) setStatus(`Loading ${label}: ${Math.round((loaded / length) * 100)}%`, loaded / length);
  }

  const bytes = new Uint8Array(loaded);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.length;
  }
  return bytes;
}

function ensureDir(module, path) {
  if (module.FS.analyzePath(path).exists) return;
  try {
    module.FS.mkdir(path);
  } catch (error) {
    if (!module.FS.analyzePath(path).exists) throw error;
  }
}

async function ensureXzModule() {
  if (!state.xzModule) {
    setStatus('Starting xz WebAssembly...', null);
    state.xzModule = await createXzDecodeModule({
      locateFile: (path) => `wasm/${path}`,
      print: (line) => state.xzStdout.push(line),
      printErr: (line) => state.xzStderr.push(line),
      noInitialRun: true
    });
    ensureDir(state.xzModule, '/in');
    ensureDir(state.xzModule, '/out');
  }
}

async function fetchXzBytes(path, label, outputName) {
  const compressed = await fetchBytes(path, `${label} xz`);
  await ensureXzModule();
  setStatus(`Decompressing ${label}...`, null);
  const inputPath = `/in/${outputName}.xz`;
  const outputPath = `/out/${outputName}`;
  state.xzModule.FS.writeFile(inputPath, compressed);
  const result = callCli(state.xzModule, 'xzStdout', 'xzStderr', [inputPath, outputPath]);
  try {
    const bytes = state.xzModule.FS.readFile(outputPath);
    return bytes;
  } finally {
    if (state.xzModule.FS.analyzePath(inputPath).exists) state.xzModule.FS.unlink(inputPath);
    if (state.xzModule.FS.analyzePath(outputPath).exists) state.xzModule.FS.unlink(outputPath);
    if (result.stderr) elements.log.textContent = result.stderr;
  }
}

async function ensureRouteModule() {
  if (!state.routeModule) {
    setStatus('Starting routing WebAssembly...', null);
    state.routeModule = await createRteRouteModule({
      locateFile: (path) => `wasm/${path}`,
      print: (line) => state.routeStdout.push(line),
      printErr: (line) => state.routeStderr.push(line),
      noInitialRun: true
    });
    ensureDir(state.routeModule, '/data');
  }
  if (!state.routeLoaded) {
    const bytes = await fetchXzBytes(ROUTE_PACK_PATH, 'Berlin route pack', 'berlin.rte');
    state.routeModule.FS.writeFile(ROUTE_PACK_FS, bytes);
    state.routeLoaded = true;
  }
}

async function ensureRenderModule() {
  if (!state.renderModule) {
    setStatus('Starting rendering WebAssembly...', null);
    state.renderModule = await createRpackRenderModule({
      locateFile: (path) => `wasm/${path}`,
      print: (line) => state.renderStdout.push(line),
      printErr: (line) => state.renderStderr.push(line),
      noInitialRun: true
    });
    ensureDir(state.renderModule, '/data');
  }
  if (!state.renderLoaded) {
    const bytes = await fetchXzBytes(RENDER_PACK_PATH, 'Berlin render pack', 'berlin.rpack');
    state.renderModule.FS.writeFile(RENDER_PACK_FS, bytes);
    state.renderLoaded = true;
  }
}

function callCli(module, stdoutKey, stderrKey, args) {
  state[stdoutKey] = [];
  state[stderrKey] = [];
  try {
    module.callMain(args);
  } catch (error) {
    const status = Number(error?.status ?? error?.exitCode ?? error?.code);
    if (!(error === 0 || status === 0 || (Number.isNaN(status) && !error?.message))) throw error;
  }
  return {
    stdout: state[stdoutKey].join('\n'),
    stderr: state[stderrKey].join('\n')
  };
}

function parseJsonLines(text) {
  return text.split('\n').filter(Boolean).map((line) => JSON.parse(line));
}

function routePoints(events) {
  return events
    .filter((event) => event.event === 'route_point' && event.data)
    .map((event) => ({ lat: event.data.lat, lon: event.data.lon }));
}

function routeBbox(points, size) {
  let minLat = Math.min(...points.map((point) => point.lat));
  let maxLat = Math.max(...points.map((point) => point.lat));
  let minLon = Math.min(...points.map((point) => point.lon));
  let maxLon = Math.max(...points.map((point) => point.lon));
  const latPad = Math.max((maxLat - minLat) * 0.12, 0.004);
  const lonPad = Math.max((maxLon - minLon) * 0.12, 0.004);
  minLat -= latPad;
  maxLat += latPad;
  minLon -= lonPad;
  maxLon += lonPad;

  const targetAspect = size.width / size.height;
  const centerLat = (minLat + maxLat) / 2;
  const centerLon = (minLon + maxLon) / 2;
  let latSpan = Math.max(maxLat - minLat, 0.008);
  let lonSpan = Math.max(maxLon - minLon, 0.008);
  const currentAspect = lonSpan / latSpan;
  if (currentAspect < targetAspect) lonSpan = latSpan * targetAspect;
  else latSpan = lonSpan / targetAspect;
  minLat = centerLat - latSpan / 2;
  maxLat = centerLat + latSpan / 2;
  minLon = centerLon - lonSpan / 2;
  maxLon = centerLon + lonSpan / 2;

  return `${minLon.toFixed(7)},${minLat.toFixed(7)},${maxLon.toFixed(7)},${maxLat.toFixed(7)}`;
}

function mapSize() {
  const rect = elements.mapPane.getBoundingClientRect();
  return {
    width: Math.max(640, Math.round(rect.width || 1200)),
    height: Math.max(420, Math.round(rect.height || 800))
  };
}

function parseRenderView(stdout) {
  const values = {};
  for (const line of stdout.split('\n')) {
    const match = /^(render_min_lon_nano|render_min_lat_nano|render_max_lon_nano|render_max_lat_nano):\s*(-?\d+)/.exec(line);
    if (match) values[match[1]] = Number(match[2]) / 1e9;
  }
  if (
    Number.isFinite(values.render_min_lon_nano) &&
    Number.isFinite(values.render_min_lat_nano) &&
    Number.isFinite(values.render_max_lon_nano) &&
    Number.isFinite(values.render_max_lat_nano)
  ) {
    return {
      minLon: values.render_min_lon_nano,
      minLat: values.render_min_lat_nano,
      maxLon: values.render_max_lon_nano,
      maxLat: values.render_max_lat_nano
    };
  }
  return null;
}

function viewToBbox(view) {
  return `${view.minLon.toFixed(7)},${view.minLat.toFixed(7)},${view.maxLon.toFixed(7)},${view.maxLat.toFixed(7)}`;
}

function routePolyline(points) {
  return points.map((point) => `${point.lon.toFixed(7)},${point.lat.toFixed(7)}`).join('\n') + '\n';
}

function clampZoomView(view) {
  const centerLat = (view.minLat + view.maxLat) / 2;
  const centerLon = (view.minLon + view.maxLon) / 2;
  const minLatSpan = 0.0012;
  const maxLatSpan = 0.42;
  const aspect = (view.maxLon - view.minLon) / (view.maxLat - view.minLat);
  let latSpan = Math.min(Math.max(view.maxLat - view.minLat, minLatSpan), maxLatSpan);
  let lonSpan = latSpan * aspect;
  const maxLonSpan = 0.9;
  if (lonSpan > maxLonSpan) {
    lonSpan = maxLonSpan;
    latSpan = lonSpan / aspect;
  }
  return {
    minLat: centerLat - latSpan / 2,
    maxLat: centerLat + latSpan / 2,
    minLon: centerLon - lonSpan / 2,
    maxLon: centerLon + lonSpan / 2
  };
}

function zoomViewAround(view, anchor, ratioX, ratioY, scale) {
  const latSpan = (view.maxLat - view.minLat) * scale;
  const lonSpan = (view.maxLon - view.minLon) * scale;
  const maxLat = anchor.lat + latSpan * ratioY;
  const minLon = anchor.lon - lonSpan * ratioX;
  return clampZoomView({
    minLat: maxLat - latSpan,
    maxLat,
    minLon,
    maxLon: minLon + lonSpan
  });
}

function coordText(point) {
  return `${point.lat.toFixed(7)},${point.lon.toFixed(7)}`;
}

function mapContentRect() {
  const rect = elements.map.getBoundingClientRect();
  const naturalWidth = elements.map.naturalWidth || rect.width;
  const naturalHeight = elements.map.naturalHeight || rect.height;
  if (rect.width <= 0 || rect.height <= 0 || naturalWidth <= 0 || naturalHeight <= 0) return rect;

  const imageAspect = naturalWidth / naturalHeight;
  const slotAspect = rect.width / rect.height;
  if (slotAspect > imageAspect) {
    const width = rect.height * imageAspect;
    const left = rect.left + (rect.width - width) / 2;
    return { left, top: rect.top, right: left + width, bottom: rect.bottom, width, height: rect.height };
  }

  const height = rect.width / imageAspect;
  const top = rect.top + (rect.height - height) / 2;
  return { left: rect.left, top, right: rect.right, bottom: top + height, width: rect.width, height };
}

function placeMarker(role, point) {
  const marker = role === 'from' ? elements.fromMarker : elements.toMarker;
  if (!state.currentView || !point) return;
  const paneRect = elements.mapPane.getBoundingClientRect();
  const imageRect = mapContentRect();
  const x = ((point.lon - state.currentView.minLon) / (state.currentView.maxLon - state.currentView.minLon)) * imageRect.width;
  const y = ((state.currentView.maxLat - point.lat) / (state.currentView.maxLat - state.currentView.minLat)) * imageRect.height;
  marker.style.left = `${imageRect.left - paneRect.left + x}px`;
  marker.style.top = `${imageRect.top - paneRect.top + y}px`;
  marker.hidden = false;
}

function refreshMarkers() {
  elements.fromMarker.hidden = true;
  elements.toMarker.hidden = true;
  placeMarker('from', state.selectedPoints.from);
  placeMarker('to', state.selectedPoints.to);
}

function renderSummary(events) {
  const route = events.find((event) => event.event === 'route')?.data;
  const from = events.find((event) => event.event === 'address' && event.data?.role === 'from')?.data;
  const to = events.find((event) => event.event === 'address' && event.data?.role === 'to')?.data;
  elements.summary.replaceChildren();
  const rows = [
    ['From', from ? `${from.street || ''} ${from.housenumber || ''}`.trim() : ''],
    ['To', to ? `${to.street || ''} ${to.housenumber || ''}`.trim() : ''],
    ['Distance', route ? `${route.distance_m} m` : ''],
    ['Estimate', route ? `${route.estimated_minutes} min` : '']
  ];
  for (const [label, value] of rows) {
    if (!value) continue;
    const dt = document.createElement('dt');
    const dd = document.createElement('dd');
    dt.textContent = label;
    dd.textContent = value;
    elements.summary.append(dt, dd);
  }
}

function renderSteps(events) {
  const steps = events.filter((event) => event.event === 'route_step').map((event) => event.data);
  elements.steps.replaceChildren();
  for (const step of steps.slice(0, 80)) {
    const item = document.createElement('li');
    const direction = step.direction ? ` ${step.direction}` : '';
    item.textContent = `${step.action}${direction}${step.distance_m ? ` for ${step.distance_m} m` : ''}.`;
    elements.steps.append(item);
  }
}

function interestingRenderLines(stdout) {
  return stdout
    .split('\n')
    .filter((line) => /^(selected_tiles|features_drawn|segments_drawn|route_overlay_segments_drawn|output):/.test(line))
    .join('\n');
}

function showPng(module, path) {
  const bytes = module.FS.readFile(path);
  const blob = new Blob([bytes], { type: 'image/png' });
  const previous = elements.map.dataset.url;
  const url = URL.createObjectURL(blob);
  elements.map.src = url;
  elements.map.hidden = false;
  elements.placeholder.hidden = true;
  elements.map.dataset.url = url;
  if (previous) URL.revokeObjectURL(previous);
}

async function renderBerlin() {
  await ensureRenderModule();
  setStatus('Rendering Berlin...', null);
  const outputPath = '/out/berlin.png';
  const size = mapSize();
  state.currentRoutePoints = [];
  ensureDir(state.renderModule, '/out');
  const result = callCli(state.renderModule, 'renderStdout', 'renderStderr', [
    RENDER_PACK_FS,
    outputPath,
    '--city',
    'Berlin',
    '--width',
    String(size.width),
    '--height',
    String(size.height)
  ]);
  showPng(state.renderModule, outputPath);
  state.currentView = parseRenderView(result.stdout) || state.currentView;
  refreshMarkers();
  elements.log.textContent = state.renderStdout.slice(-18).join('\n');
  setStatus('Rendered Berlin.', 1);
}

async function renderViewport(view) {
  await ensureRenderModule();
  setStatus('Rendering zoomed map...', null);
  ensureDir(state.renderModule, '/out');
  const size = mapSize();
  const outputPath = '/out/viewport.png';
  const args = [
    RENDER_PACK_FS,
    outputPath,
    '--bbox',
    viewToBbox(view),
    '--width',
    String(size.width),
    '--height',
    String(size.height)
  ];
  if (state.currentRoutePoints.length !== 0) {
    state.renderModule.FS.writeFile('/out/route.txt', routePolyline(state.currentRoutePoints));
    args.push('--route-polyline', '/out/route.txt');
  }
  const result = callCli(state.renderModule, 'renderStdout', 'renderStderr', args);
  showPng(state.renderModule, outputPath);
  state.currentView = parseRenderView(result.stdout) || view;
  refreshMarkers();
  elements.log.textContent = state.currentRoutePoints.length === 0 ? state.renderStdout.slice(-18).join('\n') : interestingRenderLines(result.stdout);
  setStatus('Map zoomed.', 1);
  return result;
}

async function renderRouteMap(points) {
  await ensureRenderModule();
  setStatus('Rendering route overlay...', null);
  ensureDir(state.renderModule, '/out');
  const size = mapSize();
  state.currentRoutePoints = points;
  state.renderModule.FS.writeFile('/out/route.txt', routePolyline(points));
  const result = callCli(state.renderModule, 'renderStdout', 'renderStderr', [
    RENDER_PACK_FS,
    '/out/route.png',
    '--bbox',
    routeBbox(points, size),
    '--width',
    String(size.width),
    '--height',
    String(size.height),
    '--route-polyline',
    '/out/route.txt'
  ]);
  showPng(state.renderModule, '/out/route.png');
  state.currentView = parseRenderView(result.stdout) || state.currentView;
  refreshMarkers();
  return result;
}

async function routeFromForm() {
  setBusy(true);
  try {
    await ensureRouteModule();
    setStatus('Calculating walking route...', null);
    const result = callCli(state.routeModule, 'routeStdout', 'routeStderr', [
      ROUTE_PACK_FS,
      elements.from.value.trim(),
      elements.to.value.trim(),
      '--json'
    ]);
    const events = parseJsonLines(result.stdout);
    const status = events.find((item) => item.event === 'route_status')?.data?.status;
    if (status !== 'found') throw new Error(result.stderr || 'Route was not found.');
    const points = routePoints(events);
    renderSummary(events);
    renderSteps(events);
    const renderResult = await renderRouteMap(points);
    elements.log.textContent = [
      `Route points: ${points.length}`,
      interestingRenderLines(renderResult.stdout),
      result.stderr,
      renderResult.stderr
    ].filter(Boolean).join('\n');
    setStatus('Route ready.', 1);
  } catch (error) {
    setStatus(error?.message || String(error), 1);
    elements.log.textContent = error.stack || String(error);
  } finally {
    setBusy(false);
  }
}

async function findRoute(event) {
  event.preventDefault();
  await routeFromForm();
}

function pointFromClientPosition(clientX, clientY) {
  return pointFromClientPositionInView(clientX, clientY, state.currentView);
}

function pointFromClientPositionInView(clientX, clientY, view) {
  if (!view || elements.map.hidden) return null;
  const rect = mapContentRect();
  if (rect.width <= 0 || rect.height <= 0) return null;
  if (clientX < rect.left || clientX > rect.right || clientY < rect.top || clientY > rect.bottom) return null;
  const ratioX = Math.min(Math.max((clientX - rect.left) / rect.width, 0), 1);
  const ratioY = Math.min(Math.max((clientY - rect.top) / rect.height, 0), 1);
  return {
    lat: view.maxLat - ratioY * (view.maxLat - view.minLat),
    lon: view.minLon + ratioX * (view.maxLon - view.minLon),
    ratioX,
    ratioY
  };
}

function normalizedWheelDelta(event) {
  if (event.deltaMode === WheelEvent.DOM_DELTA_LINE) return event.deltaY * 16;
  if (event.deltaMode === WheelEvent.DOM_DELTA_PAGE) return event.deltaY * mapContentRect().height;
  return event.deltaY;
}

function scheduleZoomRender() {
  if (state.zoomTimer) window.clearTimeout(state.zoomTimer);
  state.zoomTimer = window.setTimeout(renderPendingZoom, 140);
}

async function renderPendingZoom() {
  state.zoomTimer = 0;
  if (state.zoomRendering || !state.pendingZoomView) return;
  const view = state.pendingZoomView;
  state.pendingZoomView = null;
  state.zoomRendering = true;
  setBusy(true);
  try {
    await renderViewport(view);
  } catch (error) {
    setStatus(error?.message || String(error), 1);
    elements.log.textContent = error.stack || String(error);
  } finally {
    setBusy(false);
    state.zoomRendering = false;
    if (state.pendingZoomView) scheduleZoomRender();
  }
}

function handleMapWheel(event) {
  const baseView = state.pendingZoomView || state.currentView;
  const point = pointFromClientPositionInView(event.clientX, event.clientY, baseView);
  if (!point) return;
  event.preventDefault();
  const delta = Math.max(-600, Math.min(600, normalizedWheelDelta(event)));
  const scale = Math.max(0.5, Math.min(2, Math.exp(delta / 520)));
  state.pendingZoomView = zoomViewAround(baseView, point, point.ratioX, point.ratioY, scale);
  setStatus(delta < 0 ? 'Zooming in...' : 'Zooming out...', null);
  scheduleZoomRender();
}

function setPoint(role, point) {
  state.selectedPoints[role] = point;
  if (role === 'from') {
    elements.from.value = coordText(point);
  } else {
    elements.to.value = coordText(point);
  }
  refreshMarkers();
}

function suppressImmediateClick() {
  state.suppressNextClick = true;
  window.setTimeout(() => {
    state.suppressNextClick = false;
  }, 0);
}

function handleMapClick(event) {
  if (state.suppressNextClick) {
    state.suppressNextClick = false;
    return;
  }
  const point = pointFromClientPosition(event.clientX, event.clientY);
  if (!point) return;
  const role = state.nextPointRole;
  setPoint(role, point);
  if (role === 'from') {
    state.nextPointRole = 'to';
    setStatus('Start set. Click the target point.', 1);
  } else {
    state.nextPointRole = 'from';
    if (state.selectedPoints.from) routeFromForm();
    else setStatus('Target set. Click the start point.', 1);
  }
}

function handleMarkerPointerDown(event, role) {
  event.preventDefault();
  event.stopPropagation();
  state.dragRole = role;
  event.currentTarget.setPointerCapture(event.pointerId);
}

function handleMarkerPointerMove(event) {
  if (!state.dragRole) return;
  const point = pointFromClientPosition(event.clientX, event.clientY);
  if (!point) return;
  setPoint(state.dragRole, point);
}

function handleMarkerPointerUp(event) {
  if (!state.dragRole) return;
  event.preventDefault();
  event.stopPropagation();
  state.dragRole = null;
  suppressImmediateClick();
  if (state.selectedPoints.from && state.selectedPoints.to) routeFromForm();
}

async function renderOnly() {
  setBusy(true);
  try {
    await renderBerlin();
  } catch (error) {
    setStatus(error.message, 1);
    elements.log.textContent = error.stack || String(error);
  } finally {
    setBusy(false);
  }
}

elements.form.addEventListener('submit', findRoute);
elements.renderButton.addEventListener('click', renderOnly);
elements.mapPane.addEventListener('click', handleMapClick);
elements.mapPane.addEventListener('wheel', handleMapWheel, { passive: false });
elements.map.addEventListener('load', refreshMarkers);
elements.fromMarker.addEventListener('pointerdown', (event) => handleMarkerPointerDown(event, 'from'));
elements.toMarker.addEventListener('pointerdown', (event) => handleMarkerPointerDown(event, 'to'));
elements.fromMarker.addEventListener('pointermove', handleMarkerPointerMove);
elements.toMarker.addEventListener('pointermove', handleMarkerPointerMove);
elements.fromMarker.addEventListener('pointerup', handleMarkerPointerUp);
elements.toMarker.addEventListener('pointerup', handleMarkerPointerUp);
elements.fromMarker.addEventListener('pointercancel', handleMarkerPointerUp);
elements.toMarker.addEventListener('pointercancel', handleMarkerPointerUp);
window.addEventListener('resize', refreshMarkers);
window.addEventListener('load', () => {
  renderOnly();
});