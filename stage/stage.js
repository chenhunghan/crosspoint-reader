// Agent Mux stage: a laptop running Claude Code and a Metalio E-Ink 4 running
// the real CrossPoint firmware (WebAssembly), side by side in three.js.
//   demo (default): replays a real recording; the device talks to an in-page replay bridge.
//   live (?mode=live&bridge=ws://HOST:7878&token=XXXX[&sid=s1]): the laptop shows the
//   session's /term stream, the device connects to the real bridge's /device.
// Scene look, camera and controls adapted from lns-lab.
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {RoomEnvironment} from 'three/addons/environments/RoomEnvironment.js';
import {RoundedBoxGeometry} from 'three/addons/geometries/RoundedBoxGeometry.js';
import XtermHeadless from '@xterm/headless';
import {SimHost, KEY, UI_W, UI_H, parseBridge} from './sim-host.js';
import {TermCanvas} from './term-canvas.js';
import {Replay, ReplaySocket, parseCast} from './replay.js';
import {buildLaptop} from './laptop.js';
import {buildDevice, DEV} from './device.js';

const $ = (id) => document.getElementById(id);
const q = new URLSearchParams(location.search);
const MODE = q.get('mode') === 'live' ? 'live' : 'demo';
const CAST_URL = q.get('cast') || 'demo/claude-demo.cast';
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]));
const fmt = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
const easeIO = (t) => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2);

// ───────────────────────────────────────────────────────────── renderer, scene, lights
const host = $('world');
const scene = new THREE.Scene();
scene.background = new THREE.Color('#080b14');
scene.fog = new THREE.FogExp2('#0a0e19', 0.0075);
const camera = new THREE.PerspectiveCamera(30, 1, 1, 600);
const renderer = new THREE.WebGLRenderer({antialias: true, powerPreference: 'high-performance'});
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.15;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFShadowMap;
host.appendChild(renderer.domElement);
const pmrem = new THREE.PMREMGenerator(renderer);
scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
scene.environmentIntensity = 0.45;

scene.add(new THREE.HemisphereLight('#b4c8ff', '#0b0d14', 1.1));
const sun = new THREE.DirectionalLight('#f1f4ff', 1.7);
sun.position.set(-14, 30, 22);
sun.castShadow = true;
sun.shadow.mapSize.set(2048, 2048);
Object.assign(sun.shadow.camera, {left: -34, right: 34, top: 26, bottom: -20, near: 1, far: 90});
sun.shadow.bias = -0.0004;
sun.shadow.radius = 4;
scene.add(sun);
const rim = new THREE.DirectionalLight('#ffb98a', 0.55);
rim.position.set(26, 10, -16);
scene.add(rim);
const devLamp = new THREE.PointLight('#fff4ea', 90, 42, 1.5);
devLamp.position.set(10, 13, 19);
scene.add(devLamp);
const lapLamp = new THREE.PointLight('#cfe0ff', 70, 44, 1.5);
lapLamp.position.set(-9, 15, 17);
scene.add(lapLamp);

// Ground and a faint grid (as in lns-lab).
{
  const g = new THREE.Mesh(new THREE.PlaneGeometry(420, 420), new THREE.MeshStandardMaterial({color: '#0d111c', roughness: 0.9}));
  g.rotation.x = -Math.PI / 2;
  g.position.y = -0.52;
  g.receiveShadow = true;
  scene.add(g);
  const grid = new THREE.Mesh(new THREE.PlaneGeometry(160, 160), new THREE.ShaderMaterial({
    transparent: true, depthWrite: false,
    vertexShader: 'varying vec3 vW; void main(){ vec4 w = modelMatrix*vec4(position,1.); vW = w.xyz; gl_Position = projectionMatrix*viewMatrix*w; }',
    fragmentShader: `varying vec3 vW;
      float line(float x, float w){ float d = abs(fract(x-.5)-.5); return 1.-smoothstep(0., w, d); }
      void main(){
        vec2 p = vW.xz;
        float g = max(line(p.x/2., .012), line(p.y/2., .012))*.5 + max(line(p.x/10., .004), line(p.y/10., .004))*.6;
        float fall = exp(-length(p - vec2(4.,2.))*.045);
        gl_FragColor = vec4(vec3(.35,.55,.95)*g, g*.14*fall);
      }`,
  }));
  grid.rotation.x = -Math.PI / 2;
  grid.position.y = -0.5;
  scene.add(grid);
}

