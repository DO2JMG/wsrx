let activeTab = 'log';
let lastSpectrum = null;
let lastPeaks = null;
let peakHitboxes = [];
let lastPlotMeta = null;
let activeChannelFreqs = [];
let spectrumAxisMinP = null;
let spectrumAxisMaxP = null;

let sondeMap = null;
let sondeMapLayers = new Map(); // serial -> { line: L.Polyline, marker: L.CircleMarker }
let sondeMapPredictionLayers = new Map(); // serial -> { line: L.Polyline, marker: L.CircleMarker|null }
let sondeMapLaunchLayers = new Map(); // serial -> L.Marker (first track point)
let sondeMapBurstLayers = new Map(); // serial -> L.Marker (highest track point so far)
let sondeMapBoundsFitted = false;
let lastMapRefresh = 0;
let lastSondesData = []; // most recent sondes[] from /api/radiosondes, used by the azel box
let mapAzElSerial = null; // serial the azel overlay is currently showing, or null when hidden
let mapAzElInterval = null;
const MAP_HOURS = 12;
const MAP_REFRESH_MS = 5000;
const PREDICTION_COLOR = '#385b80';

// A sonde counts as "live" while its last received frame is younger than
// this - same freshness window the balloon label already uses to switch
// its altitude line from blue to red.
const SONDE_FRESH_MAX_AGE_SEC = 180;

const MAP_LIVE_ONLY_KEY = 'wettersonde-map-live-only';
let mapLiveOnly = false;
try { mapLiveOnly = localStorage.getItem(MAP_LIVE_ONLY_KEY) === '1'; } catch (e) {}

const THEME_KEY = 'wettersonde-theme';

function getThemeColor(varName, fallback) {
  const v = getComputedStyle(document.documentElement).getPropertyValue(varName).trim();
  return v || fallback;
}

function applyTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  const btn = document.getElementById('themeToggle');
  if (btn) btn.textContent = theme === 'light' ? ' Day' : ' Night';
  try { localStorage.setItem(THEME_KEY, theme); } catch (e) {}
  if (lastSpectrum) drawSpectrum(lastSpectrum, lastPeaks);
  if (radarInterval) radarDrawFrame();
}

function initTheme() {
  let theme = 'dark';
  try {
    const saved = localStorage.getItem(THEME_KEY);
    if (saved === 'light' || saved === 'dark') {
      theme = saved;
    } else if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches) {
      theme = 'light';
    }
  } catch (e) {}
  applyTheme(theme);
}

function toggleTheme() {
  const current = document.documentElement.getAttribute('data-theme') === 'light' ? 'light' : 'dark';
  applyTheme(current === 'light' ? 'dark' : 'light');
}

function iniValue(text, section, key) {
  if (!text) return null;
  const lines = String(text).split(/\r?\n/);
  let inSection = section === null;
  for (const rawLine of lines) {
    const line = rawLine.trim();
    if (!line || line.startsWith(';') || line.startsWith('#')) continue;
    const sectionMatch = line.match(/^\[(.+)\]$/);
    if (sectionMatch) {
      inSection = sectionMatch[1].trim().toLowerCase() === String(section).toLowerCase();
      continue;
    }
    if (!inSection) continue;
    const eq = line.indexOf('=');
    if (eq === -1) continue;
    const k = line.slice(0, eq).trim().toLowerCase();
    if (k === key.toLowerCase()) {
      return line.slice(eq + 1).trim();
    }
  }
  return null;
}

async function getJson(url) {
  const r = await fetch(url, { cache: 'no-store' });
  return await r.json();
}

async function getText(url) {
  const r = await fetch(url, { cache: 'no-store' });
  return await r.text();
}

function setText(id, value) {
  document.getElementById(id).textContent = value;
}

function setHtml(id, value) {
  document.getElementById(id).innerHTML = value;
}


