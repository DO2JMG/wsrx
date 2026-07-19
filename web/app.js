let activeTab = 'log';
let lastSpectrum = null;
let lastPeaks = null;
let peakHitboxes = [];
let lastPlotMeta = null;
let activeChannelFreqs = [];

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
  for (const freq of freqs) {
    const f = Number(freq);
    if (!Number.isFinite(f)) continue;
    if (f < minF || f > maxF) continue;
    const xx = padL + (f - minF) / (maxF - minF || 1) * plotW;
    const half = 6 * dpr;
    const topY = yBase + 2 * dpr;
    const tipY = yBase + 12 * dpr;
    ctx.fillStyle = '#d25a3a';
    ctx.strokeStyle = '#7f321e';
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

async function refreshStatus() {
  try {
    const s = await getJson('/api/status');
    setHtml('running', s.running ? '<span class="ok">running</span>' : '<span class="bad">stopped</span>');
    setText('pid', s.pid || '-');
    setText('path', s.base_dir || '');
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
  ctx.fillStyle = '#0c1828';
  ctx.fillRect(0, 0, w, h);

  const points = spec && Array.isArray(spec.points) ? spec.points : [];
  if (!points.length) {
    ctx.fillStyle = '#8b949e';
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

  const x = f => padL + (f - minF) / (maxF - minF || 1) * plotW;
  const y = p => padT + (maxP - p) / (maxP - minP || 1) * plotH;

  ctx.strokeStyle = '#507ba8';
  ctx.lineWidth = 1 * dpr;
  ctx.beginPath();
  for (let i = 0; i <= 5; i++) {
    const yy = padT + i * plotH / 5;
    ctx.moveTo(padL, yy);
    ctx.lineTo(w - padR, yy);
  }
  ctx.stroke();

  ctx.fillStyle = '#507ba8';
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
    ctx.strokeStyle = '#bf702b';
    ctx.setLineDash([6 * dpr, 5 * dpr]);
    ctx.beginPath();
    ctx.moveTo(padL, y(trig));
    ctx.lineTo(w - padR, y(trig));
    ctx.stroke();
    ctx.setLineDash([]);
  }

  ctx.strokeStyle = '#507ba8';
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
    ctx.fillStyle = '#83c1ee';
    ctx.beginPath();
    ctx.arc(xx, yy, 5 * dpr, 0, Math.PI * 2);
    ctx.fill();
    ctx.strokeStyle = '#296481';
    ctx.beginPath();
    ctx.moveTo(xx, yy);
    ctx.lineTo(xx, padT);
    ctx.stroke();
    peakHitboxes.push({ xCss: xx / dpr, yCss: yy / dpr, freqMhz: pk[0], powerDb: pk[1] });
  }

  drawActiveChannelMarkers(ctx, activeChannelFreqs, minF, maxF, padL, padT, plotW, plotH, dpr);

  lastPlotMeta = { points, minF, maxF, minP, maxP, padL, padT, plotW, plotH, dpr };

  ctx.fillStyle = '#c9d1d9';
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

async function refreshSpectrum() {
  try {
    const [spec, peaks] = await Promise.all([
      getJson('/api/spectrum'),
      getJson('/api/peaks')
    ]);
    lastSpectrum = spec;
    lastPeaks = peaks;
    drawSpectrum(lastSpectrum, lastPeaks);
  } catch (e) {
    drawSpectrum({ error: 'Spectrum could not be loaded', points: [] }, { peaks: [] });
  }
}


function fmtAltitude(v) {
  if (v === null || v === undefined || Number.isNaN(Number(v))) return '-';
  return Number(v).toFixed(1) + ' m';
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
      rows.innerHTML = '<tr><td class="empty" colspan="6">No radiosonde logs found yet</td></tr>';
      return;
    }
    rows.innerHTML = sondes.map(s => `
      <tr>
        <td>${s.serial || '-'}</td>
        <td>${s.type || '-'}</td>
        <td>${fmtAltitude(s.first_altitude)}</td>
        <td>${fmtAltitude(s.last_altitude)}</td>
        <td>${s.frames ?? '-'}</td>
        <td>${fmtTime(s.last_time)}</td>
      </tr>
    `).join('');
  } catch (e) {
    const rows = document.getElementById('radiosondeRows');
    if (rows) rows.innerHTML = '<tr><td class="empty" colspan="6">Could not load radiosonde list</td></tr>';
  }
}

async function refreshAll() {
  await refreshStatus();
  await refreshSpectrum();
  if (activeTab === 'log') setLogText(await getText('/api/log?lines=300'));
  if (activeTab === 'config') setText('configText', await getText('/api/config'));
  if (activeTab === 'whitelist') setText('whitelistText', await getText('/api/whitelist'));
  if (activeTab === 'blacklist') setText('blacklistText', await getText('/api/blacklist'));
  if (activeTab === 'offsets') setText('offsetText', await getText('/api/offsets'));
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

document.querySelectorAll('[data-action]').forEach(btn => {
  btn.addEventListener('click', () => action(btn.dataset.action));
});

document.getElementById('refreshBtn').addEventListener('click', refreshAll);
window.addEventListener('resize', () => { if (lastSpectrum) drawSpectrum(lastSpectrum, lastPeaks); });

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

initSpectrumTooltip();
setInterval(refreshAll, 500);
refreshAll();