// ───────────────────────────────────────────────────────────── terminal + laptop
const term = new XtermHeadless.Terminal({cols: 100, rows: 30, scrollback: 200, allowProposedApi: true});
const termCanvas = new TermCanvas(term, {title: MODE === 'live' ? 'claude — live' : 'claude — demo'});
term.onWriteParsed(() => (termCanvas.dirty = true));
term.onResize(() => termCanvas.layout());
const laptop = buildLaptop(termCanvas.canvas);
laptop.group.position.set(-7, 0, 0);
scene.add(laptop.group);

// ───────────────────────────────────────────────────────────── device on a small stand
const sim = new SimHost({
  ...(MODE === 'live' ? liveDeviceTarget() : {host: 'demo', port: 7878, token: 'demo1234'}),
  openSocket: (url) => (MODE === 'live' ? new WebSocket(url) : new ReplaySocket(replay, socketHooks)),
  onLink: (state, text) => setPill('dev', state, (MODE === 'live' ? 'device · ' : 'device · demo bridge · ') + text),
  onLog: (line) => console.debug('[fw]', line),
});
const device = buildDevice(sim.canvas);
const rig = new THREE.Group();
rig.position.set(15.5, -0.5, 7.5);
rig.rotation.y = -0.38;
scene.add(rig);
{
  const standMat = new THREE.MeshStandardMaterial({color: '#262a33', roughness: 0.55, metalness: 0.35});
  const base = new THREE.Mesh(new RoundedBoxGeometry(8.6, 0.9, 3.4, 3, 0.3), standMat);
  base.position.y = 0.45;
  base.castShadow = base.receiveShadow = true;
  const back = new THREE.Mesh(new RoundedBoxGeometry(5.2, 5.2, 0.5, 3, 0.22), standMat);
  back.position.set(0, 2.9, -1.35);
  back.rotation.x = -0.12;
  back.castShadow = true;
  rig.add(base, back);
}
const pivot = new THREE.Group();
pivot.position.set(0, 0.62, 0.15);
pivot.rotation.x = -0.12;
rig.add(pivot);
device.group.position.y = DEV.h / 2;
pivot.add(device.group);

// ───────────────────────────────────────────────────────────── camera + controls
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.minDistance = 8;
controls.maxDistance = 320;
controls.maxPolarAngle = Math.PI * 0.48;
controls.screenSpacePanning = true;

function fitDist(w, h, margin = 1.12) {
  const v = Math.tan(THREE.MathUtils.degToRad(camera.fov / 2));
  return Math.max((h / 2) / v, (w / 2) / (v * camera.aspect)) * margin;
}
function viewOf(name) {
  scene.updateMatrixWorld(true);
  if (name === 'device') {
    // Aim below the centre so the caption dock does not cover the keys.
    const c = new THREE.Vector3(0, -2.2, 0).applyMatrix4(device.group.matrixWorld);
    const n = new THREE.Vector3(0.14, 0.1, 1).normalize().transformDirection(device.group.matrixWorld);
    return [c.clone().addScaledVector(n, fitDist(DEV.w * 1.7, DEV.h * 1.45)), c];
  }
  if (name === 'laptop') {
    const c = laptop.centre.clone().applyMatrix4(laptop.group.matrixWorld);
    const n = laptop.normal.clone().add(new THREE.Vector3(0, 0.12, 0)).normalize().transformDirection(laptop.group.matrixWorld);
    return [c.clone().addScaledVector(n, fitDist(laptop.size.w, laptop.size.h, 1.32)), c];
  }
  // Overview: laptop left (larger), device right.
  const t = new THREE.Vector3(2.4, 6.6, 1);
  const dir = new THREE.Vector3(-0.06, 0.34, 1).normalize();
  if (camera.aspect < 0.85) {
    const tp = new THREE.Vector3(-1, 5.5, 4);
    return [tp.clone().addScaledVector(dir, fitDist(36, 30, 1.0)), tp];
  }
  return [t.clone().addScaledVector(dir, fitDist(60, 36, 1.0)), t];
}
let camTween = null;
function flyTo(name, dur = 1.3) {
  const [pos, tgt] = viewOf(name);
  camTween = {t: 0, dur, p0: camera.position.clone(), t0: controls.target.clone(), pos, tgt};
  view = name;
  for (const [id, v] of [['viewAll', 'overview'], ['viewLaptop', 'laptop'], ['viewDevice', 'device']]) $(id).classList.toggle('on', v === name);
}
let view = 'overview';
controls.addEventListener('start', () => (camTween = null));

