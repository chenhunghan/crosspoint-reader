// CrossPoint simulator, device page: a flat Metalio E-Ink 4 with the real
// firmware (WebAssembly) drawing into its screen at exact pixels.
// Content sources: demo (in-page scripted bridge), replay (a real recorded
// Claude Code session played by an in-page bridge) and live (agentmux daemon).
import {SimHost, KEY, KEYBOARD, UI_W, UI_H, parseBridge} from './core/sim-host.js';
import {DemoBridgeSocket} from './core/demo-bridge.js';
import {startDevLoop} from './core/devloop.js';

const $ = (id) => document.getElementById(id);
const canvas = $('screen');
const PREFS_KEY = 'crosspoint-sim2d';
const RESTORE_KEY = 'crosspoint-sim2d-restore';
const ZOOMS = ['fit', '1', '2', '4'];
const DEMO_CONFIG = {host: 'demo', port: 7878, token: 'demo1234'};
const fmt = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ───────────────────────────────────────────── prefs (per-viewer, survive reloads)
const prefs = {zoom: 'fit', source: 'demo', seed: true, panel: true, liveBridge: '', liveToken: '', speed: 1, log: false};
try { Object.assign(prefs, JSON.parse(localStorage.getItem(PREFS_KEY) || '{}')); } catch (e) { /* storage blocked */ }
function savePrefs() { try { localStorage.setItem(PREFS_KEY, JSON.stringify(prefs)); } catch (e) { /* storage blocked */ } }
{
  // URL parameters (also the old dev.html / stage ones) override and are then saved.
  const q = new URLSearchParams(location.search);
  const src = q.get('source') || (q.get('mode') === 'live' ? 'live' : '');
  const bridge = q.get('bridge');
  if (bridge && bridge.toLowerCase() !== 'demo') { prefs.liveBridge = bridge.replace(/\/device$/, ''); prefs.source = 'live'; }
  if (bridge && bridge.toLowerCase() === 'demo') prefs.source = 'demo';
  if (['demo', 'replay', 'live'].includes(src)) prefs.source = src;
  if (q.get('token')) prefs.liveToken = q.get('token');
  if (ZOOMS.includes(q.get('zoom'))) prefs.zoom = q.get('zoom');
  if (q.get('seed')) prefs.seed = q.get('seed') !== '0';
  savePrefs();
  if (['source', 'mode', 'bridge', 'token', 'zoom', 'seed'].some((k) => q.has(k))) {
    for (const k of ['source', 'mode', 'bridge', 'token', 'zoom', 'seed']) q.delete(k);
    history.replaceState(null, '', location.pathname + (q.toString() ? '?' + q : '') + location.hash);
  }
}
const SOURCE = prefs.source;

// ───────────────────────────────────────────── log
const logEl = $('log');
function log(text, cls = 'sys') {
  if (cls === 'fw' && !$('showFw').checked) return;
  if ((cls === 'rx' || cls === 'tx') && !$('showFrames').checked) return;
  if (!$('logBox').open) { pendingLog.push([text, cls]); if (pendingLog.length > 800) pendingLog.shift(); return; }
  appendLog(text, cls);
}
const pendingLog = [];
function appendLog(text, cls) {
  const line = document.createElement('div');
  line.className = cls;
  line.textContent = text;
  const stick = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 8;
  logEl.appendChild(line);
  while (logEl.childNodes.length > 800) logEl.removeChild(logEl.firstChild);
  if (stick) logEl.scrollTop = logEl.scrollHeight;
}
$('logBox').open = prefs.log;
$('logBox').addEventListener('toggle', () => {
  prefs.log = $('logBox').open;
  savePrefs();
  if (prefs.log) { for (const [t, c] of pendingLog.splice(0)) appendLog(t, c); logEl.scrollTop = logEl.scrollHeight; }
});
$('clearLog').onclick = () => { logEl.textContent = ''; pendingLog.length = 0; };

