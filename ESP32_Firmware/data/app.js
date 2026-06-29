/* Kopi Control — dashboard 2 tab (Wafi suhu · Fadel kecepatan), offline */
'use strict';
const $ = id => document.getElementById(id);
const WS_URL = `ws://${location.hostname || location.host}/ws`;

/* ── Chart factory (ringan, offline) ──────────────────────────────────────── */
function makeChart(id, color, yMin, yMax) {
  const cv = $(id), ctx = cv.getContext('2d'), MAX = 80, PAD = { l: 32, r: 10, t: 12, b: 6 };
  let W = 0, H = 0, val = [], ref = [];
  function resize() {
    const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    W = r.width; H = r.height; cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); draw();
  }
  const X = (i, n) => PAD.l + (W - PAD.l - PAD.r) * (n <= 1 ? 0 : i / (n - 1));
  const Y = v => PAD.t + (H - PAD.t - PAD.b) * (1 - (v - yMin) / (yMax - yMin));
  function push(a, b) { val.push(a); ref.push(b); if (val.length > MAX) { val.shift(); ref.shift(); } }
  function line(arr) { const n = arr.length; ctx.beginPath(); for (let i = 0; i < n; i++) { const x = X(i, n), y = Y(arr[i]); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); } }
  function draw() {
    ctx.clearRect(0, 0, W, H); ctx.font = '10px ui-monospace,monospace'; ctx.textBaseline = 'middle';
    for (let g = 0; g <= 4; g++) { const v = yMin + (yMax - yMin) * g / 4, y = Y(v);
      ctx.strokeStyle = 'rgba(180,150,120,.10)'; ctx.beginPath(); ctx.moveTo(PAD.l, y); ctx.lineTo(W - PAD.r, y); ctx.stroke();
      ctx.fillStyle = '#7d6a53'; ctx.fillText(v.toFixed(0), 4, y); }
    if (!val.length) return;
    ctx.strokeStyle = 'rgba(255,82,71,.7)'; ctx.setLineDash([5, 4]); ctx.lineWidth = 1.5; line(ref); ctx.stroke(); ctx.setLineDash([]);
    ctx.strokeStyle = color; ctx.lineWidth = 2.4; ctx.lineJoin = 'round'; line(val); ctx.stroke();
    const n = val.length, lx = X(n - 1, n), ly = Y(val[n - 1]);
    ctx.fillStyle = color; ctx.beginPath(); ctx.arc(lx, ly, 3.4, 0, 7); ctx.fill();
  }
  window.addEventListener('resize', resize);
  return { push, draw, resize };
}
const tempChart = makeChart('chart', '#ff7a2f', 0, 130);
const rpmChart  = makeChart('rpmChart', '#3ec6e0', 0, 60);

/* ── Tab ──────────────────────────────────────────────────────────────────── */
let curTab = 'Wafi';
let lastManualTab = 0;   // anti-balik: abaikan auto-follow sesaat setelah klik manual
const TAB_TITLE = {
  Wafi:  'Sistem Kontrol Suhu Penggiling Biji Kopi',
  Fadel: 'Sistem Kontrol Kecepatan Penggiling Biji Kopi'
};
function showTab(name, fromUser = true) {
  // Safety: tab tujuan dikunci saat proses berjalan (lihat render()) → abaikan.
  const btn = name === 'Fadel' ? $('tabBtnFadel') : $('tabBtnWafi');
  if (btn.disabled) return;
  if (fromUser) lastManualTab = Date.now();
  curTab = name;
  $('tabWafi').style.display  = name === 'Wafi'  ? '' : 'none';
  $('tabFadel').style.display = name === 'Fadel' ? '' : 'none';
  $('tabBtnWafi').classList.toggle('on', name === 'Wafi');
  $('tabBtnFadel').classList.toggle('on', name === 'Fadel');
  $('brandTitle').textContent = TAB_TITLE[name];
  document.title = TAB_TITLE[name];
  send({ profile: name === 'Fadel' ? 'FADEL' : 'WAFI' });
  setTimeout(() => (name === 'Fadel' ? rpmChart : tempChart).resize(), 30);
}