function resize() {
  const w = host.clientWidth, h = host.clientHeight;
  camera.aspect = w / h;
  // Narrow screens: shift the subject up a little so the caption dock does not cover it.
  if (w < 760) camera.setViewOffset(w, h, 0, h * 0.06, w, h);
  else camera.clearViewOffset();
  camera.updateProjectionMatrix();
  renderer.setSize(w, h, false);
  // Portrait: bring the device in front of the laptop so both fit a narrow frame.
  const portrait = camera.aspect < 0.85;
  rig.position.set(portrait ? 7.5 : 15.5, -0.5, portrait ? 15 : 7.5);
  rig.rotation.y = portrait ? -0.2 : -0.38;
}
addEventListener('resize', () => { resize(); if (!camTween) { const [p, t] = viewOf(view); camera.position.copy(p); controls.target.copy(t); } });
resize();
{
  const [p, t] = viewOf('overview');
  camera.position.copy(p).add(new THREE.Vector3(-8, 10, 14));
  controls.target.copy(t);
}

// ───────────────────────────────────────────────────────────── device input (raycast)
const ray = new THREE.Raycaster();
const ndc = new THREE.Vector2();
let grab = null; // {kind:'button', mesh} | {kind:'screen', last:[x,y]}
function pick(ev) {
  const r = renderer.domElement.getBoundingClientRect();
  ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
  ray.setFromCamera(ndc, camera);
  const hit = ray.intersectObjects([device.group, laptop.group], true)[0];
  if (!hit) return null;
  if (hit.object === device.screen) return {kind: 'screen', uv: hit.uv};
  if (device.buttons.includes(hit.object)) return {kind: 'button', mesh: hit.object};
  return null;
}
const uvToUi = (uv) => [uv.x * UI_W, (1 - uv.y) * UI_H];
function pressButton(mesh, down) {
  device.setDown(mesh.name, down);
  sim.key(mesh.userData.key, down);
}
// Registered before OrbitControls sees the event (both on the canvas; ours first).
renderer.domElement.addEventListener('pointerdown', (ev) => {
  if (ev.button !== 0) return;
  const p = pick(ev);
  if (!p) return;
  controls.enabled = false;
  renderer.domElement.setPointerCapture(ev.pointerId);
  if (p.kind === 'button') {
    grab = {kind: 'button', mesh: p.mesh};
    pressButton(p.mesh, true);
  } else {
    const [x, y] = uvToUi(p.uv);
    grab = {kind: 'screen', last: [x, y]};
    sim.tap(0, x, y);
  }
}, {capture: true});
renderer.domElement.addEventListener('pointermove', (ev) => {
  if (grab && grab.kind === 'screen') {
    const r = renderer.domElement.getBoundingClientRect();
    ndc.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
    ray.setFromCamera(ndc, camera);
    const hit = ray.intersectObject(device.screen)[0];
    if (hit) { grab.last = uvToUi(hit.uv); sim.tap(1, ...grab.last); }
    return;
  }
  if (!grab && ev.buttons === 0) hoverEv = ev;
});
const endGrab = () => {
  if (!grab) return;
  if (grab.kind === 'button') pressButton(grab.mesh, false);
  else sim.tap(2, ...grab.last);
  grab = null;
  controls.enabled = true;
};
renderer.domElement.addEventListener('pointerup', endGrab);
renderer.domElement.addEventListener('pointercancel', endGrab);
let hoverEv = null;
function updateHover() {
  if (!hoverEv) return;
  const p = pick(hoverEv);
  hoverEv = null;
  renderer.domElement.classList.toggle('hot', !!p);
  renderer.domElement.title = p ? (p.kind === 'screen' ? 'Tap the e-ink screen' : p.mesh.userData.title) : '';
}