// ───────────────────────────────────────────── zoom: integer device pixels per framebuffer pixel
const device = $('device');
const scroller = $('scroller');
let scale = 1;
function fitScale(dpr) {
  const hint = device.nextElementSibling;
  const availW = scroller.clientWidth - 48;
  const availH = scroller.clientHeight - 32 - (hint ? hint.offsetHeight + 14 : 0);
  const u1 = UI_W / dpr / 5.3; // CSS px per device cm at scale 1
  return Math.max(1, Math.floor(Math.min(availW / (7.36 * u1), availH / (12.5 * u1))));
}
function layout() {
  const dpr = window.devicePixelRatio || 1;
  scale = prefs.zoom === 'fit' ? fitScale(dpr) : Number(prefs.zoom);
  const w = (UI_W * scale) / dpr, h = (UI_H * scale) / dpr;
  device.style.setProperty('--u', `${w / 5.3}px`);
  canvas.style.width = `${w}px`;
  canvas.style.height = `${h}px`;
  // Screen offset inside the body, in whole device pixels.
  const u = w / 5.3;
  canvas.style.left = `${Math.round(0.95 * u * dpr) / dpr}px`;
  canvas.style.top = `${Math.round(0.88 * u * dpr) / dpr}px`;
  Object.assign($('loading').style, {left: canvas.style.left, top: canvas.style.top, width: `${w}px`, height: `${h}px`});
  for (const b of document.querySelectorAll('#zoom button')) b.setAttribute('aria-pressed', String(b.dataset.zoom === prefs.zoom));
  $('readout').textContent = `1 px = ${scale}×${scale} device px · dpr ${+dpr.toFixed(3)} · ${+w.toFixed(2)}×${+h.toFixed(2)} CSS px`;
  snap();
}
// Moves the device onto whole device pixels (a layout offset, not a transform,
// so the canvas is never resampled) and nearest-neighbour scaling stays exact.
function snap() {
  const dpr = window.devicePixelRatio || 1;
  device.style.left = device.style.top = '';
  const r = device.getBoundingClientRect();
  const dx = Math.round(r.left * dpr) / dpr - r.left, dy = Math.round(r.top * dpr) / dpr - r.top;
  if (Math.abs(dx) > 1e-3) device.style.left = `${dx}px`;
  if (Math.abs(dy) > 1e-3) device.style.top = `${dy}px`;
}
function watchDpr() {
  matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`).addEventListener('change', () => { layout(); watchDpr(); }, {once: true});
}
function setZoom(z) { prefs.zoom = z; savePrefs(); layout(); }
for (const b of document.querySelectorAll('#zoom button')) b.onclick = () => setZoom(b.dataset.zoom);
new ResizeObserver(() => layout()).observe(scroller);
scroller.addEventListener('scroll', snap, {passive: true});
watchDpr();

const app = $('app');
function setPanel(open) {
  prefs.panel = open;
  savePrefs();
  app.classList.toggle('collapsed', !open);
  $('panelToggle').setAttribute('aria-expanded', String(open));
}
$('panelToggle').onclick = () => setPanel(!prefs.panel);
setPanel(prefs.panel);
layout();

// ───────────────────────────────────────────── source UI
for (const b of document.querySelectorAll('#source button')) {
  b.setAttribute('aria-pressed', String(b.dataset.source === SOURCE));
  b.onclick = () => {
    if (b.dataset.source === SOURCE) return;
    prefs.source = b.dataset.source;
    savePrefs();
    location.reload();
  };
}
$('srcDemo').hidden = SOURCE !== 'demo';
$('srcReplay').hidden = SOURCE !== 'replay';
$('srcLive').hidden = SOURCE !== 'live';
$('seed').checked = prefs.seed;

function defaultLiveBridge(devServed) {
  if (location.protocol.startsWith('http') && location.host && !devServed) return `ws://${location.host}`;
  return 'ws://127.0.0.1:7878';
}
function deviceConfig() {
  if (SOURCE !== 'live') return DEMO_CONFIG;
  const b = parseBridge(prefs.liveBridge || $('liveBridge').value);
  return {host: b.host, port: b.port, token: prefs.liveToken};
}

function setLink(state, text) {
  $('linkDot').className = 'dot ' + state;
  $('linkText').textContent = text;
}