/* ── State maps ───────────────────────────────────────────────────────────── */
const STATE = { IDLE:['SIAGA','s-idle'], RUNNING:['BERJALAN','s-run'], FINISHED:['SELESAI','s-fin'], FAULT:['GANGGUAN','s-fault'], BOOT:['BOOT','s-idle'] };
const FAULTS = { ESTOP:'berhenti darurat', SENSOR:'sensor suhu gagal', OVERTEMP:'suhu lewat batas' };
const RPM = [['siaga','idle'],['normal','ok'],['rendah','warn'],['tinggi','warn'],['RENDAH!','bad'],['TINGGI!','bad']];
const setT = (id, v) => { const e = $(id); if (e) e.textContent = v; };
const setBtn = (id, on) => { const b = $(id); if (b) b.disabled = !on; };
const fmt = s => { s = Math.max(0, s | 0); return String((s / 60) | 0).padStart(2,'0') + ':' + String(s % 60).padStart(2,'0'); };

function render(d) {
  const [stTxt, stCls] = STATE[d.opState] || STATE.BOOT;
  const pill = $('statePill'); pill.textContent = stTxt + (d.subMode && d.subMode !== 'NONE' && d.profile === 'WAFI' ? ' · ' + d.subMode : ''); pill.className = 'pill ' + stCls;
  const fb = $('faultBadge');
  if (d.fault && d.fault !== 'NONE') { fb.textContent = 'FAULT: ' + d.fault; fb.style.display = ''; } else fb.style.display = 'none';

  // ── Wafi ──
  setT('temp', d.temp.toFixed(1)); setT('sp', d.setpoint.toFixed(1));
  setT('err', (d.error >= 0 ? '+' : '') + d.error.toFixed(1));
  $('errBad').innerHTML = (d.fault && d.fault !== 'NONE') ? ` · <b>${FAULTS[d.fault] || d.fault}</b>` : '';
  setT('blower', d.blower);
  const mode = $('blowerMode');
  const heating = d.blower >= 20 && d.blower <= 30;
  mode.textContent = d.blower === 0 ? 'mati' : (heating ? 'memanaskan' : 'mendinginkan');
  mode.className = 'mode ' + (d.blower === 0 ? 'neutral' : (heating ? 'heat' : 'cool'));
  if (d.blower <= 30) { $('mHeat').style.width = (Math.min(10, 30 - d.blower) / 10 * 50) + '%'; $('mCool').style.width = '0%'; }
  else { $('mCool').style.width = ((d.blower - 30) / 70 * 50) + '%'; $('mHeat').style.width = '0%'; }
  setT('fis', d.fisOut.toFixed(1)); setT('u', d.u_fopid.toFixed(3)); setT('ival', d.integral.toFixed(2)); setT('dval', d.derivative.toFixed(3));
  const sTag = $('sensorTag'); sTag.textContent = d.mlxOk ? 'sensor ok' : 'sensor gagal'; sTag.className = 'tag ' + (d.mlxOk ? 'ok' : 'bad');

  // ── RPM / daya (kedua tab) ──
  const [rt, rc] = RPM[d.rpmStatus] || RPM[0];
  setT('rpm', d.rpm.toFixed(1)); const rTag = $('rpmTag'); if (rTag) { rTag.textContent = rt; rTag.className = 'tag ' + rc; }
  setT('power', d.power.toFixed(0)); setT('volt', d.voltage.toFixed(1)); setT('curr', d.current.toFixed(2)); setT('pf', (d.pf || 0).toFixed(2));

  // ── Fadel ──
  setT('fRpm', d.rpm.toFixed(1)); setT('fRpm2', d.rpm.toFixed(1));
  setT('fTarget', (d.speedSP || 0).toFixed(1));
  setT('fVfd', (d.vfdFreq || 0).toFixed(1)); setT('fVfd2', (d.vfdFreq || 0).toFixed(1));
  setT('powerF', d.power.toFixed(0)); setT('voltF', d.voltage.toFixed(1)); setT('currF', d.current.toFixed(2));
  const rTagF = $('rpmTagF'); if (rTagF) { rTagF.textContent = rt; rTagF.className = 'tag ' + rc; }

  // ── Tombol per state+profil ──
  const idle = d.opState === 'IDLE', run = d.opState === 'RUNNING', halted = d.opState === 'FAULT' || d.opState === 'FINISHED';
  setBtn('bFuzzy', idle && d.profile === 'WAFI'); setBtn('bManual', idle && d.profile === 'WAFI');
  setBtn('bStop', run); setBtn('bReset', halted);
  setBtn('bStartF', idle && d.profile === 'FADEL'); setBtn('bManualF', idle && d.profile === 'FADEL');
  setBtn('bStopF', run); setBtn('bResetF', halted);

  // ── Safety: kunci pindah tab saat proses berjalan (tak boleh ganti profil di tengah jalan) ──
  const lockFadel = run && d.profile === 'WAFI', lockWafi = run && d.profile === 'FADEL';
  $('tabBtnFadel').disabled = lockFadel; $('tabBtnWafi').disabled = lockWafi;
  $('tabBtnFadel').title = lockFadel ? 'Terkunci — proses Wafi sedang berjalan' : '';
  $('tabBtnWafi').title  = lockWafi  ? 'Terkunci — proses Fadel sedang berjalan' : '';

  // ── Sinkron profil: ikuti g_state (mis. diganti dari keypad LCD) → web pindah tab ──
  //   Jeda 1.5 dtk setelah klik manual agar broadcast lama tak membalik tab.
  if (d.profile) {
    const want = d.profile === 'FADEL' ? 'Fadel' : 'Wafi';
    if (want !== curTab && Date.now() - lastManualTab > 1500) showTab(want, false);
  }

  // ── Manual Wafi (blower) ──
  $('manualBox').classList.toggle('on', d.subMode === 'MANUAL' && d.profile === 'WAFI');
  if (d.subMode === 'MANUAL' && d.profile === 'WAFI' && document.activeElement !== $('inBlower')) { $('inBlower').value = d.blower; setT('manVal', d.blower); }
  // ── Manual Fadel (freq VFD) ──
  $('manualBoxF').classList.toggle('on', d.subMode === 'MANUAL' && d.profile === 'FADEL');
  if (d.subMode === 'MANUAL' && d.profile === 'FADEL' && document.activeElement !== $('inFreqMan')) { $('inFreqMan').value = d.vfdFreq || 0; setT('manValF', (d.vfdFreq || 0).toFixed(1)); }

  // ── Rekam (hitung mundur) ──
  const remTxt = fmt(d.remaining), recOn = d.logging;
  ['rec','fRec'].forEach((rid, i) => { const r = $(rid); if (r) r.classList.toggle('on', recOn); });
  setT('recTime', remTxt); setT('fRecTime', remTxt);

  tempChart.push(d.temp, d.setpoint); rpmChart.push(d.rpm, d.speedSP || 0);
  (curTab === 'Fadel' ? rpmChart : tempChart).draw();
}

