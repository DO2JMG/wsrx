let activeTab = 'log';
let lastSpectrum = null;
let lastPeaks = null;
let peakHitboxes = [];
let lastPlotMeta = null;
let activeChannelFreqs = [];
let spectrumAxisMinP = null;
let spectrumAxisMaxP = null;

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
    setText('path', cachedCallsign ? ('Receiver: ' + cachedCallsign) : '');
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

  const peakText = peaks.length ? peaks.map(p => p[0].toFixed(3) + ' MHz').join(', ') : 'none';
  const liveTs = spec.timestamp || '-';
  const scanTs = peaksDoc && peaksDoc.timestamp ? peaksDoc.timestamp : '-';
  const noiseText = Number.isFinite(noise) ? noise.toFixed(1) : '-';
  const trigText = Number.isFinite(trig) ? trig.toFixed(1) : '-';
  info.textContent = `Live: ${liveTs} | Last scan: ${scanTs} | Noise ${noiseText} dB | Trigger ${trigText} dB | Peaks: ${peakText}`;
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

function fmtFrequency(v) {
  if (v === null || v === undefined || Number.isNaN(Number(v))) return '-';
  return Number(v).toFixed(3) + ' MHz';
}

function fmtTime(v) {
  if (!v) return '-';
  return String(v).replace('T', ' ').replace('.000Z', ' UTC').replace('Z', ' UTC');
}

async function refreshRadiosondes() {
  try {
    const data = await getJson('/api/radiosondes');
    const rows = document.getElementById('radiosondeRows');
    if (!rows) return;
    const sondes = Array.isArray(data.radiosondes) ? data.radiosondes : [];
    if (!sondes.length) {
      rows.innerHTML = '<tr><td class="empty" colspan="9">No radiosonde logs found yet</td></tr>';
      return;
    }
    rows.innerHTML = sondes.map(s => `
      <tr data-serial="${escapeHtml(s.serial || '')}">
        <td>${s.serial || '-'}</td>
        <td>${s.type || '-'}</td>
        <td>${fmtFrequency(s.frequency)}</td>
        <td>${s.launchsite || '-'}</td>
        <td>${fmtAltitude(s.first_altitude)}</td>
        <td>${fmtAltitude(s.last_altitude)}</td>
        <td>${fmtDistance(s.distance_km)}</td>
        <td>${s.frames ?? '-'}</td>
        <td>${fmtTime(s.last_time)}</td>
      </tr>
    `).join('');
    rows.querySelectorAll('tr[data-serial]').forEach(tr => {
      tr.addEventListener('click', () => openSondeDetail(tr.dataset.serial));
    });
  } catch (e) {
    const rows = document.getElementById('radiosondeRows');
    if (rows) rows.innerHTML = '<tr><td class="empty" colspan="9">Could not load radiosonde list</td></tr>';
  }
}

async function refreshCpu() {
  try {
    const c = await getJson('/api/cpu');
    const pct = Number(c.cpu_percent);
    setText('cpuLoad', Number.isFinite(pct) ? ('CPU: ' + pct.toFixed(1) + '%') : 'CPU: -');
  } catch (e) {
    setText('cpuLoad', 'CPU: -');
  }
}

async function refreshAll() {
  await refreshStatus();
  await refreshCpu();
  await refreshSpectrum();
  if (activeTab === 'log') setLogText(await getText('/api/log?lines=300'));
  if (activeTab === 'config') setText('configText', await getText('/api/config'));
  if (activeTab === 'whitelist') setText('whitelistText', await getText('/api/whitelist'));
  if (activeTab === 'blacklist') setText('blacklistText', await getText('/api/blacklist'));
  if (activeTab === 'radiosondes') await refreshRadiosondes();
}

async function action(cmd) {
  if (cmd === 'stop' && !confirm('Really stop wsrx?')) return;
  if (cmd === 'clearlogs' && !confirm('Really delete wsrx.log, wsrx-web.log and all radiosonde logs? This cannot be undone.')) return;
  const r = await fetch('/api/' + cmd, { method: 'POST' });
  const t = await r.text();
  setText('statusText', t);
  setTimeout(refreshAll, 700);
}

function showTab(id, btn) {
  activeTab = id;
  document.querySelectorAll('.pane').forEach(p => p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  btn.classList.add('active');
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
  { label: 'Distance', render: d => d.distance_km != null ? fmtDistance(d.distance_km) : null },
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
];

const SONDE_DETAIL_CONSUMED_KEYS = new Set([
  'serial', 'type', 'wsrx_frequency', 'encrypted', 'frame', 'datetime', 'ref_datetime', 'ref_position',
  'first_time', 'modified', 'frames', 'distance_km', 'lat', 'lon', 'first_altitude', 'alt', 'altitude',
  'vel_h', 'heading', 'vel_v', 'sats', 'sat', 'temp', 'humidity', 'pressure', 'batt', 'rssi',
  'burstkilltimer', 'bt', 'killtimer', 'aux', 'rs41_mainboard', 'rs41_mainboard_fw',
]);

function openSondeDetail(serial) {
  const dialog = document.getElementById('sondeDialog');
  if (!dialog || !serial) return;
  const title = document.getElementById('sondeDialogTitle');
  if (title) title.textContent = 'Radiosonde ' + serial;
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