// ───────────────────────────────────────────── replay (real recorded session)
let replay = null;
let replayMod = null;
let termView = null;
let permAt = null;
function caption(text, ask = false) {
  $('rpCaption').textContent = text;
  $('rpCaption').classList.toggle('ask', ask);
}
function replayCaption(id, m) {
  if (id === 'start') caption('Recording of `agentmux run -- claude` (Claude Code 2.1.289).');
  else if (id === 'registered') caption('The session registered with the bridge. Open it on the device.');
  else if (id.startsWith('state:')) caption(`Claude is ${id.slice(6)}.`);
  else if (id === 'input') caption(`The recorded device sends: “${m.text}”`);
  else if (id === 'perm') { caption('Permission prompt. Answer it on the device (tap an option or press BOOT); playback waits.', true); syncPlay(); }
  else if (id === 'decide') { caption(`Answered “${m.label}”.${m.no ? ' The recording continues with “Yes”.' : ''}`); setTimeout(syncPlay, 0); }
  else if (id === 'exit') caption('The session exited. Restart to replay.');
}
function syncPlay() {
  if (!replay) return;
  const waiting = replay.hold === 'perm';
  $('rpPlay').textContent = replay.ended ? 'Replay' : waiting ? 'Waiting for device' : replay.playing ? 'Pause' : 'Play';
}
function replayRestart() {
  replay.reset();
  replay.decision = null;
  replay.playing = true;
  syncPlay();
}
async function loadReplay() {
  const [xterm, mod, tc] = await Promise.all([
    import('@xterm/headless'), import('./core/replay.js'), import('./core/term-canvas.js'),
  ]);
  replayMod = mod;
  const castUrl = new URLSearchParams(location.search).get('cast') || 'demo/claude-demo.cast';
  const text = await fetch(castUrl).then((r) => { if (!r.ok) throw new Error(`${castUrl}: ${r.status}`); return r.text(); });
  const cast = mod.parseCast(text);
  const term = new xterm.default.Terminal({cols: 100, rows: 30, scrollback: 200, allowProposedApi: true});
  termView = new tc.TermCanvas(term, {width: 1200, height: 760, title: 'claude — recording', font: 'ui-monospace, SFMono-Regular, Menlo, monospace'});
  term.onWriteParsed(() => (termView.dirty = true));
  term.onResize(() => termView.layout());
  $('rpTerm').appendChild(termView.canvas);
  replay = new mod.Replay(term, cast, {caption: replayCaption, ended: syncPlay});
  replay.playing = false; // starts once the firmware is up
  replay.speed = prefs.speed || 1;
  $('rpSpeed').textContent = replay.speed + '×';
  const p = cast.events.find((e) => e.k === 'm' && e.d.e === 'perm');
  if (p) { permAt = p.t; $('rpPerm').style.left = (100 * p.t / cast.duration) + '%'; $('rpPerm').hidden = false; }
  $('rpPlay').onclick = () => {
    if (replay.ended) return replayRestart();
    if (replay.hold === 'perm') return;
    replay.playing = !replay.playing;
    syncPlay();
  };
  $('rpRestart').onclick = replayRestart;
  $('rpSpeed').onclick = () => {
    replay.speed = {1: 2, 2: 4, 4: 8}[replay.speed] || 1;
    prefs.speed = replay.speed;
    savePrefs();
    $('rpSpeed').textContent = replay.speed + '×';
  };
  $('rpSkip').onclick = () => {
    if (permAt === null) return;
    if (replay.t > permAt || replay.decision !== null) replayRestart();
    replay.seek(permAt);
    replay.playing = true;
    syncPlay();
  };
}
const socketHooks = {
  input: () => caption('This is a recording: replies typed on the device are not sent anywhere.'),
  keys: (keys) => caption(`Recording: device keys (${keys.join(', ')}) are not sent.`),
};