/* ── WebSocket ────────────────────────────────────────────────────────────── */
let ws = null, retry = null;
function connect() {
  if (ws && ws.readyState === WebSocket.OPEN) return;
  ws = new WebSocket(WS_URL);
  ws.onopen = () => { $('conn').classList.add('on'); setT('connText', 'tersambung'); if (retry) { clearInterval(retry); retry = null; } };
  ws.onclose = () => { $('conn').classList.remove('on'); setT('connText', 'terputus'); if (!retry) retry = setInterval(connect, 3000); };
  ws.onerror = () => ws.close();
  ws.onmessage = e => { try { render(JSON.parse(e.data)); } catch (_) {} };
}
const send = o => { if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(o)); };

/* ── Aksi ─────────────────────────────────────────────────────────────────── */
const numv = id => parseFloat($(id).value);
function setSp()     { const v = numv('inSp');     if (!isNaN(v)) send({ setpoint: v }); }
function setServo()  { const v = parseInt($('inServo').value); if (!isNaN(v)) send({ servo: v }); }
function setFreq()   { const v = numv('inFreq');   if (!isNaN(v)) send({ freq: v }); }
function setDur()    { const v = parseInt($('inDur').value);  if (!isNaN(v)) send({ duration: v }); }
function setBlower(v){ setT('manVal', v); send({ blower: parseInt(v) }); }
function setSpeed()  { const v = numv('inSpeed');  if (!isNaN(v)) send({ speedsp: v }); }
function setServoF() { const v = parseInt($('inServoF').value); if (!isNaN(v)) send({ servo: v }); }
function setBlowerC(){ const v = parseInt($('inBlowerC').value); if (!isNaN(v)) send({ blower: v }); }
function setDurF()   { const v = parseInt($('inDurF').value); if (!isNaN(v)) send({ duration: v }); }
function setVfdMan(v){ setT('manValF', parseFloat(v).toFixed(1)); send({ vfd: parseFloat(v) }); }
function setP(key, id){ const v = numv(id); if (!isNaN(v)) send({ [key]: v }); }
function startFuzzy()  { send({ start: 'FUZZY' }); }
function startManual() { send({ start: 'MANUAL' }); }
function startSpeed()   { send({ start: 'FUZZY' }); }   // profil sudah FADEL via tab → PID kecepatan
function startManualF() { send({ start: 'MANUAL' }); }  // Fadel manual → freq VFD langsung
function stopSys()  { send({ stop: true }); }
function resetSys() { send({ reset: true }); }
function estop()    { if (confirm('Hentikan semua sekarang?')) send({ estop: true }); }