function resizeCanvas(canvas, minW, minH) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(minW, Math.floor(rect.width * dpr));
  const h = Math.max(minH, Math.floor(rect.height * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  return { w, h, dpr };
}

function percentile(sortedValues, q) {
  if (!sortedValues.length) return NaN;
  const pos = (sortedValues.length - 1) * q;
  const lo = Math.floor(pos);
  const hi = Math.ceil(pos);
  if (lo === hi) return sortedValues[lo];
  const k = pos - lo;
  return sortedValues[lo] * (1 - k) + sortedValues[hi] * k;
}

function interpolatePalette(t, stops) {
  t = Math.max(0, Math.min(1, t));
  for (let i = 1; i < stops.length; i++) {
    if (t <= stops[i][0]) {
      const [p0, c0] = stops[i - 1];
      const [p1, c1] = stops[i];
      const k = (t - p0) / (p1 - p0 || 1);
      const r = Math.round(c0[0] + (c1[0] - c0[0]) * k);
      const g = Math.round(c0[1] + (c1[1] - c0[1]) * k);
      const b = Math.round(c0[2] + (c1[2] - c0[2]) * k);
      return [r, g, b];
    }
  }
  return stops[stops.length - 1][1];
}

function drawActiveChannelMarkers(ctx, freqs, minF, maxF, padL, padT, plotW, plotH, dpr) {
  if (!Array.isArray(freqs) || !freqs.length) return;
  const yBase = padT + plotH;
  const markerFill = getThemeColor('--plot-channel-marker', '#d25a3a');
  const markerStroke = getThemeColor('--plot-channel-marker-border', '#7f321e');
  for (const freq of freqs) {
    const f = Number(freq);
    if (!Number.isFinite(f)) continue;
    if (f < minF || f > maxF) continue;
    const xx = padL + (f - minF) / (maxF - minF || 1) * plotW;
    const half = 6 * dpr;
    const topY = yBase + 2 * dpr;
    const tipY = yBase + 12 * dpr;
    ctx.fillStyle = markerFill;
    ctx.strokeStyle = markerStroke;
    ctx.lineWidth = 1 * dpr;
    ctx.beginPath();
    ctx.moveTo(xx - half, topY);
    ctx.lineTo(xx + half, topY);
    ctx.lineTo(xx, tipY);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();
  }
}

let cachedCallsign = '';
let lastConfigFetch = 0;
const CONFIG_REFRESH_MS = 5000;

async function refreshStatus() {
  try {
    const s = await getJson('/api/status');
    setHtml('running', s.running ? '<span class="ok">running</span>' : '<span class="bad">stopped</span>');
    setText('pid', s.pid || '-');
    const now = Date.now();
    if (now - lastConfigFetch > CONFIG_REFRESH_MS) {
      lastConfigFetch = now;
      try {
        const configText = await getText('/api/config');
        const callsign = iniValue(configText, 'station', 'callsign');
        cachedCallsign = callsign || '';
      } catch (e) {
        cachedCallsign = '';
      }
    }
    setHtml('path', cachedCallsign ? ('Receiver: <span class="header-value">' + escapeHtml(cachedCallsign) + '</span>') : '');
    setText('statusText', s.raw || '');
    const channels = s.channels || [];
    activeChannelFreqs = channels.map(x => Number(x)).filter(x => Number.isFinite(x));
    setText('channelCount', channels.length);
    setHtml('channels', channels.map(x => '<span>' + x + ' MHz</span>').join(''));
    setText('updated', new Date().toLocaleString());
  } catch (e) {
    activeChannelFreqs = [];
    setHtml('running', '<span class="bad">error</span>');
  }
}

function setLogText(t) {
  const el = document.getElementById('logText');
  const nearBottom = (el.scrollTop + el.clientHeight + 40) >= el.scrollHeight;
  el.textContent = t;
  if (nearBottom || activeTab === 'log') el.scrollTop = el.scrollHeight;
}

function drawSpectrum(spec, peaksDoc) {
  const canvas = document.getElementById('spectrumCanvas');
  const info = document.getElementById('spectrumInfo');
  if (!canvas || !info) return;

  const { w, h, dpr } = resizeCanvas(canvas, 320, 180);

  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = getThemeColor('--plot-bg', '#0c1828');
  ctx.fillRect(0, 0, w, h);

  const points = spec && Array.isArray(spec.points) ? spec.points : [];
  if (!points.length) {
    ctx.fillStyle = getThemeColor('--plot-empty-text', '#8b949e');
    ctx.font = `${13 * dpr}px system-ui`;
    ctx.fillText(spec && spec.error ? spec.error : 'No live spectrum available yet', 14 * dpr, 28 * dpr);
    info.textContent = 'Live spectrum: no data';
    lastPlotMeta = null;
    peakHitboxes = [];
    return;
  }

  const padL = 48 * dpr;
  const padR = 16 * dpr;
  const padT = 20 * dpr;
  const padB = 40 * dpr;
  const plotW = w - padL - padR;
  const plotH = h - padT - padB;

  let minF = points[0][0], maxF = points[0][0];
  let minP = points[0][1], maxP = points[0][1];
  for (const p of points) {
    if (p[0] < minF) minF = p[0];
    if (p[0] > maxF) maxF = p[0];
    if (p[1] < minP) minP = p[1];
    if (p[1] > maxP) maxP = p[1];
  }
  const trig = Number((peaksDoc && peaksDoc.trigger_db) ?? spec.trigger_db);
  const noise = Number((peaksDoc && peaksDoc.noise_floor_db) ?? spec.noise_floor_db);
  if (Number.isFinite(trig)) {
    minP = Math.min(minP, trig - 4);
    maxP = Math.max(maxP, trig + 4);
  }
  minP = Math.floor(minP / 5) * 5;
  maxP = Math.ceil(maxP / 5) * 5;
  if (maxP - minP < 20) { maxP += 10; minP -= 10; }

  if (spectrumAxisMinP === null || spectrumAxisMaxP === null) {
    spectrumAxisMinP = minP;
    spectrumAxisMaxP = maxP;
  } else {
    const axisSmoothing = 0.25;
    spectrumAxisMinP += (minP - spectrumAxisMinP) * axisSmoothing;
    spectrumAxisMaxP += (maxP - spectrumAxisMaxP) * axisSmoothing;
  }
  minP = Math.floor(spectrumAxisMinP / 5) * 5;
  maxP = Math.ceil(spectrumAxisMaxP / 5) * 5;

  const x = f => padL + (f - minF) / (maxF - minF || 1) * plotW;
  const y = p => padT + (maxP - p) / (maxP - minP || 1) * plotH;

  const gridColor = getThemeColor('--plot-grid', '#507ba8');
  const axisTextColor = getThemeColor('--plot-axis-text', '#507ba8');
  const lineColor = getThemeColor('--plot-line', '#507ba8');
  const triggerColor = getThemeColor('--plot-trigger', '#bf702b');
  const peakColor = getThemeColor('--plot-peak', '#83c1ee');
  const peakLineColor = getThemeColor('--plot-peak-line', '#296481');
  const axisTitleColor = getThemeColor('--axis-title', '#c9d1d9');

  ctx.strokeStyle = gridColor;
  ctx.lineWidth = 1 * dpr;
  ctx.beginPath();
  for (let i = 0; i <= 5; i++) {
    const yy = padT + i * plotH / 5;
    ctx.moveTo(padL, yy);
    ctx.lineTo(w - padR, yy);
  }
  ctx.stroke();

  ctx.fillStyle = axisTextColor;
  ctx.font = `${11 * dpr}px system-ui`;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'middle';
  for (let i = 0; i <= 5; i++) {
    const val = maxP - i * (maxP - minP) / 5;
    ctx.fillText(val.toFixed(0), padL - 6 * dpr, padT + i * plotH / 5);
  }
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  for (let i = 0; i <= 5; i++) {
    const val = minF + i * (maxF - minF) / 5;
    ctx.fillText(val.toFixed(3).replace(/0+$/, '').replace(/\.$/, ''), padL + i * plotW / 5, h - padB + 8 * dpr);
  }

  if (Number.isFinite(trig)) {
    ctx.strokeStyle = triggerColor;
    ctx.setLineDash([6 * dpr, 5 * dpr]);
    ctx.beginPath();
    ctx.moveTo(padL, y(trig));
    ctx.lineTo(w - padR, y(trig));
    ctx.stroke();
    ctx.setLineDash([]);
  }

  ctx.strokeStyle = lineColor;
  ctx.lineWidth = 1 * dpr;
  ctx.beginPath();
  points.forEach((p, i) => {
    const xx = x(p[0]);
    const yy = y(p[1]);
    if (i === 0) ctx.moveTo(xx, yy);
    else ctx.lineTo(xx, yy);
  });
  ctx.stroke();

  const peaks = peaksDoc && Array.isArray(peaksDoc.peaks) ? peaksDoc.peaks : [];
  peakHitboxes = [];
  for (const pk of peaks) {
    const xx = x(pk[0]);
    const yy = y(pk[1]);
    ctx.fillStyle = peakColor;
    ctx.beginPath();
    ctx.arc(xx, yy, 5 * dpr, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = peakLineColor;
    ctx.beginPath();
    ctx.moveTo(xx, yy);
    ctx.lineTo(xx, padT);
    ctx.stroke();
    peakHitboxes.push({ xCss: xx / dpr, yCss: yy / dpr, freqMhz: pk[0], powerDb: pk[1] });
  }

  drawActiveChannelMarkers(ctx, activeChannelFreqs, minF, maxF, padL, padT, plotW, plotH, dpr);

  lastPlotMeta = { points, minF, maxF, minP, maxP, padL, padT, plotW, plotH, dpr };

  ctx.fillStyle = axisTitleColor;
  ctx.font = `${12 * dpr}px system-ui`;
  ctx.textAlign = 'right';
  ctx.textBaseline = 'bottom';
  ctx.fillText('Frequency (MHz)', w - padR, h - 4 * dpr);
  ctx.save();
  ctx.translate(12 * dpr, padT + plotH / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.textAlign = 'center';
  ctx.fillText('Power (dB)', 0, 0);
  ctx.restore();

  const peakText = peaks.length
    ? peaks.map(p => `<span class="scanner-value">${p[0].toFixed(3)}</span> MHz`).join(', ')
    : 'none';
  const liveTs = spec.timestamp || '-';
  const scanTs = peaksDoc && peaksDoc.timestamp ? peaksDoc.timestamp : '-';
  const noiseText = Number.isFinite(noise) ? noise.toFixed(1) : '-';
  const trigText = Number.isFinite(trig) ? trig.toFixed(1) : '-';
  info.innerHTML = `Live: <span class="scanner-value">${escapeHtml(liveTs)}</span> | ` +
    `Last scan: <span class="scanner-value">${escapeHtml(scanTs)}</span> | ` +
    `Noise <span class="scanner-value">${noiseText}</span> dB | ` +
    `Trigger <span class="scanner-value">${trigText}</span> dB | ` +
    `Peaks: ${peakText}`;
}

const spectrumBinSmoothed = new Map();

function smoothSpectrumPoints(points) {
  const alpha = 0.35; // higher = reacts faster, lower = smoother/slower
  const seen = new Set();
  const out = points.map(p => {
    const freq = p[0];
    const key = Math.round(freq * 1e6); // Hz, avoids float-equality issues
    seen.add(key);
    const prev = spectrumBinSmoothed.get(key);
    const smoothed = (prev === undefined) ? p[1] : prev + (p[1] - prev) * alpha;
    spectrumBinSmoothed.set(key, smoothed);
    return [freq, smoothed];
  });

  for (const key of Array.from(spectrumBinSmoothed.keys())) {
    if (!seen.has(key)) spectrumBinSmoothed.delete(key);
  }
  return out;
}

async function refreshSpectrum() {
  try {
    const [spec, peaks] = await Promise.all([
      getJson('/api/spectrum'),
      getJson('/api/peaks')
    ]);
    if (spec && Array.isArray(spec.points) && spec.points.length) {
      spec.points = smoothSpectrumPoints(spec.points);
    }
    lastSpectrum = spec;
    lastPeaks = peaks;
    drawSpectrum(lastSpectrum, lastPeaks);
  } catch (e) {
    drawSpectrum({ error: 'Spectrum could not be loaded', points: [] }, { peaks: [] });
  }
}


function escapeHtml(v) {
  return String(v).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function fmtAltitude(v) {
  if (v === null || v === undefined || Number.isNaN(Number(v))) return '-';
  return Number(v).toFixed(1) + ' m';
}

function fmtDistance(v) {
  if (v === null || v === undefined || Number.isNaN(Number(v))) return '-';
  return Number(v).toFixed(1) + ' km';
}

function fmtDistanceBearing(distKm, bearing, elevation) {
  const dist = fmtDistance(distKm);
  const parts = [];
  if (bearing !== null && bearing !== undefined && !Number.isNaN(Number(bearing))) {
    parts.push(Number(bearing).toFixed(0) + '\u00b0 az');
  }
  if (elevation !== null && elevation !== undefined && !Number.isNaN(Number(elevation))) {
    parts.push(Number(elevation).toFixed(1) + '\u00b0 el');
  }
  if (!parts.length) return dist;
  return dist === '-' ? parts.join(', ') : dist + ', ' + parts.join(', ');
}

function fmtFrequency(v) {
  if (v === null || v === undefined || Number.isNaN(Number(v))) return '-';
  return Number(v).toFixed(3) + ' MHz';
}

function fmtTime(v) {
  if (!v) return '-';
  return String(v).replace('T', ' ').replace('.000Z', ' UTC').replace('Z', ' UTC');
}

let radiosondesHours = 12;
const ACTIVE_SONDE_MAX_AGE_SEC = 600; // 10 minutes - matches the radar's own freshness window
let sondesCache = new Map(); // serial -> sonde row data

function renderRadiosondesTable() {
  const rows = document.getElementById('radiosondeRows');
  if (!rows) return;
  const sondes = Array.from(sondesCache.values());
  if (!sondes.length) {
    rows.innerHTML = '<tr><td class="empty" colspan="9">No radiosonde logs found for this time range</td></tr>';
    return;
  }
  sondes.sort((a, b) => (b.modified || 0) - (a.modified || 0));
  rows.innerHTML = sondes.map(s => `
    <tr data-serial="${escapeHtml(s.serial || '')}">
      <td>${s.serial || '-'}</td>
      <td>${s.launchsite || '-'}</td>
      <td>${fmtFrequency(s.frequency)}</td>
      <td>${s.type || '-'}</td>
      <td>${fmtAltitude(s.first_altitude)}</td>
      <td>${fmtAltitude(s.last_altitude)}</td>
      <td>${fmtDistanceBearing(s.distance_km, s.bearing_deg, s.elevation_deg)}</td>
      <td>${s.frames ?? '-'}</td>
      <td>${fmtTime(s.last_time)}</td>
    </tr>
  `).join('');
  rows.querySelectorAll('tr[data-serial]').forEach(tr => {
    tr.addEventListener('click', () => openSondeDetail(tr.dataset.serial));
  });
}

// Full reload respecting the 12h/All filter - replaces the whole cache.
// Only needed on tab switch / filter change, not on every periodic poll.
async function loadRadiosondesFull() {
  try {
    const data = await getJson('/api/radiosondes?hours=' + radiosondesHours);
    const sondes = Array.isArray(data.radiosondes) ? data.radiosondes : [];
    sondesCache = new Map(sondes.map(s => [s.serial, s]));
    renderRadiosondesTable();
  } catch (e) {
    const rows = document.getElementById('radiosondeRows');
    if (rows) rows.innerHTML = '<tr><td class="empty" colspan="9">Could not load radiosonde list</td></tr>';
  }
}

function mergeSondeRecord(existing, incoming) {
  const merged = existing ? { ...existing } : {};
  for (const key of Object.keys(incoming)) {
    const v = incoming[key];
    if (v === null || v === undefined || v === '') continue;
    merged[key] = v;
  }
  return merged;
}

async function refreshRadiosondesActive() {
  try {
    const data = await getJson('/api/radiosondes?active_sec=' + ACTIVE_SONDE_MAX_AGE_SEC);
    const sondes = Array.isArray(data.radiosondes) ? data.radiosondes : [];
    for (const s of sondes) sondesCache.set(s.serial, mergeSondeRecord(sondesCache.get(s.serial), s));
    renderRadiosondesTable();
  } catch (e) {

  }
}

async function refreshCpu() {
  try {
    const c = await getJson('/api/cpu');
    const pct = Number(c.cpu_percent);
    setText('cpuLoad', Number.isFinite(pct) ? (pct.toFixed(1) + '%') : '-');
  } catch (e) {
    setText('cpuLoad', '-');
  }
}

// ---- Map ----

const TRACK_COLOR = '#0f6799';
let sondeIcon = null;
let predictionIcon = null;
let launchIcon = null;
let burstIcon = null;

function initSondeMap() {
  if (sondeMap || typeof L === 'undefined') return;
  const container = document.getElementById('sondeMap');
  if (!container) return;
  sondeMap = L.map('sondeMap').setView([52, 8], 7);
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    attribution: '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>'
  }).addTo(sondeMap);
  sondeIcon = L.icon({
    iconUrl: 'ballon.png',
    iconSize: [16, 21],
    iconAnchor: [7, 18],
    popupAnchor: [1, 1]
  });
  // Same icon + geometry wettersonde.net uses for its own predictionIcon.
  predictionIcon = L.icon({
    iconUrl: 'target.png',
    iconSize: [14, 14],
    iconAnchor: [7, 7],
    popupAnchor: [1, 1]
  });
  // First-frame marker: same inline-SVG "triangleIcon" wettersonde.net uses
  // for its own "sondeFirst" track marker (src/ws_icon.js) -- same size,
  // colors and anchor, ported 1:1 rather than approximated with CSS.
  launchIcon = L.icon({
    iconUrl:
      'data:image/svg+xml;utf8,' +
      encodeURIComponent(
        '<svg width="13" height="13" viewBox="0 0 13 13" xmlns="http://www.w3.org/2000/svg">' +
        '<polygon points="6.5,12 1,1 12,1" fill="#678eb9" stroke="#436c98" stroke-width="1"/>' +
        '</svg>'
      ),
    iconSize: [13, 13],
    iconAnchor: [6.5, 13],
    popupAnchor: [0, -13]
  });
  // Burst marker: same icon geometry as wettersonde.net's burstIcon
  // (images/pop-marker.png, 15x15, anchor 7,7) but using the user's own
  // burst.png, shipped alongside app.js/style.css.
  burstIcon = L.icon({
    iconUrl: 'burst.png',
    iconSize: [15, 15],
    iconAnchor: [7, 7],
    popupAnchor: [1, 1],
    className: 'sonde-burst-icon'
  });

  initMapLiveFilterButton();
  initMapFullscreenButton();
  initMapAzElBox();
}

// ---- Map fullscreen ----

function initMapFullscreenButton() {
  const btn = document.getElementById('mapFullscreenBtn');
  const wrap = document.querySelector('#mapview .map-wrap');
  if (!btn || !wrap) return;
  const requestFs = wrap.requestFullscreen || wrap.webkitRequestFullscreen;
  const exitFs = document.exitFullscreen || document.webkitExitFullscreen;
  if (!requestFs || !exitFs) { btn.style.display = 'none'; return; }

  const isFullscreen = () =>
    document.fullscreenElement === wrap || document.webkitFullscreenElement === wrap;

  const update = () => {
    const fs = isFullscreen();
    btn.textContent = fs ? 'Exit fullscreen' : 'Fullscreen';
    btn.setAttribute('aria-pressed', fs ? 'true' : 'false');
    if (sondeMap) setTimeout(() => sondeMap.invalidateSize(), 150);
  };

  btn.addEventListener('click', () => {
    if (isFullscreen()) {
      exitFs.call(document);
    } else {
      requestFs.call(wrap);
    }
  });
  document.addEventListener('fullscreenchange', update);
  document.addEventListener('webkitfullscreenchange', update);
  update();
}

// ---- Map azimuth/elevation/distance overlay ----
// Shown when a sonde marker is selected. Unlike wettersonde.net's own
// AzElControl (which uses the browser's GPS position as the observer),
// wsrx already computes distance_km/bearing_deg/elevation_deg server-side
// relative to the fixed receiver station for every sonde in
// /api/radiosondes -- so this just displays those values, no client-side
// geolocation or trig needed.

function initMapAzElBox() {
  const box = document.getElementById('mapAzElBox');
  if (!box) return;
  if (typeof L !== 'undefined') {
    L.DomEvent.disableClickPropagation(box);
    L.DomEvent.disableScrollPropagation(box);
  }
  const closeBtn = document.getElementById('mapAzElClose');
  if (closeBtn) closeBtn.addEventListener('click', () => deselectMapSonde());
  if (sondeMap) sondeMap.on('click', () => deselectMapSonde());
  if (mapAzElInterval) clearInterval(mapAzElInterval);
  mapAzElInterval = setInterval(updateMapAzElBox, 1000);
}

function selectMapSonde(serial) {
  mapAzElSerial = serial;
  const box = document.getElementById('mapAzElBox');
  if (box) box.classList.add('visible');
  const serialEl = document.getElementById('mapAzElSerial');
  if (serialEl) serialEl.textContent = serial;
  updateMapAzElBox();
}

function deselectMapSonde() {
  mapAzElSerial = null;
  const box = document.getElementById('mapAzElBox');
  if (box) box.classList.remove('visible');
}

function updateMapAzElBox() {
  if (!mapAzElSerial) return;
  const box = document.getElementById('mapAzElBox');
  if (!box) return;

  const timeEl = document.getElementById('mapAzElTime');
  if (timeEl) timeEl.textContent = new Date().toISOString().slice(11, 19) + ' UTC';

  const s = lastSondesData.find(x => x.serial === mapAzElSerial);
  const az = s && Number.isFinite(s.bearing_deg) ? Number(s.bearing_deg) : null;
  const el = s && Number.isFinite(s.elevation_deg) ? Number(s.elevation_deg) : null;
  const dist = s && Number.isFinite(s.distance_km) ? Number(s.distance_km) : null;

  const dataEl = document.getElementById('mapAzElData');
  if (dataEl) {
    dataEl.innerHTML =
      '<div class="map-azel-row"><span>Azimuth</span><span>' + (az !== null ? az.toFixed(0) + '°' : '-') + '</span></div>' +
      '<div class="map-azel-row"><span>Elevation</span><span>' + (el !== null ? el.toFixed(1) + '°' : '-') + '</span></div>' +
      '<div class="map-azel-row"><span>Distance</span><span>' + (dist !== null ? dist.toFixed(1) + ' km' : '-') + '</span></div>';
  }
  const needle = document.getElementById('mapAzElNeedle');
  if (needle) needle.style.transform = 'rotate(' + (az !== null ? az : 0) + 'deg)';
}

function updateMapLiveFilterButton(btn) {
  btn.classList.toggle('active', mapLiveOnly);
  btn.setAttribute('aria-pressed', mapLiveOnly ? 'true' : 'false');
}

function initMapLiveFilterButton() {
  const btn = document.getElementById('mapLiveFilterBtn');
  if (!btn) return;
  updateMapLiveFilterButton(btn);
  btn.addEventListener('click', () => {
    mapLiveOnly = !mapLiveOnly;
    try { localStorage.setItem(MAP_LIVE_ONLY_KEY, mapLiveOnly ? '1' : '0'); } catch (e) {}
    updateMapLiveFilterButton(btn);
    sondeMapBoundsFitted = false; // re-fit the view to whatever the new filter shows
    refreshSondeMap();
  });
}

function sondeLabelHtml(s) {
  const freqText = Number.isFinite(s.frequency) ? Number(s.frequency).toFixed(3) + 'MHz' : null;
  let html = '<b>' + escapeHtml(s.serial) + '</b>' + (freqText ? ' - ' + freqText : '');

  const nowSec = Date.now() / 1000;
  const stale = Number.isFinite(s.modified) && (nowSec - s.modified) > SONDE_FRESH_MAX_AGE_SEC;
  const color = stale ? '#900000' : '#436c98';
  const altText = Number.isFinite(s.last_altitude) ? Number(s.last_altitude).toFixed(1) + 'm' : '-';
  const vText = Number.isFinite(s.vel_v) ? Number(s.vel_v).toFixed(1) + 'm/s' : '-';
  const hText = Number.isFinite(s.vel_h) ? (Number(s.vel_h) * 3.6).toFixed(1) + 'km/h' : '-';
  html += '<br><b><font color="' + color + '">' + altText + '</font> ' + vText + ' ' + hText + '</b>';
  return html;
}

function bindSondeInteractivity(marker, serial) {
  // Clicking a sonde on the map only selects it (shows the azel overlay) --
  // unlike clicking a row in the radiosonde list, it should NOT open the
  // detail dialog.
  marker.on('click', () => selectMapSonde(serial));
  marker.on('tooltipopen', () => {
    const tooltip = marker.getTooltip();
    const el = tooltip && tooltip.getElement();
    if (!el || el.dataset.sondeClickBound === '1') return;
    el.dataset.sondeClickBound = '1';
    L.DomEvent.on(el, 'click', (ev) => {
      L.DomEvent.stop(ev);
      selectMapSonde(serial);
    });
  });
}

function renderSondePrediction(s, allPoints) {
  const pred = s.prediction;
  const track = pred && Array.isArray(pred.track)
    ? pred.track.filter(p => Number.isFinite(p[0]) && Number.isFinite(p[1])).map(p => [p[0], p[1]])
    : [];
  if (!track.length) return false;

  allPoints.push(...track);
  const landing = pred.landing && Number.isFinite(pred.landing.lat) && Number.isFinite(pred.landing.lon)
    ? [pred.landing.lat, pred.landing.lon]
    : null;
  if (landing) allPoints.push(landing);

  const landingTimeText = pred.landing && pred.landing.time ? fmtTime(pred.landing.time) : '-';
  const landingAltText = pred.landing && Number.isFinite(pred.landing.alt) ? fmtAltitude(pred.landing.alt) : '-';
  const landingTooltip = '<b>Predicted landing</b><br>' + landingAltText + '<br>' + landingTimeText;

  let entry = sondeMapPredictionLayers.get(s.serial);
  if (!entry) {
    const line = L.polyline(track, {
      color: PREDICTION_COLOR, weight: 2, opacity: 0.85, dashArray: '4, 6'
    }).addTo(sondeMap);
    let marker = null;
    if (landing) {
      marker = L.marker(landing, { icon: predictionIcon }).addTo(sondeMap);
      marker.bindTooltip(landingTooltip, { direction: 'top', className: 'balloon-label' });
    }
    sondeMapPredictionLayers.set(s.serial, { line, marker });
  } else {
    entry.line.setLatLngs(track);
    if (landing) {
      if (!entry.marker) {
        entry.marker = L.marker(landing, { icon: predictionIcon }).addTo(sondeMap);
      } else {
        entry.marker.setLatLng(landing);
      }
      entry.marker.setTooltipContent(landingTooltip);
    } else if (entry.marker) {
      sondeMap.removeLayer(entry.marker);
      entry.marker = null;
    }
  }
  return true;
}

// Launch (first track point) and burst (highest-altitude track point so far)
// markers, same idea as wettersonde.net's own track view. `track` here still
// carries altitude ([lat, lon, alt|null, time]) -- refreshSondeMap keeps that
// around for this, unlike the lat/lon-only `latlngs` used for the polyline.
function renderSondeLaunchBurst(s, track, allPoints) {
  if (track.length < 2) {
    const existingLaunch = sondeMapLaunchLayers.get(s.serial);
    if (existingLaunch) { sondeMap.removeLayer(existingLaunch); sondeMapLaunchLayers.delete(s.serial); }
    const existingBurst = sondeMapBurstLayers.get(s.serial);
    if (existingBurst) { sondeMap.removeLayer(existingBurst); sondeMapBurstLayers.delete(s.serial); }
    return false;
  }

  const launch = track[0];
  // Only treat this as a burst once the sonde is actually falling -- while
  // still ascending, the highest point so far is just "current altitude"
  // and showing the burst icon there would be misleading.
  const falling = Number.isFinite(s.vel_v) && s.vel_v < 0;
  let burst = null;
  if (falling) {
    for (const p of track) {
      if (Number.isFinite(p[2]) && (!burst || p[2] > burst[2])) burst = p;
    }
  }

  const launchLatLng = [launch[0], launch[1]];
  allPoints.push(launchLatLng);
  const launchAltText = Number.isFinite(launch[2]) ? fmtAltitude(launch[2]) : '-';
  const launchTimeText = launch[3] ? fmtTime(launch[3]) : '-';
  const launchTooltip = '<b>First position</b><br>at altitude ' + launchAltText + '<br>' + launchTimeText;

  let launchMarker = sondeMapLaunchLayers.get(s.serial);
  if (!launchMarker) {
    launchMarker = L.marker(launchLatLng, { icon: launchIcon }).addTo(sondeMap);
    launchMarker.bindTooltip(launchTooltip, { direction: 'top', className: 'balloon-label' });
    sondeMapLaunchLayers.set(s.serial, launchMarker);
  } else {
    launchMarker.setLatLng(launchLatLng);
    launchMarker.setTooltipContent(launchTooltip);
  }

  if (burst) {
    const burstLatLng = [burst[0], burst[1]];
    allPoints.push(burstLatLng);
    const burstAltText = fmtAltitude(burst[2]);
    const burstTimeText = burst[3] ? fmtTime(burst[3]) : '-';
    const burstTooltip = '<b>Burst</b><br>at altitude ' + burstAltText + '<br>' + burstTimeText;

    let burstMarker = sondeMapBurstLayers.get(s.serial);
    if (!burstMarker) {
      burstMarker = L.marker(burstLatLng, { icon: burstIcon }).addTo(sondeMap);
      burstMarker.bindTooltip(burstTooltip, { direction: 'top', className: 'balloon-label' });
      sondeMapBurstLayers.set(s.serial, burstMarker);
    } else {
      burstMarker.setLatLng(burstLatLng);
      burstMarker.setTooltipContent(burstTooltip);
    }
  } else {
    const existingBurst = sondeMapBurstLayers.get(s.serial);
    if (existingBurst) { sondeMap.removeLayer(existingBurst); sondeMapBurstLayers.delete(s.serial); }
  }

  return true;
}

async function refreshSondeMap() {
  if (!sondeMap) return;
  let data;
  try {
    data = await getJson('/api/radiosondes?hours=' + MAP_HOURS + '&tracks=1');
  } catch (e) {
    return;
  }

  let sondes = Array.isArray(data.radiosondes) ? data.radiosondes : [];
  lastSondesData = sondes;
  const totalCount = sondes.length;
  if (mapLiveOnly) {
    const nowSec = Date.now() / 1000;
    sondes = sondes.filter(s => Number.isFinite(s.modified) && (nowSec - s.modified) <= SONDE_FRESH_MAX_AGE_SEC);
  }
  const seen = new Set();
  const predSeen = new Set();
  const launchBurstSeen = new Set();
  const allPoints = [];

  for (const s of sondes) {
    if (!s.serial) continue;

    // Keep altitude/time here (needed for the launch/burst markers below);
    // `latlngs` below is the lat/lon-only view the polyline/bounds use.
    const track = Array.isArray(s.track)
      ? s.track.filter(p => Number.isFinite(p[0]) && Number.isFinite(p[1])).map(p => [p[0], p[1], p[2], p[3]])
      : [];
    let latlngs = track.map(p => [p[0], p[1]]);
    if (!latlngs.length && Number.isFinite(s.last_latitude) && Number.isFinite(s.last_longitude)) {
      latlngs = [[s.last_latitude, s.last_longitude]];
    }
    if (latlngs.length) {
      seen.add(s.serial);
      allPoints.push(...latlngs);

      const last = latlngs[latlngs.length - 1];
      const labelHtml = sondeLabelHtml(s);
      let entry = sondeMapLayers.get(s.serial);
      if (!entry) {
        const line = L.polyline(latlngs, { color: TRACK_COLOR, weight: 2, opacity: 0.8 }).addTo(sondeMap);
        const marker = L.marker(last, { icon: sondeIcon }).addTo(sondeMap);
        marker.bindTooltip(labelHtml, {
          permanent: true,
          direction: 'top',
          offset: [0, -20],
          opacity: 0.8,
          className: 'balloon-label',
          interactive: true
        });
        bindSondeInteractivity(marker, s.serial);
        entry = { line, marker };
        sondeMapLayers.set(s.serial, entry);
      } else {
        entry.line.setLatLngs(latlngs);
        entry.marker.setLatLng(last);
        entry.marker.setTooltipContent(labelHtml);
      }
    }

    if (renderSondePrediction(s, allPoints)) predSeen.add(s.serial);
    if (renderSondeLaunchBurst(s, track, allPoints)) launchBurstSeen.add(s.serial);
  }

  for (const [serial, entry] of sondeMapLayers) {
    if (!seen.has(serial)) {
      sondeMap.removeLayer(entry.line);
      sondeMap.removeLayer(entry.marker);
      sondeMapLayers.delete(serial);
    }
  }

  for (const [serial, entry] of sondeMapPredictionLayers) {
    if (!predSeen.has(serial)) {
      sondeMap.removeLayer(entry.line);
      if (entry.marker) sondeMap.removeLayer(entry.marker);
      sondeMapPredictionLayers.delete(serial);
    }
  }

  for (const [serial, marker] of sondeMapLaunchLayers) {
    if (!launchBurstSeen.has(serial)) {
      sondeMap.removeLayer(marker);
      sondeMapLaunchLayers.delete(serial);
    }
  }
  for (const [serial, marker] of sondeMapBurstLayers) {
    if (!launchBurstSeen.has(serial)) {
      sondeMap.removeLayer(marker);
      sondeMapBurstLayers.delete(serial);
    }
  }

  if (!sondeMapBoundsFitted && allPoints.length) {
    sondeMap.fitBounds(allPoints, { padding: [30, 30], maxZoom: 12 });
    sondeMapBoundsFitted = true;
  }

  updateMapAzElBox();

  const info = document.getElementById('mapInfo');
  if (info) {
    if (!totalCount) {
      info.textContent = 'No radiosondes received in the last ' + MAP_HOURS + ' hours';
    } else if (mapLiveOnly) {
      info.textContent = 'Showing ' + seen.size + ' live radiosonde(s) (updated within the last '
        + SONDE_FRESH_MAX_AGE_SEC + 's) · updates automatically';
    } else {
      info.textContent = 'Showing ' + seen.size + ' radiosonde(s) from the last ' + MAP_HOURS + ' hours · updates automatically';
    }
  }
}

let lastRadiosondesRefresh = 0;

async function refreshAll() {
  await refreshStatus();
  await refreshCpu();
  await refreshSpectrum();
  if (activeTab === 'log') setLogText(await getText('/api/log?lines=300'));

  if (activeTab === 'radiosondes') {

    const now = Date.now();
    if (now - lastRadiosondesRefresh >= 4000) {
      lastRadiosondesRefresh = now;
      await refreshRadiosondesActive();
    }
  }

  if (activeTab === 'mapview' && sondeMap) {
    const now = Date.now();
    if (now - lastMapRefresh >= MAP_REFRESH_MS) {
      lastMapRefresh = now;
      await refreshSondeMap();
    }
  }
}

async function action(cmd) {
  if (cmd === 'stop' && !confirm('Really stop wsrx?')) return;
  if (cmd === 'clearlogs' && !confirm('Really delete wsrx.log, wsrx-web.log and all radiosonde logs? This cannot be undone.')) return;
  if (cmd === 'update' && !confirm(
    'Start update?\n\n' +
    'update.sh will stop wsrx AND this web interface, pull from git, rebuild, ' +
    'and restart both. This page will go quiet for a bit and pick back up on its own ' +
    'once the rebuild is done (can take a few minutes). Continue?'
  )) return;
  const r = await fetch('/api/' + cmd, { method: 'POST' });
  const t = await r.text();
  setText('statusText', t);
  if (cmd === 'clearlogs') {
    sondesCache = new Map();
    renderRadiosondesTable();
  }
  if (cmd === 'update') {
    const updateStatusEl = document.getElementById('updateStatus');
    if (updateStatusEl) {
      updateStatusEl.textContent = t.split('\n')[0];
      updateStatusEl.classList.toggle('ok', r.ok);
      updateStatusEl.classList.toggle('bad', !r.ok);
    }
  }
  setTimeout(refreshAll, 700);
}

const FILE_EDITORS = {
  config: { url: '/api/config', textareaId: 'configText', statusId: 'configStatus', dirty: false },
  whitelist: { url: '/api/whitelist', textareaId: 'whitelistText', statusId: 'whitelistStatus', dirty: false },
  blacklist: { url: '/api/blacklist', textareaId: 'blacklistText', statusId: 'blacklistStatus', dirty: false },
};

function editorTextarea(key) {
  return document.getElementById(FILE_EDITORS[key].textareaId);
}

function setEditorStatus(key, msg, kind) {
  const el = document.getElementById(FILE_EDITORS[key].statusId);
  if (!el) return;
  el.textContent = msg || '';
  el.classList.toggle('ok', kind === 'ok');
  el.classList.toggle('bad', kind === 'bad');
}

async function loadEditor(key, force) {
  const ed = FILE_EDITORS[key];
  if (!ed || (ed.dirty && !force)) return;
  const ta = editorTextarea(key);
  if (!ta) return;
  try {
    ta.value = await getText(ed.url);
    ed.dirty = false;
    setEditorStatus(key, '');
  } catch (e) {
    setEditorStatus(key, 'Could not load file', 'bad');
  }
}

function markEditorDirty(key) {
  FILE_EDITORS[key].dirty = true;
  setEditorStatus(key, 'Unsaved changes');
}

async function saveEditor(key) {
  const ed = FILE_EDITORS[key];
  const ta = editorTextarea(key);
  if (!ed || !ta) return;
  setEditorStatus(key, 'Saving…');
  try {
    const r = await fetch(ed.url, { method: 'POST', body: ta.value });
    const t = await r.text();
    if (r.ok) {
      ed.dirty = false;
      setEditorStatus(key, 'Saved – restart wsrx to apply', 'ok');
    } else {
      setEditorStatus(key, 'Save failed: ' + t.trim(), 'bad');
    }
  } catch (e) {
    setEditorStatus(key, 'Save failed', 'bad');
  }
}

function reloadEditor(key) {
  const ed = FILE_EDITORS[key];
  if (ed && ed.dirty && !confirm('Discard unsaved changes?')) return;
  loadEditor(key, true);
}

Object.keys(FILE_EDITORS).forEach(key => {
  const ta = editorTextarea(key);
  if (ta) ta.addEventListener('input', () => markEditorDirty(key));
});

document.querySelectorAll('[data-editor-action]').forEach(btn => {
  btn.addEventListener('click', () => {
    const key = btn.dataset.editorKey;
    if (btn.dataset.editorAction === 'save') saveEditor(key);
    else if (btn.dataset.editorAction === 'reload') reloadEditor(key);
  });
});

window.addEventListener('beforeunload', (e) => {
  const anyDirty = Object.values(FILE_EDITORS).some(ed => ed.dirty);
  if (anyDirty) { e.preventDefault(); e.returnValue = ''; }
});

function showTab(id, btn) {
  activeTab = id;
  document.querySelectorAll('.pane').forEach(p => p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  btn.classList.add('active');
  if (id === 'radiosondes') {
    lastRadiosondesRefresh = Date.now();
    loadRadiosondesFull();
  }
  if (id === 'mapview') {
    initSondeMap();
    setTimeout(() => { if (sondeMap) sondeMap.invalidateSize(); }, 0);
    lastMapRefresh = Date.now();
    refreshSondeMap();
  }
  if (FILE_EDITORS[id]) loadEditor(id, false);
  refreshAll();
}

document.querySelectorAll('[data-tab]').forEach(btn => {
  btn.addEventListener('click', () => showTab(btn.dataset.tab, btn));
});

// ---- Radar ----
let radarInterval = null;
let radarStation = null; // {lat, lon, alt}

const RADAR_DEG_TO_RAD = Math.PI / 180.0;
const RADAR_EARTH_RADIUS = 6371000.0;

function radarToRadians(degrees) { return degrees * RADAR_DEG_TO_RAD; }

function radarCalculateLookAngles(a, b) {
  const aLat = a.lat * RADAR_DEG_TO_RAD, aLon = a.lon * RADAR_DEG_TO_RAD;
  const bLat = b.lat * RADAR_DEG_TO_RAD, bLon = b.lon * RADAR_DEG_TO_RAD;

  const dLon = bLon - aLon;
  const sa = Math.cos(bLat) * Math.sin(dLon);
  const sb = (Math.cos(aLat) * Math.sin(bLat)) - (Math.sin(aLat) * Math.cos(bLat) * Math.cos(dLon));
  let bearing = Math.atan2(sa, sb);
  const aa = Math.sqrt(sa * sa + sb * sb);
  const ab = (Math.sin(aLat) * Math.sin(bLat)) + (Math.cos(aLat) * Math.cos(bLat) * Math.cos(dLon));
  const angleAtCentre = Math.atan2(aa, ab);

  const ta = RADAR_EARTH_RADIUS + a.alt;
  const tb = RADAR_EARTH_RADIUS + b.alt;
  const ea = (Math.cos(angleAtCentre) * tb) - ta;
  const eb = Math.sin(angleAtCentre) * tb;
  const elevation = Math.atan2(ea, eb) / RADAR_DEG_TO_RAD;
  const distance = Math.sqrt(ta * ta + tb * tb - 2 * tb * ta * Math.cos(angleAtCentre));

  bearing += (bearing < 0) ? 2 * Math.PI : 0;
  bearing /= RADAR_DEG_TO_RAD;

  return { elevation, azimuth: bearing, range: distance };
}

function radarText(ctx, top, left, size, color, text) {
  ctx.fillStyle = color;
  ctx.font = size + 'px Arial';
  ctx.fillText(text, left, top);
}

function radarDrawLine(ctx, distancePx, degree, color) {
  const hyp = (140 / 300 * distancePx);
  const geg = Math.sin(radarToRadians(degree)) * hyp;
  const ank = Math.cos(radarToRadians(degree)) * hyp;
  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.moveTo(ank + 200, 200 - geg);
  ctx.lineTo(200, 200);
  ctx.stroke();
}

function radarDrawSonde(ctx, distanceKm, degree, label, isClosest, closestText) {
  let hyp = (150 / 100 * distanceKm);
  if (hyp > 150) hyp = 150;
  const geg = Math.sin(radarToRadians(degree)) * hyp;
  const ank = Math.cos(radarToRadians(degree)) * hyp;
  const textColor = getThemeColor('--axis-title', '#c9d1d9');

  if (isClosest) {
    radarText(ctx, 180 - ank, geg + 175, 12, textColor, label);
    radarText(ctx, 192 - ank, geg + 155, 12, textColor, closestText);
  } else {
    radarText(ctx, 192 - ank, geg + 175, 11, textColor, label);
  }

  let color = getThemeColor('--accent', '#2f80b8');
  if (distanceKm > 100) color = getThemeColor('--bad', '#b23327');
  else if (distanceKm < 20) color = getThemeColor('--ok', '#1f7a30');

  ctx.beginPath();
  ctx.strokeStyle = color;
  ctx.lineWidth = 2;
  ctx.arc(geg + 200, 200 - ank, 5, 0, 2 * Math.PI, false);
  ctx.stroke();
}

let radarBaseCanvas = null;
let radarBaseKey = '';

function radarGetBaseCanvas(stationLabel) {
  const theme = document.documentElement.getAttribute('data-theme') || 'dark';
  const key = theme + '|' + stationLabel;
  if (!radarBaseCanvas || radarBaseKey !== key) {
    radarBaseCanvas = document.createElement('canvas');
    radarBaseCanvas.width = 400;
    radarBaseCanvas.height = 400;
    radarDrawBase(radarBaseCanvas.getContext('2d'), stationLabel);
    radarBaseKey = key;
  }
  return radarBaseCanvas;
}

function radarDrawBase(ctx, stationLabel) {
  const bgColor = getThemeColor('--plot-bg', '#0c1828');
  const gridColor = getThemeColor('--plot-grid', '#507ba8');
  const textColor = getThemeColor('--axis-title', '#c9d1d9');
  const legendColor = getThemeColor('--bad', '#b23327');
  const legendColor2 = getThemeColor('--accent', '#2f80b8');
  const legendColor3 = getThemeColor('--ok', '#1f7a30');

  ctx.strokeStyle = bgColor;
  ctx.fillStyle = bgColor;
  ctx.fillRect(0, 0, 400, 400);

  for (let s = 0; s < 360; s += 10) radarDrawLine(ctx, 330, s, gridColor);
  for (let s = 0; s < 360; s += 10) radarDrawLine(ctx, 310, s, bgColor);

  ctx.strokeStyle = gridColor;
  ctx.strokeRect(40, 200, 320, 0);
  ctx.strokeRect(200, 40, 0, 320);
  ctx.strokeRect(200, 200, 150, 1);
  ctx.strokeRect(230, 195, 0, 10);
  ctx.strokeRect(260, 195, 0, 10);
  ctx.strokeRect(290, 195, 0, 10);
  ctx.strokeRect(320, 195, 0, 10);

  radarText(ctx, 30, 196, 12, textColor, 'N');
  radarText(ctx, 380, 196, 12, textColor, 'S');
  radarText(ctx, 206, 20, 12, textColor, 'W');
  radarText(ctx, 206, 370, 12, textColor, 'O');

  radarText(ctx, 215, 220, 11, textColor, '20');
  radarText(ctx, 215, 250, 11, textColor, '40');
  radarText(ctx, 215, 280, 11, textColor, '60');
  radarText(ctx, 215, 310, 11, textColor, '80');
  radarText(ctx, 215, 345, 11, textColor, '100');
  radarText(ctx, 225, 348, 11, textColor, 'km');

  [150, 30, 60, 90, 120].forEach(r => {
    ctx.beginPath();
    ctx.strokeStyle = gridColor;
    ctx.lineWidth = 2;
    ctx.arc(200, 200, r, 0, 2 * Math.PI, false);
    ctx.stroke();
  });

  radarDrawLine(ctx, 350, 45, gridColor);
  radarDrawLine(ctx, 350, 135, gridColor);
  radarDrawLine(ctx, 350, 315, gridColor);
  radarDrawLine(ctx, 350, 225, gridColor);

  ctx.beginPath(); ctx.strokeStyle = legendColor; ctx.lineWidth = 2; ctx.arc(343, 10, 5, 0, 2 * Math.PI, false); ctx.stroke();
  radarText(ctx, 13, 353, 11, textColor, '> 100km');
  ctx.beginPath(); ctx.strokeStyle = legendColor2; ctx.lineWidth = 2; ctx.arc(343, 25, 5, 0, 2 * Math.PI, false); ctx.stroke();
  radarText(ctx, 28, 353, 11, textColor, '< 100km');
  ctx.beginPath(); ctx.strokeStyle = legendColor3; ctx.lineWidth = 2; ctx.arc(343, 40, 5, 0, 2 * Math.PI, false); ctx.stroke();
  radarText(ctx, 43, 353, 11, textColor, '< 20km');

  radarText(ctx, 13, 5, 12, textColor, stationLabel);
}

async function radarLoadStation() {
  if (radarStation) return radarStation;
  const configText = await getText('/api/config');
  const lat = parseFloat(iniValue(configText, 'station', 'lat'));
  const lon = parseFloat(iniValue(configText, 'station', 'lon'));
  const alt = parseFloat(iniValue(configText, 'station', 'alt'));
  radarStation = {
    lat: Number.isFinite(lat) ? lat : 0,
    lon: Number.isFinite(lon) ? lon : 0,
    alt: Number.isFinite(alt) ? alt : 0,
  };
  return radarStation;
}

async function radarDrawFrame() {
  const canvas = document.getElementById('radarCanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const station = await radarLoadStation();
  const stationEl = document.getElementById('radarStation');
  const closestEl = document.getElementById('radarClosest');

  let data;
  try {
    data = await getJson('/api/radiosondes');
  } catch (e) {
    if (closestEl) closestEl.textContent = 'Closest sonde: could not load data';
    return;
  }

  if (stationEl) {
    stationEl.textContent = 'Station: ' + station.lat.toFixed(5) + ', ' + station.lon.toFixed(5) +
      ' (' + station.alt.toFixed(0) + ' m)';
  }
  const stationLabel = 'Position: ' + station.lat.toFixed(5) + ' ' + station.lon.toFixed(5) + ' ' + station.alt.toFixed(0) + 'm';
  const base = radarGetBaseCanvas(stationLabel);

  const RADAR_MAX_AGE_SEC = 10 * 60;
  const nowSec = Date.now() / 1000;
  const sondes = (Array.isArray(data.radiosondes) ? data.radiosondes : [])
    .filter(s => Number.isFinite(s.last_latitude) && Number.isFinite(s.last_longitude))
    .filter(s => Number.isFinite(s.modified) && (nowSec - s.modified) <= RADAR_MAX_AGE_SEC);

  ctx.clearRect(0, 0, 400, 400);
  ctx.drawImage(base, 0, 0);

  if (!sondes.length) {
    if (closestEl) closestEl.textContent = 'Closest sonde: none tracked in the last 10 minutes';
    return;
  }

  const a = { lat: station.lat, lon: station.lon, alt: station.alt };
  let closest = null;

  const withLook = sondes.map(s => {
    const b = { lat: s.last_latitude, lon: s.last_longitude, alt: s.last_altitude || 0 };
    const look = radarCalculateLookAngles(a, b);
    const distanceKm = look.range / 1000.0;
    const degree = Math.round(look.azimuth);
    if (!closest || distanceKm < closest.distanceKm) {
      closest = { serial: s.serial, distanceKm, azimuth: degree, elevation: look.elevation };
    }
    return { serial: s.serial, distanceKm, degree };
  });

  withLook.forEach(s => {
    const isClosest = !!closest && s.serial === closest.serial;
    const closestText = isClosest
      ? closest.elevation.toFixed(2) + '\u00b0 - ' + closest.azimuth + '\u00b0 - ' + closest.distanceKm.toFixed(1) + 'km'
      : '';
    radarDrawSonde(ctx, s.distanceKm, s.degree, s.serial, isClosest, closestText);
  });

  if (closest && closestEl) {
    closestEl.textContent = 'Closest sonde: ' + closest.serial + ' \u2013 ' + closest.distanceKm.toFixed(1) +
      ' km, az ' + closest.azimuth + '\u00b0, el ' + closest.elevation.toFixed(1) + '\u00b0';
  }
}

function openRadar() {
  const dialog = document.getElementById('radarDialog');
  if (!dialog) return;
  if (typeof dialog.showModal === 'function') dialog.showModal();
  else dialog.setAttribute('open', '');
  radarDrawFrame();
  if (radarInterval) clearInterval(radarInterval);
  radarInterval = setInterval(radarDrawFrame, 3000);
}

function closeRadar() {
  const dialog = document.getElementById('radarDialog');
  if (radarInterval) { clearInterval(radarInterval); radarInterval = null; }
  if (dialog) {
    if (typeof dialog.close === 'function') dialog.close();
    else dialog.removeAttribute('open');
  }
}

document.getElementById('radarCloseBtn')?.addEventListener('click', closeRadar);
document.getElementById('radarDialog')?.addEventListener('cancel', closeRadar);
document.getElementById('radarDialog')?.addEventListener('click', (e) => {
  if (e.target === document.getElementById('radarDialog')) closeRadar();
});

document.querySelectorAll('[data-action]').forEach(btn => {
  btn.addEventListener('click', () => {
    if (btn.dataset.action === 'radar') { openRadar(); return; }
    if (btn.dataset.action === 'sonde-filter') {
      radiosondesHours = parseInt(btn.dataset.hours, 10) || 0;
      document.querySelectorAll('[data-action="sonde-filter"]').forEach(b => b.classList.toggle('active', b === btn));
      lastRadiosondesRefresh = Date.now();
      loadRadiosondesFull();
      return;
    }
    action(btn.dataset.action);
  });
});

function fmtUnixTime(v) {
  if (v === null || v === undefined || !Number.isFinite(Number(v)) || Number(v) <= 0) return '-';
  return new Date(Number(v) * 1000).toISOString().replace('T', ' ').replace('.000Z', ' UTC').replace('Z', ' UTC');
}

function fmt1(v) {
  return (v === null || v === undefined || v === '') ? null : v;
}

function joinParts(parts, sep) {
  const filtered = parts.filter(p => p !== null && p !== undefined && p !== '');
  return filtered.length ? filtered.join(sep) : null;
}

const SONDE_DETAIL_ROWS = [
  { label: 'Serial', render: d => fmt1(d.serial) },
  { label: 'Type / Frequency', render: d => joinParts(
      [fmt1(d.type), d.wsrx_frequency != null ? fmtFrequency(d.wsrx_frequency) : null], ' @ ') },
  { label: 'Encrypted', render: d => d.encrypted != null ? (d.encrypted ? 'Yes' : 'No') : null },
  { label: 'Frame # / Frames received', render: d => joinParts(
      [d.frame != null ? '#' + d.frame : null,
       d.frames != null ? d.frames + ' received' : null], ', ') },
  { label: 'GPS datetime', render: d => fmt1(d.datetime) },
  { label: 'Datetime / Position reference', render: d => joinParts([fmt1(d.ref_datetime), fmt1(d.ref_position)], ', ') },
  { label: 'First seen', render: d => fmt1(d.first_time) },
  { label: 'Last update', render: d => d.modified != null ? fmtUnixTime(d.modified) : null },
  { label: 'Distance / Bearing / Elevation', render: d => joinParts(
      [d.distance_km != null ? fmtDistance(d.distance_km) : null,
       d.bearing_deg != null ? Number(d.bearing_deg).toFixed(0) + '\u00b0 az' : null,
       d.elevation_deg != null ? Number(d.elevation_deg).toFixed(1) + '\u00b0 el' : null], ', ') },
  { label: 'Latitude / Longitude', render: d => joinParts(
      [d.lat != null ? Number(d.lat).toFixed(5) + '\u00b0' : null,
       d.lon != null ? Number(d.lon).toFixed(5) + '\u00b0' : null], ', ') },
  { label: 'Altitude (first / last)', render: d => {
      const last = d.alt != null ? d.alt : d.altitude;
      return joinParts(
        [d.first_altitude != null ? 'First: ' + fmtAltitude(d.first_altitude) : null,
         last != null ? 'Last: ' + fmtAltitude(last) : null], ', ');
    } },
  { label: 'Horizontal speed / Heading / Climb rate', render: d => joinParts(
      [d.vel_h != null ? Number(d.vel_h).toFixed(1) + ' m/s (' + (Number(d.vel_h) * 3.6).toFixed(1) + ' km/h)' : null,
       d.heading != null ? Number(d.heading).toFixed(1) + '\u00b0' : null,
       d.vel_v != null ? Number(d.vel_v).toFixed(1) + ' m/s climb' : null], ', ') },
  { label: 'Satellites', render: d => fmt1(d.sats != null ? d.sats : d.sat) },
  { label: 'Temperature / Humidity / Pressure', render: d => joinParts(
      [d.temp != null ? Number(d.temp).toFixed(1) + ' \u00b0C' : null,
       d.humidity != null ? Number(d.humidity).toFixed(1) + ' %' : null,
       d.pressure != null ? Number(d.pressure).toFixed(2) + ' hPa' : null], ', ') },
  { label: 'Battery', render: d => d.batt != null ? Number(d.batt).toFixed(2) + ' V' : null },
  { label: 'RSSI', render: d => d.rssi != null ? Number(d.rssi).toFixed(1) + ' dBm' : null },
  // Raw sonde-internal tx power code from rs41mod's "tx_power_raw" (currently
  // RS41 only). Not a calibrated dBm value -- rs41mod.c surfaces the raw
  // STATUS-block byte as-is, so this is shown as a plain code, not "X dBm".
  { label: 'TX power (raw code)', render: d => d.tx_power_raw != null ? String(d.tx_power_raw) : null },
  { label: 'Burst-kill timer', render: d => {
      const v = d.burstkilltimer != null ? d.burstkilltimer : d.bt;
      return v != null ? v + ' s' : null;
    } },
  { label: 'Kill timer', render: d => d.killtimer != null ? d.killtimer + ' s' : null },
  { label: 'Mainboard / Firmware', render: d => {
      if (!d.type || String(d.type).toUpperCase().indexOf('RS41') === -1) return null;
      return joinParts(
        [fmt1(d.rs41_mainboard),
         d.rs41_mainboard_fw != null ? 'FW ' + d.rs41_mainboard_fw : null], ', ');
    } },
  { label: 'Aux data', render: d => fmt1(d.aux) },
  // Decoded OIF411 (Vaisala ozone interface) XDATA, when the sonde's "aux"
  // field is a 20-hex-digit OIF411 string (instrument type 5). Same field
  // layout/scaling as wettersonde.net's own decodeOzoneXdata() (sonde.php).
  { label: 'Ozone: pump temperature', render: d => d.o3_pump_temperature_c != null ? Number(d.o3_pump_temperature_c).toFixed(2) + ' °C' : null },
  { label: 'Ozone: sensor current', render: d => d.o3_current_ua != null ? Number(d.o3_current_ua).toFixed(4) + ' µA' : null },
  { label: 'Ozone: pump current', render: d => d.o3_pump_current_ma != null ? d.o3_pump_current_ma + ' mA' : null },
  { label: 'Ozone: interface battery', render: d => d.o3_battery_v != null ? Number(d.o3_battery_v).toFixed(1) + ' V' : null },
  { label: 'Ozone: external voltage', render: d => d.o3_external_v != null ? Number(d.o3_external_v).toFixed(1) + ' V' : null },
];

const SONDE_DETAIL_CONSUMED_KEYS = new Set([
  'serial', 'type', 'wsrx_frequency', 'encrypted', 'frame', 'datetime', 'ref_datetime', 'ref_position',
  'first_time', 'modified', 'frames', 'distance_km', 'bearing_deg', 'elevation_deg', 'lat', 'lon', 'first_altitude', 'alt', 'altitude',
  'vel_h', 'heading', 'vel_v', 'sats', 'sat', 'temp', 'humidity', 'pressure', 'batt', 'rssi',
  'burstkilltimer', 'bt', 'killtimer', 'aux', 'rs41_mainboard', 'rs41_mainboard_fw', 'tx_power_raw',
  'o3_instrument_number', 'o3_pump_temperature_c', 'o3_current_ua', 'o3_battery_v', 'o3_pump_current_ma', 'o3_external_v',
]);

function openSondeDetail(serial) {
  const dialog = document.getElementById('sondeDialog');
  if (!dialog || !serial) return;
  const title = document.getElementById('sondeDialogTitle');
  if (title) title.textContent = 'Radiosonde ' + serial;
  const mapLink = document.getElementById('sondeMapLink');
  if (mapLink) mapLink.href = 'https://www.wettersonde.net/map.php?sonde=' + encodeURIComponent(serial);
  if (typeof dialog.showModal === 'function') dialog.showModal();
  else dialog.setAttribute('open', '');
  loadSondeDetail(serial);
}

function closeSondeDetail() {
  const dialog = document.getElementById('sondeDialog');
  if (dialog) {
    if (typeof dialog.close === 'function') dialog.close();
    else dialog.removeAttribute('open');
  }
}

async function loadSondeDetail(serial) {
  const tbody = document.querySelector('#sondeDetailTable tbody');
  if (!tbody) return;
  tbody.innerHTML = '<tr><td colspan="2">loading...</td></tr>';
  let data;
  try {
    data = await getJson('/api/radiosonde?serial=' + encodeURIComponent(serial));
  } catch (e) {
    tbody.innerHTML = '<tr><td colspan="2">Could not load sonde details</td></tr>';
    return;
  }
  if (data.error) {
    tbody.innerHTML = '<tr><td colspan="2">' + escapeHtml(data.error) + '</td></tr>';
    return;
  }

  let rowsHtml = '';
  for (const row of SONDE_DETAIL_ROWS) {
    const val = row.render(data);
    if (val === null || val === undefined || val === '') continue;
    rowsHtml += '<tr><td>' + escapeHtml(row.label) + '</td><td>' + escapeHtml(val) + '</td></tr>';
  }
  // Anything the backend sends that isn't part of a known row above still
  // shows up, just without friendly formatting/grouping.
  for (const key of Object.keys(data)) {
    if (SONDE_DETAIL_CONSUMED_KEYS.has(key)) continue;
    const v = data[key];
    if (v === null || v === undefined || v === '') continue;
    rowsHtml += '<tr><td>' + escapeHtml(key) + '</td><td>' + escapeHtml(v) + '</td></tr>';
  }

  tbody.innerHTML = rowsHtml || '<tr><td colspan="2">No data</td></tr>';
}

document.getElementById('sondeCloseBtn')?.addEventListener('click', closeSondeDetail);
document.getElementById('sondeDialog')?.addEventListener('cancel', closeSondeDetail);
document.getElementById('sondeDialog')?.addEventListener('click', (e) => {
  if (e.target === document.getElementById('sondeDialog')) closeSondeDetail();
});

document.getElementById('refreshBtn').addEventListener('click', refreshAll);
window.addEventListener('resize', () => { if (lastSpectrum) drawSpectrum(lastSpectrum, lastPeaks); });

(function initControlsMenu() {
  const toggleBtn = document.getElementById('menuToggle');
  const actionsEl = document.getElementById('menuActions');
  if (!toggleBtn || !actionsEl) return;

  function setOpen(open) {
    actionsEl.classList.toggle('open', open);
    toggleBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
  }

  toggleBtn.addEventListener('click', (e) => {
    e.stopPropagation();
    setOpen(!actionsEl.classList.contains('open'));
  });


  actionsEl.addEventListener('click', (e) => {
    if (e.target.tagName === 'BUTTON') setOpen(false);
  });

  document.addEventListener('click', (e) => {
    if (!actionsEl.contains(e.target) && e.target !== toggleBtn) setOpen(false);
  });

  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') setOpen(false);
  });
})();

const themeToggleBtn = document.getElementById('themeToggle');
if (themeToggleBtn) themeToggleBtn.addEventListener('click', toggleTheme);
initTheme();

function initSpectrumTooltip() {
  const canvas = document.getElementById('spectrumCanvas');
  const tooltip = document.getElementById('spectrumTooltip');
  if (!canvas || !tooltip) return;
  const PEAK_SNAP_CSS = 8;
  const IDLE_DELAY_MS = 150;
  let idleTimer = null;
  let pendingPos = null;

  function updateTooltip(mx, my) {
    if (!lastPlotMeta || !lastPlotMeta.points.length) {
      tooltip.style.display = 'none';
      return;
    }
    const { points, minF, maxF, minP, maxP, padL, padT, plotW, plotH, dpr } = lastPlotMeta;
    const padLCss = padL / dpr, padTCss = padT / dpr, plotWCss = plotW / dpr, plotHCss = plotH / dpr;

    if (mx < padLCss || mx > padLCss + plotWCss || my < padTCss || my > padTCss + plotHCss) {
      tooltip.style.display = 'none';
      canvas.style.cursor = 'default';
      return;
    }

    // pixel position -> frequency, then find the nearest measured spectrum bin
    const freq = minF + (mx - padLCss) / plotWCss * (maxF - minF);
    let lo = 0, hi = points.length - 1;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (points[mid][0] < freq) lo = mid + 1; else hi = mid;
    }
    if (lo > 0 && Math.abs(points[lo - 1][0] - freq) < Math.abs(points[lo][0] - freq)) lo -= 1;
    const pt = points[lo];

    // if the cursor is close to a detected peak, snap to it and label it as such
    let hit = null, bestDist = Infinity;
    for (const p of peakHitboxes) {
      const d = Math.abs(p.xCss - mx);
      if (d < bestDist) { bestDist = d; hit = p; }
    }
    const isPeak = hit && bestDist <= PEAK_SNAP_CSS;

    const freqMhz = isPeak ? hit.freqMhz : pt[0];
    const powerDb = isPeak ? hit.powerDb : pt[1];
    tooltip.textContent = `${freqMhz.toFixed(3)} MHz | ${powerDb.toFixed(1)} dB${isPeak ? ' (Peak)' : ''}`;
    tooltip.style.left = mx + 'px';
    tooltip.style.top = (my - 20) + 'px';
    tooltip.style.display = 'block';
    canvas.style.cursor = 'crosshair';
  }

  canvas.addEventListener('mousemove', (e) => {
    const rect = canvas.getBoundingClientRect();
    pendingPos = { mx: e.clientX - rect.left, my: e.clientY - rect.top };
    tooltip.style.display = 'none';
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = setTimeout(() => {
      if (pendingPos) updateTooltip(pendingPos.mx, pendingPos.my);
    }, IDLE_DELAY_MS);
  });

  canvas.addEventListener('mouseleave', () => {
    if (idleTimer) clearTimeout(idleTimer);
    pendingPos = null;
    tooltip.style.display = 'none';
  });
}

async function loadVersion() {
  try {
    const v = await getJson('/api/version');
    setText('appVersion', v && v.version ? ('v' + v.version) : '');
  } catch (e) {
    setText('appVersion', '');
  }
}

initSpectrumTooltip();
loadVersion();
setInterval(refreshAll, 500);
refreshAll();