// Hardware-key shortcuts (same as dev.html), animated on the 3D buttons.
const KEYMAP = {
  Enter: ['boot', KEY.Confirm], ArrowUp: ['volUp', KEY.Up], ArrowDown: ['volDown', KEY.Down],
  ArrowLeft: [null, KEY.Left], ArrowRight: [null, KEY.Right], Escape: [null, KEY.Back], Backspace: [null, KEY.Back],
  h: ['home', KEY.Home], H: ['home', KEY.Home], p: ['power', KEY.Power], P: ['power', KEY.Power],
};
const typing = (ev) => ev.target && /INPUT|TEXTAREA/.test(ev.target.tagName);
addEventListener('keydown', (ev) => {
  if (typing(ev) || ev.metaKey || ev.ctrlKey || ev.altKey) return;
  const m = KEYMAP[ev.key];
  if (m) {
    ev.preventDefault();
    if (ev.repeat) return;
    if (m[0]) device.setDown(m[0], true);
    sim.key(m[1], true);
    return;
  }
  if (ev.key === ' ' && MODE === 'demo') { ev.preventDefault(); togglePlay(); }
  else if ((ev.key === 'r' || ev.key === 'R') && MODE === 'demo') restart();
  else if (ev.key === 'o' || ev.key === 'O') flyTo('overview');
  else if (ev.key === 'l' || ev.key === 'L') flyTo('laptop');
  else if (ev.key === 'f' || ev.key === 'F') flyTo('device');
});
addEventListener('keyup', (ev) => {
  const m = KEYMAP[ev.key];
  if (!m || typing(ev)) return;
  if (m[0]) device.setDown(m[0], false);
  sim.key(m[1], false);
});
function autoPress(name, ms = 160) {
  const b = device.byName[name];
  pressButton(b, true);
  setTimeout(() => pressButton(b, false), ms);
}
$('viewAll').onclick = () => flyTo('overview');
$('viewLaptop').onclick = () => flyTo('laptop');
$('viewDevice').onclick = () => flyTo('device');

// ───────────────────────────────────────────────────────────── HUD helpers
let toastT = null;
function toast(text, ms = 3200) {
  const t = $('toast');
  t.textContent = text;
  t.classList.add('on');
  clearTimeout(toastT);
  toastT = setTimeout(() => t.classList.remove('on'), ms);
}
function caption(no, html, {ask = false, actions = []} = {}) {
  $('capNo').textContent = no;
  $('capTx').innerHTML = html;
  $('cap').classList.toggle('ask', ask);
  const act = $('capAct');
  act.replaceChildren(...actions.map(([label, fn]) => {
    const b = document.createElement('button');
    b.className = 'btn';
    b.textContent = label;
    b.onclick = fn;
    return b;
  }));
  act.hidden = !actions.length;
}
function setPill(which, state, text) {
  $(which + 'Dot').className = 'dot ' + state;
  $(which + 'Text').textContent = text;
}

// ───────────────────────────────────────────────────────────── demo mode
let replay = null;
let deviceSub = '';
const demo = {promptSent: false, decided: false};
const socketHooks = {
  subscribed: (sid) => (deviceSub = sid),
  input: () => toast('This is a recording: replies typed on the device are not sent. Try live mode with your own bridge.', 4200),
  keys: (keys) => toast(`Recording: device keys (${keys.join(', ')}) are not sent to the replay.`),
};