/* ── Params (load sekali) ─────────────────────────────────────────────────── */
function loadParams() {
  fetch('/api/params').then(r => r.json()).then(p => {
    const set = (id, v) => { if (v != null && $(id)) $(id).value = v; };
    set('inSp', p.setpoint); set('inServo', p.servo); set('inFreq', p.freq); set('inDur', p.duration);
    set('inKp', p.Kp); set('inKi', p.Ki); set('inKd', p.Kd); set('inLam', p.lambda); set('inMu', p.mu); set('inBeta', p.beta);
    set('inSpeed', p.speedSP); set('inServoF', p.servo); set('inBlowerC', p.blowerConst); set('inDurF', p.duration);
    set('inSKp', p.sKp); set('inSKi', p.sKi); set('inSKd', p.sKd);
    if (p.profile) showTab(p.profile === 'FADEL' ? 'Fadel' : 'Wafi');
  }).catch(() => {});
}

/* ── Riwayat: grafik dari CSV + metrik + download ─────────────────────────── */
const basename = p => p.split('/').pop();
function renderLogList(id, arr) {
  const e = $(id); if (!e) return;
  e.innerHTML = arr.length
    ? arr.map(n => `<button class="logitem" onclick="openHist('${n}')">📈 ${basename(n)}</button>`).join('')
    : '<span class="empty">Belum ada rekaman.</span>';
}
function loadLogs() {
  fetch('/api/logs').then(r => r.json()).then(f => {
    renderLogList('logsWafi',  f.filter(n => n.includes('/wafi/')));
    renderLogList('logsFadel', f.filter(n => n.includes('/fadel/')));
  }).catch(() => {
    ['logsWafi', 'logsFadel'].forEach(id => { const e = $(id); if (e) e.innerHTML = '<span class="empty">Gagal memuat daftar.</span>'; });
  });
}
let histRows = [], histStart = '';
function parseCsv(text) {
  const lines = text.split(/\r?\n/).filter(l => l && !l.startsWith('#'));
  if (!lines.length) return [];
  const head = lines[0].split(',');
  const iT = head.indexOf('Suhu_C'), iSP = head.indexOf('SetPoint_C'), iDt = head.indexOf('DateTime'), iR = head.indexOf('RPM');
  const rows = []; let t0 = null; histStart = '';
  for (let k = 1; k < lines.length; k++) {
    const c = lines[k].split(','); if (c.length < 3) continue;
    const temp = parseFloat(useRpm ? c[iR] : c[iT]), sp = parseFloat(c[iSP]);
    if (isNaN(temp)) continue;
    let sec; const dt = Date.parse((c[iDt] || '').replace(' ', 'T'));
    if (!isNaN(dt)) { if (t0 === null) { t0 = dt; histStart = (c[iDt] || '').split(' ')[1] || ''; } sec = (dt - t0) / 1000; }
    else sec = rows.length * 5;
    rows.push({ sec, temp, sp: isNaN(sp) ? 0 : sp });
  }
  return rows;
}
let useRpm = false;
function histMetrics(rows) {
  if (!rows.length) return { rise: null, over: 0, osil: 0 };
  const sp = rows[rows.length - 1].sp || Math.max(...rows.map(r => r.temp)), band = 0.01 * sp;
  let rise = null; for (const r of rows) if (sp && Math.abs(r.temp - sp) <= band) { rise = r.sec; break; }
  const peak = Math.max(...rows.map(r => r.temp)), over = sp ? (peak - sp) / sp * 100 : 0;
  let osil = 0; if (rise != null && sp) { const seg = rows.filter(r => r.sec >= rise).map(r => r.temp); if (seg.length) osil = (Math.max(...seg) - Math.min(...seg)) / sp * 100; }
  return { rise, over, osil };
}
function renderHist(rows) {
  const cv = $('histChart'), ctx = cv.getContext('2d');
  const dpr = window.devicePixelRatio || 1, bb = cv.getBoundingClientRect();
  const W = bb.width, H = bb.height; cv.width = W * dpr; cv.height = H * dpr; ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  const PAD = { l: 38, r: 12, t: 12, b: 28 };
  ctx.fillStyle = '#1d150c'; ctx.fillRect(0, 0, W, H);
  if (!rows.length) return;
  const sp = rows[rows.length - 1].sp, tMax = rows[rows.length - 1].sec || 1, temps = rows.map(r => r.temp);
  const yMin = Math.max(0, Math.min(...temps) - 3), yMax = Math.max((sp || 0) + 10, Math.max(...temps) + 3);
  const X = s => PAD.l + (W - PAD.l - PAD.r) * (s / tMax), Y = v => PAD.t + (H - PAD.t - PAD.b) * (1 - (v - yMin) / (yMax - yMin));
  ctx.font = '10px ui-monospace,monospace'; ctx.textBaseline = 'middle';
  for (let g = 0; g <= 4; g++) { const v = yMin + (yMax - yMin) * g / 4, y = Y(v);
    ctx.strokeStyle = 'rgba(180,150,120,.10)'; ctx.beginPath(); ctx.moveTo(PAD.l, y); ctx.lineTo(W - PAD.r, y); ctx.stroke();
    ctx.fillStyle = '#7d6a53'; ctx.fillText(v.toFixed(0), 4, y); }
  ctx.textBaseline = 'top';
  for (let g = 0; g <= 5; g++) { const s = tMax * g / 5; ctx.fillStyle = '#7d6a53'; ctx.fillText((s / 60).toFixed(0) + 'm', X(s), H - PAD.b + 6); }
  if (histStart) { ctx.fillStyle = '#9b8b7a'; ctx.fillText('t0 ' + histStart, PAD.l, H - 12); }
  if (sp) { ctx.strokeStyle = 'rgba(255,82,71,.7)'; ctx.setLineDash([5, 4]); ctx.lineWidth = 1.5; ctx.beginPath();
    rows.forEach((r, i) => { const x = X(r.sec), y = Y(r.sp); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }); ctx.stroke(); ctx.setLineDash([]); }
  const grad = ctx.createLinearGradient(0, PAD.t, 0, H - PAD.b); grad.addColorStop(0, '#ff7a2f'); grad.addColorStop(1, '#ffb24d');
  ctx.strokeStyle = grad; ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.beginPath();
  rows.forEach((r, i) => { const x = X(r.sec), y = Y(r.temp); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }); ctx.stroke();
  const m = histMetrics(rows);
  if (m.rise != null && sp) { const x = X(m.rise), y = Y(sp); ctx.fillStyle = '#22d3ee'; ctx.beginPath(); ctx.arc(x, y, 5, 0, 7); ctx.fill();
    ctx.strokeStyle = '#161009'; ctx.lineWidth = 2; ctx.stroke(); ctx.fillStyle = '#22d3ee'; ctx.textBaseline = 'bottom'; ctx.fillText('SP ' + (m.rise / 60).toFixed(1) + 'm', x + 6, y - 4); }
}
function openHist(path) {
  useRpm = path.includes('/fadel/');
  $('histModal').classList.add('on'); $('dlCsv').href = '/api/download?file=' + encodeURIComponent(path);
  fetch('/api/download?file=' + encodeURIComponent(path)).then(r => r.text()).then(txt => {
    histRows = parseCsv(txt);
    $('histTitle').textContent = 'Grafik — ' + basename(path) + (histStart ? ' (mulai ' + histStart + ')' : '');
    const m = histMetrics(histRows);
    setT('mRise', m.rise != null ? (m.rise / 60).toFixed(2) + ' mnt' : '—');
    setT('mOver', m.over.toFixed(1) + ' %'); setT('mOsil', m.osil.toFixed(1) + ' %');
    requestAnimationFrame(() => renderHist(histRows));
  }).catch(() => { setT('histTitle', 'Gagal memuat CSV'); });
}
function closeHist() { $('histModal').classList.remove('on'); }
function downloadJpg() {
  const a = document.createElement('a'); a.download = $('histTitle').textContent.replace(/[^\w.-]/g, '_') + '.jpg';
  a.href = $('histChart').toDataURL('image/jpeg', 0.92); a.click();
}
window.addEventListener('resize', () => { if ($('histModal').classList.contains('on')) renderHist(histRows); });