// ───────────────────────────────────────────── firmware
let currentSid = '';
let lastSessions = null;
const sim = new SimHost({
  canvas,
  storage: 'idb',
  config: null, // set below, once the source is known
  mdns: () => {
    const c = deviceConfig();
    return `${c.host}:${c.port}:${c.host === 'demo' ? 'demo-bridge' : c.host}`;
  },
  openSocket: (url) => {
    if (SOURCE === 'demo') return new DemoBridgeSocket(url);
    if (SOURCE === 'replay') return new replayMod.ReplaySocket(replay, socketHooks);
    return new WebSocket(url);
  },
  onLink: (state, text) => setLink(state, `${{demo: 'Demo bridge', replay: 'Replay bridge', live: 'Live bridge'}[SOURCE]} · ${text}`),
  onLog: (line) => log(line, /\[ERR\]|error/i.test(line) ? 'err' : 'fw'),
  onFrame: (dir, text) => {
    if (dir === 'tx') {
      log('↑ ' + text.replace(/"token":"[^"]*"/, '"token":"***"'), 'tx');
      if (text.includes('"subscribe"')) { try { currentSid = JSON.parse(text).sid || ''; } catch (e) { /* not JSON */ } }
    } else {
      log('↓ ' + text, 'rx');
      if (text.startsWith('{"t":"sessions"')) { try { lastSessions = JSON.parse(text).items || []; onSessions(); } catch (e) { /* not JSON */ } }
    }
  },
});

// ───────────────────────────────────────────── input: screen = touch, keys = buttons
function uiPoint(ev) {
  const r = canvas.getBoundingClientRect();
  return [((ev.clientX - r.left) * UI_W) / r.width, ((ev.clientY - r.top) * UI_H) / r.height];
}
let touching = false;
canvas.addEventListener('pointerdown', (ev) => {
  if (ev.button !== 0) return;
  canvas.setPointerCapture(ev.pointerId);
  touching = true;
  sim.tap(0, ...uiPoint(ev));
  ev.preventDefault();
});
canvas.addEventListener('pointermove', (ev) => { if (touching) sim.tap(1, ...uiPoint(ev)); });
const endTouch = (ev) => {
  if (!touching) return;
  touching = false;
  sim.tap(2, ...uiPoint(ev));
};
canvas.addEventListener('pointerup', endTouch);
canvas.addEventListener('pointercancel', endTouch);

const buttons = Object.fromEntries([...document.querySelectorAll('.key[data-btn]')].map((b) => [b.dataset.btn, b]));
function press(name, key, down) {
  if (name && buttons[name]) buttons[name].classList.toggle('pressed', down);
  sim.key(key, down);
}
for (const b of Object.values(buttons)) {
  const k = Number(b.dataset.key);
  let down = false;
  b.addEventListener('pointerdown', (ev) => {
    if (ev.button !== 0) return;
    b.setPointerCapture(ev.pointerId);
    down = true;
    press(b.dataset.btn, k, true);
    ev.preventDefault();
  });
  const up = () => { if (down) { down = false; press(b.dataset.btn, k, false); } };
  b.addEventListener('pointerup', up);
  b.addEventListener('pointercancel', up);
  b.addEventListener('lostpointercapture', up);
  // Keyboard activation of the focused button (Space; Enter is BOOT globally).
  b.addEventListener('keydown', (ev) => { if (ev.key === ' ') { ev.preventDefault(); if (!ev.repeat) press(b.dataset.btn, k, true); } });
  b.addEventListener('keyup', (ev) => { if (ev.key === ' ') press(b.dataset.btn, k, false); });
}
const typing = (ev) => ev.target && /INPUT|TEXTAREA|SELECT/.test(ev.target.tagName);
addEventListener('keydown', (ev) => {
  if (typing(ev) || ev.metaKey || ev.ctrlKey || ev.altKey) return;
  const m = KEYBOARD[ev.key];
  if (m) {
    ev.preventDefault();
    if (!ev.repeat) press(m[0], m[1], true);
  } else if (ev.key === 'z' || ev.key === 'Z') {
    setZoom(ZOOMS[(ZOOMS.indexOf(prefs.zoom) + 1) % ZOOMS.length]);
  }
});
addEventListener('keyup', (ev) => {
  if (typing(ev)) return;
  const m = KEYBOARD[ev.key];
  if (m) press(m[0], m[1], false);
});
async function tapKey(key, ms = 80) {
  sim.key(key, true);
  await sleep(ms);
  sim.key(key, false);
  await sleep(ms + 40);
}
$('back').onclick = () => tapKey(KEY.Back);

// ───────────────────────────────────────────── device actions
$('seed').onchange = () => {
  prefs.seed = $('seed').checked;
  savePrefs();
  if (prefs.seed && sim.Module) sim.writeConfig(deviceConfig());
};
$('restartApp').onclick = () => {
  if (prefs.seed) sim.writeConfig(deviceConfig());
  sim.restartApp();
};
$('reset').onclick = async () => {
  await sim.resetStorage();
  DemoBridgeSocket.reset();
  location.reload();
};
$('copyPng').onclick = () => {
  const btn = $('copyPng');
  canvas.toBlob(async (blob) => {
    try {
      await navigator.clipboard.write([new ClipboardItem({'image/png': blob})]);
      btn.textContent = 'Copied';
    } catch (e) {
      btn.textContent = 'Not allowed';
    }
    setTimeout(() => (btn.textContent = 'Copy PNG'), 1500);
  }, 'image/png');
};
$('liveGo').onclick = () => {
  prefs.liveBridge = $('liveBridge').value.trim();
  prefs.liveToken = $('liveToken').value.trim();
  savePrefs();
  location.reload();
};

// ───────────────────────────────────────────── restore after a dev reload
// The dev loop reloads the page on every rebuild; reopen the session that was
// open (and put a replay back where it was) so UI iteration stays in place.
let restore = null;
try { restore = JSON.parse(sessionStorage.getItem(RESTORE_KEY) || 'null'); sessionStorage.removeItem(RESTORE_KEY); } catch (e) { /* none */ }
if (restore && restore.source !== SOURCE) restore = null;
function saveRestore() {
  const r = {source: SOURCE, sid: currentSid};
  if (replay) Object.assign(r, {t: replay.t, decision: replay.decision});
  try { sessionStorage.setItem(RESTORE_KEY, JSON.stringify(r)); } catch (e) { /* storage blocked */ }
}
const RANK = {blocked: 0, working: 1, idle: 2, unknown: 3, exited: 4};
let restoring = false;
async function onSessions() {
  if (!restore || !restore.sid || restoring || currentSid) return;
  // Same order as AgentMuxActivity::rebuildRows: stable by state rank.
  const rows = lastSessions.map((s, i) => [RANK[s.state] ?? 3, i, s.sid]).sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const index = rows.findIndex((r) => r[2] === restore.sid);
  if (index < 0) return;
  restoring = true;
  restore = null;
  await sleep(250);
  for (let i = 0; i < index; i++) await tapKey(KEY.Down);
  await tapKey(KEY.Confirm);
  log(`[page] reopened session ${rows[index][2]} after reload`);
}

// ───────────────────────────────────────────── main loop + boot
let ready = false;
let dev = null;
let firstFrame = true;
let tickLast = performance.now();
function frame(now) {
  const dt = Math.min(0.1, (now - tickLast) / 1000);
  tickLast = now;
  if (replay) {
    replay.update(dt);
    $('rpFill').style.width = (100 * replay.progress) + '%';
    $('rpTime').textContent = `${fmt(Math.min(replay.t, replay.duration))} / ${fmt(replay.duration)}`;
    if ($('rpTermBox').open) termView.draw();
  }
  const fb = sim.poll();
  if (fb.changed && ready) {
    $('loading').hidden = true;
    if (firstFrame) { firstFrame = false; if (dev) dev.loaded(); }
    if (fb.flash === 0 || fb.flash === 1) {
      canvas.classList.remove('flash-full', 'flash-half');
      void canvas.offsetWidth; // restart the animation
      canvas.classList.add(fb.flash === 0 ? 'flash-full' : 'flash-half');
    }
  }
  requestAnimationFrame(frame);
}

(async function main() {
  dev = await startDevLoop({
    onStatus: (state, text) => {
      $('devStatus').hidden = false;
      $('devDot').className = 'dot ' + state;
      $('devText').textContent = text;
    },
    beforeReload: saveRestore,
  });
  $('liveBridge').value = prefs.liveBridge || defaultLiveBridge(!!dev);
  $('liveToken').value = prefs.liveToken;
  if (SOURCE === 'live' && !prefs.liveBridge) prefs.liveBridge = $('liveBridge').value;
  if (SOURCE === 'live' && !prefs.liveToken) setLink('warn', 'Enter the pairing token and press Connect');

  if (SOURCE === 'replay') {
    try { await loadReplay(); } catch (e) { caption('Could not load the recording: ' + e.message); }
  }
  sim.opts.config = prefs.seed ? deviceConfig() : null;
  requestAnimationFrame(frame);
  try {
    await sim.start();
  } catch (e) {
    $('loading').textContent = 'Failed to load the firmware: ' + e;
    log(String(e), 'err');
    return;
  }
  ready = true;
  sim.dirty = true;
  log(`[page] firmware started · source ${SOURCE}${prefs.seed ? ' · config pre-written' : ''}`);
  if (replay) {
    if (restore && restore.t) replay.seek(restore.t, restore.decision || null);
    replay.playing = true;
    syncPlay();
  }
})();

// Automation hooks (tests, agent-browser).
window.__sim = {
  sim, get replay() { return replay; }, get scale() { return scale; }, layout, setZoom,
  tap(x, y, ms = 60) { sim.tap(0, x, y); setTimeout(() => sim.tap(2, x, y), ms); },
  key: tapKey, KEY,
  clientPointFor(x, y) {
    const r = canvas.getBoundingClientRect();
    return {x: r.left + ((x + 0.5) * r.width) / UI_W, y: r.top + ((y + 0.5) * r.height) / UI_H};
  },
  state: () => ({source: SOURCE, currentSid, sessions: lastSessions, replay: replay && {t: replay.t, hold: replay.hold, state: replay.session.state}}),
};