function demoCaption(id, m) {
  const s = replay.session;
  switch (id) {
    case 'start':
      caption('recording', 'A real session, recorded with <code>agentmux run -- claude</code> (Claude Code 2.1.289). The bridge mirrors its terminal.');
      break;
    case 'registered':
      caption('bridge', 'The session registers with the agentmux bridge, and the e-ink device lists it.');
      break;
    case 'state:idle':
      if (!demo.promptSent) {
        caption('ready', 'Claude is ready for a prompt. The device opens the session (BOOT = confirm).');
        if (deviceSub !== s.sid) setTimeout(() => deviceSub !== s.sid && autoPress('boot'), 500);
      } else if (demo.decided) {
        caption('done', 'Done. Claude wrote <code>greeting.txt</code> and is idle again, ready for the next prompt from the device.');
      }
      break;
    case 'input':
      if (deviceSub !== s.sid) autoPress('boot');
      if (m.text === '/exit') {
        toast('The e-ink device sends /exit');
        caption('device → claude', 'The device sends <code>/exit</code> to end the session.');
      } else {
        demo.promptSent = true;
        toast('The e-ink device sends a prompt');
        caption('device → claude', `The device sends a prompt: <b>“${esc(m.text)}”</b>`);
      }
      break;
    case 'state:working':
      if (demo.promptSent && !demo.decided) caption('working', 'Claude is working. The device mirrors the terminal, at most one e-ink refresh every 2 s.');
      break;
    case 'perm': {
      const cmd = (m.detail || []).find((l) => /\S/.test(l) && !/^[╌─-]+$/.test(l) && !/^Tip:/.test(l) && !/^Write/.test(l)) || '';
      caption('permission', `Claude asks to run a Bash command${cmd ? `: <code>${esc(cmd)}</code>` : ''}. <b>Answer on the e-ink device</b>: tap <b>Yes</b> on its screen, or press its BOOT key.`, {
        ask: true,
        actions: [['Press BOOT for me (Yes)', () => autoPress('boot')]],
      });
      $('progPerm').hidden = false;
      syncPlay();
      flyTo('device', 1.4);
      break;
    }
    case 'decide':
      demo.decided = true;
      setTimeout(syncPlay, 0);
      if (m.no) toast('In this recording the user approved: continuing with “Yes”.', 4200);
      caption('approved', `Answered on the device (“${esc(m.label)}”). The bridge types it into Claude Code, which runs the command.`);
      setTimeout(() => view === 'device' && flyTo('overview', 1.5), 1600);
      break;
    case 'exit':
      caption('exited', 'The session exited, and the device shows it as exited. <b>Restart</b> to replay.');
      break;
  }
}

function togglePlay() {
  if (!replay) return;
  if (replay.ended) return restart();
  if (replay.hold === 'perm') { toast('Waiting for your answer on the e-ink device'); return; }
  replay.playing = !replay.playing;
  syncPlay();
}
function syncPlay() {
  const playing = replay && replay.playing && !replay.ended;
  const waiting = replay && replay.hold === 'perm';
  $('playText').textContent = replay && replay.ended ? 'Replay' : waiting ? 'Waiting for device' : playing ? 'Pause' : 'Play';
  $('playIcon').innerHTML = playing ? '<path d="M2 1h3v10H2zM7 1h3v10H7z"/>' : '<path d="M2 1l9 5-9 5z"/>';
}
function restart() {
  if (!replay) return;
  replay.reset();
  replay.playing = true;
  demo.promptSent = demo.decided = false;
  $('progPerm').hidden = true;
  syncPlay();
  flyTo('overview');
}
$('play').onclick = togglePlay;
$('restart').onclick = restart;
$('speed').onclick = () => {
  if (!replay) return;
  replay.speed = replay.speed === 1 ? 2 : replay.speed === 2 ? 4 : 1;
  $('speed').textContent = replay.speed + '×';
};

