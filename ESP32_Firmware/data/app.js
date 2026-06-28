/* Kopi Control — dashboard offline (tanpa dependensi eksternal) */
'use strict';
const $ = id => document.getElementById(id);
const WS_URL = `ws://${location.hostname || location.host}/ws`;

/* ── Kurva sangrai: renderer canvas ringan ──────────────────────────────── */
const Roast = (() => {
  const cv = $('chart'), ctx = cv.getContext('2d');
  const PAD = { l: 30, r: 10, t: 14, b: 6 }, YMIN = 0, YMAX = 130, MAX = 90;
  let W = 0, H = 0, temps = [], sps = [];
  function resize() {
    const r = cv.getBoundingClientRect(), dpr = window.devicePixelRatio || 1;
    W = r.width; H = r.height;
    cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    draw();
  }
  const X = (i, n) => PAD.l + (W - PAD.l - PAD.r) * (n <= 1 ? 0 : i / (n - 1));
  const Y = v => PAD.t + (H - PAD.t - PAD.b) * (1 - (v - YMIN) / (YMAX - YMIN));
  function push(t, sp) { temps.push(t); sps.push(sp); if (temps.length > MAX) { temps.shift(); sps.shift(); } }
  function line(arr) { const n = arr.length; ctx.beginPath(); for (let i = 0; i < n; i++) { const x = X(i, n), y = Y(arr[i]); i ? ctx.lineTo(x, y) : ctx.moveTo(x, y); } }
  function draw() {
    ctx.clearRect(0, 0, W, H);
    ctx.font = '10px ui-monospace,monospace'; ctx.textBaseline = 'middle';
    for (let v = 0; v <= YMAX; v += 26) {
      const y = Y(v);
      ctx.strokeStyle = 'rgba(180,150,120,.10)'; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(PAD.l, y); ctx.lineTo(W - PAD.r, y); ctx.stroke();
      ctx.fillStyle = '#7d6a53'; ctx.fillText(v, 4, y);
    }
    const n = temps.length; if (!n) return;
    ctx.strokeStyle = 'rgba(255,82,71,.75)'; ctx.setLineDash([5, 4]); ctx.lineWidth = 1.5;
    line(sps); ctx.stroke(); ctx.setLineDash([]);
    line(temps); ctx.lineTo(X(n - 1, n), H - PAD.b); ctx.lineTo(X(0, n), H - PAD.b); ctx.closePath();
    const ag = ctx.createLinearGradient(0, PAD.t, 0, H - PAD.b);
    ag.addColorStop(0, 'rgba(255,122,47,.22)'); ag.addColorStop(1, 'rgba(255,122,47,0)');
    ctx.fillStyle = ag; ctx.fill();
    const lg = ctx.createLinearGradient(0, PAD.t, 0, H - PAD.b);
    lg.addColorStop(0, '#ff7a2f'); lg.addColorStop(1, '#ffb24d');
    line(temps); ctx.strokeStyle = lg; ctx.lineWidth = 2.6; ctx.lineJoin = 'round'; ctx.stroke();
    const lx = X(n - 1, n), ly = Y(temps[n - 1]);
    ctx.fillStyle = '#ff7a2f'; ctx.beginPath(); ctx.arc(lx, ly, 3.6, 0, 7); ctx.fill();
    ctx.strokeStyle = '#161009'; ctx.lineWidth = 2; ctx.stroke();
  }
  window.addEventListener('resize', resize);
  return { push, draw, resize };
})();

/* ── State pill / tombol ────────────────────────────────────────────────── */
const STATE = { IDLE:['SIAGA','s-idle'], RUNNING:['BERJALAN','s-run'], FINISHED:['SELESAI','s-fin'], FAULT:['GANGGUAN','s-fault'], BOOT:['BOOT','s-idle'] };
const FAULTS = { ESTOP:'berhenti darurat', SENSOR:'sensor suhu gagal', OVERTEMP:'suhu lewat batas' };
const RPM = [['siaga','idle'],['normal','ok'],['rendah','warn'],['tinggi','warn'],['RENDAH!','bad'],['TINGGI!','bad']];
const setText = (id, v) => { const e = $(id); if (e) e.textContent = v; };
function setBtn(id, on) { const b = $(id); if (b) b.disabled = !on; }