/* ── Tooltip "?" (floating, anti-terpotong di tepi layar; mouse+keyboard+sentuh) ── */
const tip = document.createElement('div'); tip.className = 'tip'; document.body.appendChild(tip);
let tipFor = null;
function showTip(el) {
  const t = el.getAttribute('data-tip'); if (!t) return;
  tip.textContent = t; tip.classList.add('on');
  const r = el.getBoundingClientRect(), tw = tip.offsetWidth, th = tip.offsetHeight;
  let x = r.left + r.width / 2 - tw / 2; x = Math.max(8, Math.min(x, innerWidth - tw - 8));
  let y = r.top - th - 8; if (y < 8) y = r.bottom + 8;
  tip.style.left = x + 'px'; tip.style.top = y + 'px';
}
function hideTip() { tip.classList.remove('on'); tipFor = null; }
document.addEventListener('mouseover', e => { const h = e.target.closest('.help'); if (h) showTip(h); });
document.addEventListener('mouseout',  e => { if (e.target.closest('.help')) hideTip(); });
document.addEventListener('focusin',   e => { const h = e.target.closest('.help'); if (h) showTip(h); });
document.addEventListener('focusout',  e => { if (e.target.closest('.help')) hideTip(); });
document.addEventListener('click', e => {                  // sentuh: tap = toggle
  const h = e.target.closest('.help');
  if (h) { if (tipFor === h) hideTip(); else { showTip(h); tipFor = h; } }
  else if (!e.target.closest('.tip')) hideTip();
});

tempChart.resize();
connect();
loadParams();
loadLogs();
setInterval(loadLogs, 15000);