async function startDemo() {
  setPill('dev', 'warn', 'device · demo bridge');
  const text = await fetch(CAST_URL).then((r) => {
    if (!r.ok) throw new Error(`${CAST_URL}: ${r.status}`);
    return r.text();
  });
  const cast = parseCast(text);
  replay = new Replay(term, cast, {
    caption: demoCaption,
    ended: () => syncPlay(),
  });
  replay.playing = false; // starts once the firmware is up
  const permAt = cast.events.find((e) => e.k === 'm' && e.d.e === 'perm');
  if (permAt) $('progPerm').style.left = (100 * permAt.t / cast.duration) + '%';
  $('progPerm').hidden = true;
  syncPlay();
}

// ───────────────────────────────────────────────────────────── live mode
function liveParams() {
  const defBridge = location.protocol.startsWith('http') && location.host ? `ws://${location.host}` : 'ws://127.0.0.1:7878';
  return {bridge: (q.get('bridge') || defBridge).replace(/\/+$/, '').replace(/\/device$/, ''), token: q.get('token') || '', sid: q.get('sid') || ''};
}
function liveDeviceTarget() {
  const p = liveParams();
  const b = parseBridge(p.bridge);
  return {host: b.host, port: b.port, token: p.token};
}

async function firstSid(bridge, token) {
  return new Promise((resolve) => {
    let done = false;
    const finish = (sid) => { if (!done) { done = true; try { ws.close(); } catch (e) { /* closed */ } resolve(sid); } };
    const ws = new WebSocket(`${bridge}/monitor?token=${encodeURIComponent(token)}`);
    ws.onmessage = (ev) => {
      try {
        const m = JSON.parse(ev.data);
        if (m.t === 'snapshot') {
          const live = (m.sessions || []).filter((s) => s.state !== 'exited');
          finish((live[0] || (m.sessions || [])[0] || {}).sid || 's1');
        } else if (m.t === 'err') finish('s1');
      } catch (e) { finish('s1'); }
    };
    ws.onerror = () => finish('s1');
    setTimeout(() => finish('s1'), 2500);
  });
}

async function startLive() {
  const p = liveParams();
  $('bar').hidden = true;
  $('termPill').hidden = false;
  $('modeLine').textContent = 'The laptop streams a live session from your agentmux bridge; the device is connected to the same bridge.';
  if (!p.token) {
    $('livebox').hidden = false;
    $('lbBridge').value = p.bridge;
    $('lbSid').value = p.sid;
    $('lbGo').onclick = () => {
      const u = new URL(location.href);
      u.searchParams.set('mode', 'live');
      u.searchParams.set('bridge', $('lbBridge').value.trim() || p.bridge);
      u.searchParams.set('token', $('lbToken').value.trim());
      if ($('lbSid').value.trim()) u.searchParams.set('sid', $('lbSid').value.trim()); else u.searchParams.delete('sid');
      location.href = u.toString();
    };
    caption('live', 'Enter your bridge address and pairing token.');
    return false;
  }
  const sid = p.sid || (await firstSid(p.bridge, p.token));
  caption('live', `Streaming session <code>${esc(sid)}</code> from <code>${esc(p.bridge)}</code>. Open it on the device to watch, approve prompts or reply; the laptop shows the real terminal.`);
  const connect = () => {
    setPill('term', 'warn', `terminal · ${sid} · connecting`);
    const ws = new WebSocket(`${p.bridge}/term?sid=${encodeURIComponent(sid)}&token=${encodeURIComponent(p.token)}`);
    ws.binaryType = 'arraybuffer';
    let fatal = false;
    ws.onmessage = (ev) => {
      if (typeof ev.data !== 'string') { term.write(new Uint8Array(ev.data)); return; }
      let m;
      try { m = JSON.parse(ev.data); } catch (e) { return; }
      if (m.t === 'term_init' || m.t === 'term_resize') {
        term.resize(m.cols, m.rows);
        termCanvas.setTitle(`claude — live · ${sid} · ${m.cols}×${m.rows}`);
        setPill('term', 'ok', `terminal · ${sid}`);
      } else if (m.t === 'term_exit') {
        setPill('term', 'warn', `terminal · ${sid} · exited`);
        termCanvas.setStatus('exited');
      } else if (m.t === 'err') {
        fatal = m.code === 'auth';
        setPill('term', 'err', `terminal · ${m.code}${m.msg ? ': ' + m.msg : ''}`);
        if (m.code === 'auth') toast('The bridge rejected the token');
      }
    };
    ws.onclose = () => {
      if (!fatal) setTimeout(connect, 3000);
      if ($('termDot').className.includes('ok')) setPill('term', 'err', `terminal · ${sid} · reconnecting`);
    };
  };
  connect();
  return true;
}