/* ── Render dari telemetri ──────────────────────────────────────────────── */
function render(d) {
  setText('temp', d.temp.toFixed(1));
  setText('sp', d.setpoint.toFixed(1));
  setText('err', (d.error >= 0 ? '+' : '') + d.error.toFixed(1));
  $('errBad').innerHTML = (d.fault && d.fault !== 'NONE') ? ` · <b>${FAULTS[d.fault] || d.fault}</b>` : '';

  // state pill
  const [stTxt, stCls] = STATE[d.opState] || STATE.BOOT;
  const pill = $('statePill');
  pill.textContent = stTxt + (d.subMode && d.subMode !== 'NONE' ? ' · ' + d.subMode : '');
  pill.className = 'pill ' + stCls;

  // blower dua-arah
  setText('blower', d.blower);
  const mode = $('blowerMode');
  const heating = d.blower >= 20 && d.blower <= 30;   // band memanaskan 20–30%
  mode.textContent = heating ? 'memanaskan' : 'mendinginkan';
  mode.className = 'mode ' + (heating ? 'heat' : 'cool');
  if (d.blower <= 30) { $('mHeat').style.width = (Math.min(10, 30 - d.blower) / 10 * 50) + '%'; $('mCool').style.width = '0%'; }
  else { $('mCool').style.width = ((d.blower - 30) / 55 * 50) + '%'; $('mHeat').style.width = '0%'; }

  // RPM / daya / internals
  setText('rpm', d.rpm.toFixed(1));
  const [rt, rc] = RPM[d.rpmStatus] || RPM[0];
  const rTag = $('rpmTag'); rTag.textContent = rt; rTag.className = 'tag ' + rc;
  setText('power', d.power.toFixed(0));
  setText('volt', d.voltage.toFixed(1)); setText('curr', d.current.toFixed(2)); setText('pf', (d.pf||0).toFixed(2));
  setText('fis', d.fisOut.toFixed(1)); setText('u', d.u_fopid.toFixed(3));
  setText('ival', d.integral.toFixed(2)); setText('dval', d.derivative.toFixed(3));

  // sensor health
  const sTag = $('sensorTag');
  sTag.textContent = d.mlxOk ? 'sensor ok' : 'sensor gagal';
  sTag.className = 'tag ' + (d.mlxOk ? 'ok' : 'bad');

  // servo input mirror (hanya bila tidak sedang difokus)
  if (document.activeElement !== $('inServo')) $('inServo').value = d.servo;

  // tombol per-state
  const idle = d.opState === 'IDLE', run = d.opState === 'RUNNING', halted = d.opState === 'FAULT' || d.opState === 'FINISHED';
  setBtn('bFuzzy', idle); setBtn('bManual', idle); setBtn('bStop', run); setBtn('bReset', halted);
  $('manualBox').classList.toggle('on', d.subMode === 'MANUAL');
  if (d.subMode === 'MANUAL' && document.activeElement !== $('inBlower')) { $('inBlower').value = d.blower; setText('manVal', d.blower); }

  // rekam + hitung mundur
  const rec = $('rec');
  if (d.logging) { rec.classList.add('on'); setText('recTime', fmt(d.remaining)); } else rec.classList.remove('on');

  Roast.push(d.temp, d.setpoint); Roast.draw();
}
const fmt = s => { s = Math.max(0, s | 0); return String((s / 60) | 0).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0'); };

/* ── WebSocket ──────────────────────────────────────────────────────────── */
let ws = null, retry = null;
function connect() {
  if (ws && ws.readyState === WebSocket.OPEN) return;
  ws = new WebSocket(WS_URL);
  ws.onopen = () => { $('conn').classList.add('on'); setText('connText', 'tersambung'); if (retry) { clearInterval(retry); retry = null; } };
  ws.onclose = () => { $('conn').classList.remove('on'); setText('connText', 'terputus'); if (!retry) retry = setInterval(connect, 3000); };
  ws.onerror = () => ws.close();
  ws.onmessage = e => { try { render(JSON.parse(e.data)); } catch (_) {} };
}
const send = o => { if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(o)); };

/* ── Aksi ───────────────────────────────────────────────────────────────── */
function setSp()    { const v = parseFloat($('inSp').value);   if (!isNaN(v)) send({ setpoint: v }); }
function setServo() { const v = parseInt($('inServo').value);  if (!isNaN(v)) send({ servo: v }); }
function setDur()   { const v = parseInt($('inDur').value);    if (!isNaN(v)) send({ duration: v }); }
function setBlower(v){ setText('manVal', v); send({ blower: parseInt(v) }); }
function startFuzzy()  { send({ start: 'FUZZY' }); }
function startManual() { send({ start: 'MANUAL' }); }
function stopSys()  { send({ stop: true }); }
function resetSys() { send({ reset: true }); }
function estop()    { if (confirm('Hentikan semua sekarang? Gas tutup, blower mati.')) send({ estop: true }); }

function loadLogs() {
  fetch('/api/logs').then(r => r.json()).then(f => {
    const el = $('logs');
    el.innerHTML = f.length
      ? f.map(n => `<a href="/api/download?file=${encodeURIComponent(n)}" target="_blank">${n}</a>`).join('')
      : '<span class="empty">Belum ada rekaman.</span>';
  }).catch(() => { $('logs').innerHTML = '<span class="empty">Gagal memuat daftar.</span>'; });
}

Roast.resize();
connect();
loadLogs();
setInterval(loadLogs, 15000);