// ───────────────────────────────────────────────────────────── main loop
let last = performance.now();
let simReady = false;
function frame(now) {
  const dt = Math.min(0.1, (now - last) / 1000);
  last = now;
  if (replay) {
    replay.update(dt);
    const d = replay.duration;
    $('progFill').style.width = (100 * replay.progress) + '%';
    $('time').textContent = `${fmt(Math.min(replay.t, d))} / ${fmt(d)}`;
    termCanvas.setStatus(replay.hold === 'perm' ? 'waiting for the device…' : replay.session.state);
  }
  const fb = sim.poll();
  if (fb.changed) {
    device.texture.needsUpdate = true;
    if (fb.flash >= 0) device.flash(fb.flash);
    if (simReady) $('loading').classList.add('done');
  }
  if (termCanvas.draw()) laptop.texture.needsUpdate = true;
  device.update(dt);
  if (camTween) {
    camTween.t += dt;
    const k = easeIO(Math.min(1, camTween.t / camTween.dur));
    camera.position.lerpVectors(camTween.p0, camTween.pos, k);
    controls.target.lerpVectors(camTween.t0, camTween.tgt, k);
    if (k >= 1) camTween = null;
  }
  updateHover();
  controls.update();
  scene.fog.density = 0.0075 * Math.min(1, 70 / Math.max(1, camera.position.distanceTo(controls.target)));
  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}

// Debug / automation hooks.
window.__stage = {
  get replay() { return replay; },
  sim, device, camera, controls, term, flyTo,
  tapDevice(x, y) { sim.tap(0, x, y); setTimeout(() => sim.tap(2, x, y), 60); },
  press(name) { autoPress(name); },
  // Page coordinates of a UI pixel on the e-ink screen (for real raycast clicks).
  clientPointFor(x, y) {
    const s = device.screenSize;
    const p = new THREE.Vector3((x / UI_W - 0.5) * s.w, s.y + (0.5 - y / UI_H) * s.h, s.z).applyMatrix4(device.screen.parent.matrixWorld).project(camera);
    const r = renderer.domElement.getBoundingClientRect();
    return {x: r.left + (p.x + 1) / 2 * r.width, y: r.top + (1 - p.y) / 2 * r.height};
  },
  state() {
    return replay ? {t: replay.t, hold: replay.hold, state: replay.session.state, ended: replay.ended, deviceSub, perm: !!replay.perm} : {mode: MODE};
  },
  screenText() {
    const b = term.buffer.active, out = [];
    for (let y = 0; y < term.rows; y++) out.push((b.getLine(b.viewportY + y) || {translateToString: () => ''}).translateToString(true));
    return out.join('\n');
  },
};

(async function main() {
  requestAnimationFrame(frame);
  flyTo('overview', 2.2);
  try { await Promise.race([document.fonts.load('500 20px "DM Mono"'), new Promise((r) => setTimeout(r, 2500))]); } catch (e) { /* fallback font */ }
  termCanvas.layout();
  caption('loading', 'Booting the e-ink firmware…');
  let ok = true;
  if (MODE === 'live') ok = await startLive();
  else {
    try { await startDemo(); } catch (e) { caption('error', 'Could not load the recording: ' + esc(e.message)); ok = false; }
  }
  if (!ok && MODE === 'live') { $('loading').classList.add('done'); return; }
  try {
    await sim.start();
    simReady = true;
    sim.dirty = true;
    if (replay) { replay.playing = true; syncPlay(); }
  } catch (e) {
    caption('error', 'The firmware failed to start: ' + esc(e));
    $('loading').classList.add('done');
  }
})();
