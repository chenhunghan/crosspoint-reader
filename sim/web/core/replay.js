
// Same clean-up as the bridge's detect.rs (recordings made before it existed
// still carry Claude's "Tip:" line and dashed separators).
const RULE_CHARS = /[─━═╌╍┄┅┈┉╭╮╰╯┌┐└┘\-▔▁]/gu;
function isRule(line) {
  const n = [...line].length;
  if (n < 8) return false;
  return (line.match(RULE_CHARS) || []).length * 10 >= n * 8;
}
function cleanDetail(lines) {
  return lines.filter((l) => !isRule(l) && !l.startsWith('Tip:'));
}
// Demo mode: replays a real agentmux asciinema recording into the laptop's
// terminal and plays the bridge's part of the device protocol (docs/protocol.md)
// for the simulated e-ink device, driven by the recording's "m" markers and
// the terminal's screen text. Playback holds at the permission prompt until the
// device answers it, so the visitor makes the decision.

// ---- device rendering, ported from bridge/src/render.rs --------------------------
const GLYPHS = new Map();
const put = (chars, s) => { for (const c of chars) GLYPHS.set(c, s); };
put('─━═╌╍┄┅┈┉╴╶╸╺▔▁⎯', '-');
put('│┃║╎╏┆┇┊┋╵╷▏▕', '|');
put('❯›▶▸►➜➤❱⏵', '>');
put('◀◂◄‹❮', '<');
put('●⏺•◦∙○◉◎▪▫■□◆◇✻✳✶✽✢✦✧✱✲✺⁕·', '*');
put('⎿', 'L');
put('→⟶⇢', '->');
put('←⟵⇠', '<-');
put('↑', '^'); put('↓', 'v'); put('⇒', '=>'); put('↵⏎', "<-'"); put('↳', 'L>');
put('✓✔☑', 'v'); put('✗✘✕✖☒', 'x'); put('…', '...'); put('⚠', '!');
put('◐◑◒◓◔◕', 'o'); put('⏸', '||'); put('⏹', '[]');
put('   \t', ' ');

function mapLine(line) {
  let out = '';
  for (const c of line) {
    const m = GLYPHS.get(c);
    if (m !== undefined) { out += m; continue; }
    const u = c.codePointAt(0);
    if (u >= 0x2800 && u <= 0x28ff) out += '*';
    else if (u >= 0x2500 && u <= 0x257f) out += '+';
    else if (u >= 0x2580 && u <= 0x259f) out += '#';
    // Symbol blocks the device font lacks (arrows, misc technical, shapes,
    // dingbats, emoji); same ranges as bridge/src/render.rs.
    else if ((u >= 0x2190 && u <= 0x21ff) || (u >= 0x2300 && u <= 0x23ff) || (u >= 0x25a0 && u <= 0x27bf) ||
             (u >= 0x27f0 && u <= 0x27ff) || (u >= 0x2900 && u <= 0x297f) || (u >= 0x2b00 && u <= 0x2bff) ||
             (u >= 0x1f300 && u <= 0x1faff)) out += '*';
    else if ((u >= 0xfe00 && u <= 0xfe0f) || u === 0x200d) continue;
    else if (u < 0x20 || u === 0x7f) continue;
    else out += c;
  }
  return out.trimEnd();
}

// East Asian wide ranges (enough for CJK/Hangul/fullwidth forms).
function charWidth(c) {
  const u = c.codePointAt(0);
  if (u < 0x1100) return 1;
  if ((u <= 0x115f) || (u >= 0x2e80 && u <= 0xa4cf) || (u >= 0xac00 && u <= 0xd7a3) || (u >= 0xf900 && u <= 0xfaff) ||
      (u >= 0xfe30 && u <= 0xfe4f) || (u >= 0xff00 && u <= 0xff60) || (u >= 0xffe0 && u <= 0xffe6) || (u >= 0x20000 && u <= 0x3fffd)) return 2;
  return 1;
}

// A mapped line made only of dashes (>= 8): a horizontal rule.
function isDashRule(line) {
  const t = line.trim();
  return t.length >= 8 && /^-+$/.test(t);
}

function wrapLine(line, cols) {
  cols = Math.max(2, cols);
  if (isDashRule(line)) return ['-'.repeat(cols)];  // one line, never wrapped
  const out = [];
  let cur = '', w = 0;
  for (const c of line) {
    const cw = charWidth(c);
    if (w + cw > cols) {
      // Break after the last space in the second half of the line (as the
      // bridge does); hard-break long tokens.
      const i = cur.lastIndexOf(' ');
      const strWidth = (t) => [...t].reduce((a, ch) => a + charWidth(ch), 0);
      if (i > 0 && strWidth(cur.slice(0, i)) * 2 >= cols) {
        out.push(cur.slice(0, i).trimEnd());
        cur = cur.slice(i + 1);
      } else {
        out.push(cur.trimEnd());
        cur = '';
      }
      w = strWidth(cur);
    }
    cur += c; w += cw;
  }
  cur = cur.trimEnd();
  if (cur || !out.length) out.push(cur);
  return out;
}

export function renderScreen(raw, cols, rows) {
  const mapped = raw.map(mapLine);
  let end = mapped.length;
  while (end > 0 && !mapped[end - 1]) end--;
  const wrapped = [];
  for (const l of mapped.slice(0, end)) wrapped.push(...wrapLine(l, cols));
  return wrapped.slice(Math.max(0, wrapped.length - Math.max(1, rows)));
}

function renderBlock(raw, cols, max) {
  const out = [];
  for (const l of raw) for (const w of wrapLine(mapLine(l), cols)) { if (out.length === max) return out; out.push(w); }
  return out;
}
const renderSingle = (s, cols) => wrapLine(mapLine(s).trim(), cols)[0] || '';

// ---- cast parsing ---------------------------------------------------------------------
export function parseCast(text) {
  const lines = text.split('\n').filter((l) => l.trim());
  const header = JSON.parse(lines[0]);
  const events = lines.slice(1).map((l) => {
    const [t, k, d] = JSON.parse(l);
    return {t, k, d: k === 'm' ? JSON.parse(d) : d};
  });
  return {header, events, duration: events.length ? events[events.length - 1].t : 0};
}

const nowS = () => Math.floor(Date.now() / 1000);
const SCREEN_INTERVAL_MS = 2000;

// ---- the replay: player + bridge world -------------------------------------------------
export class Replay {
  // term: @xterm/headless Terminal; hooks: {caption(id, data), toast(text), held(reason), ended()}
  constructor(term, cast, hooks = {}) {
    this.term = term;
    this.cast = cast;
    this.hooks = hooks;
    this.sockets = new Set();
    this.speed = 1;
    this.playing = true;
    this.reset();
  }

  reset() {
    this.t = 0;
    this.idx = 0;
    this.hold = null;        // null | 'perm' | 'input'
    this.holdLeft = 0;       // seconds left for timed holds
    this.decision = null;
    this.inputShown = new Set();
    this.ended = false;
    this.screenRev = 0;
    const hadPerm = this.perm;
    this.perm = null;
    this.session = {sid: 's1', agent: 'claude', title: this.cast.header.title || 'demo', cwd: '~/demo', state: 'unknown', since: nowS()};
    // Stays registered across restarts so the device keeps its session open.
    if (this.registered === undefined) this.registered = false;
    this.term.reset();
    this.term.resize(this.cast.header.width || 100, this.cast.header.height || 30);
    if (hadPerm) this.broadcast({t: 'perm_closed', sid: 's1', req: hadPerm.req});
    this.broadcastSessions();
  }

  get duration() { return this.cast.duration; }
  get progress() { return Math.min(1, this.t / Math.max(1, this.duration)); }

  // dt in real seconds.
  update(dt) {
    if (this.hold === 'input') {
      this.holdLeft -= dt;
      if (this.holdLeft <= 0) this.hold = null;
    }
    if (this.playing && !this.hold && !this.ended) {
      this.t += dt * this.speed;
      this.applyUntil(this.t);
    }
    for (const s of this.sockets) s.maybeScreen();
  }

  applyUntil(t) {
    const ev = this.cast.events;
    while (this.idx < ev.length && ev[this.idx].t <= t && !this.hold) {
      const e = ev[this.idx];
      if (e.k === 'm' && this.marker(e)) return; // marker started a hold: stay on it
      this.idx++;
      if (e.k === 'o') { this.term.write(e.d); this.screenRev++; }
      else if (e.k === 'r') { const [c, r] = e.d.split('x').map(Number); this.term.resize(c, r); }
    }
    if (this.idx >= ev.length && !this.ended) {
      this.ended = true;
      this.hooks.ended && this.hooks.ended();
    }
  }

  // Returns true when playback must hold *before* consuming this marker.
  marker(e) {
    const m = e.d;
    const s = this.session;
    switch (m.e) {
      case 'start':
        s.agent = m.agent || s.agent;
        this.cap('start', m);
        break;
      case 'registered':
        this.registered = true;
        this.broadcastSessions();
        this.cap('registered', m);
        break;
      case 'state':
        this.setState(m.state);
        this.cap('state:' + m.state, m);
        break;
      case 'title':
        s.title = m.title;
        this.broadcastSessions();
        break;
      case 'input':
        if (!this.inputShown.has(this.idx)) {
          this.inputShown.add(this.idx);
          this.hold = 'input';
          this.holdLeft = 2.2;
          this.cap('input', m);
          this.hooks.held && this.hooks.held('input', m);
          return true;
        }
        break;
      case 'perm':
        if (this.decision === null) {
          this.perm = m;
          this.session.req = m.req;
          this.setState('blocked');
          for (const sock of this.sockets) sock.sendPerm();
          this.hold = 'perm';
          this.cap('perm', m);
          this.hooks.held && this.hooks.held('perm', m);
          return true;
        }
        break;
      case 'decide':
        // Already answered by the visitor on the device.
        break;
      case 'perm_closed':
        if (this.perm) {
          this.broadcast({t: 'perm_closed', sid: s.sid, req: this.perm.req});
          this.perm = null;
          delete s.req;
        }
        break;
      case 'exit':
        this.setState('exited');
        this.cap('exit', m);
        break;
    }
    return false;
  }

  cap(id, data) { this.hooks.caption && this.hooks.caption(id, data); }

  setState(state) {
    const s = this.session;
    if (s.state === state) return;
    s.state = state;
    s.since = nowS();
    if (state !== 'blocked') delete s.req;
    this.broadcast({t: 'status', sid: s.sid, state, since: s.since});
    this.broadcastSessions();
  }

  // The visitor answered on the device.
  decide(req, choice) {
    if (this.hold !== 'perm' || !this.perm || this.perm.req !== req) return false;
    const opt = (this.perm.options || []).find((o) => o.k === choice);
    this.decision = choice;
    this.hold = null;
    this.cap('decide', {choice, label: opt ? opt.label : choice, no: !!(opt && /^no\b/i.test(opt.label))});
    // Skip to the recorded decision so the session continues right away.
    const d = this.cast.events.find((e) => e.k === 'm' && e.d.e === 'decide');
    if (d && d.t > this.t) { this.t = d.t; this.applyUntil(this.t); }
    return true;
  }

  // Fast-forwards to `t` seconds, skipping timed holds. Stops at an unanswered
  // permission prompt unless `answer` (an option key) is given.
  seek(t, answer = null) {
    for (let guard = 0; guard < 100; guard++) {
      this.applyUntil(t);
      if (this.hold === 'input') { this.hold = null; this.holdLeft = 0; continue; }
      if (this.hold === 'perm' && answer && this.decide(this.perm.req, answer)) continue;
      break;
    }
    const ev = this.cast.events[this.idx];
    this.t = this.hold && ev ? ev.t : Math.max(this.t, Math.min(t, this.duration));
  }

  // Lets playback past the prompt without the device (Play while held).
  release() {
    if (this.hold === 'input') { this.hold = null; this.holdLeft = 0; }
  }

  sessionsFrame() {
    if (!this.registered) return {t: 'sessions', items: []};
    const s = this.session;
    const item = {sid: s.sid, agent: s.agent, title: s.title, cwd: s.cwd, state: s.state, since: s.since};
    if (s.req) item.req = s.req;
    return {t: 'sessions', items: [item]};
  }
  broadcastSessions() { this.broadcast(this.sessionsFrame()); }
  broadcast(obj) { for (const s of this.sockets) s.emit(obj); }

  screenLines(cols, rows) {
    const buf = this.term.buffer.active;
    const raw = [];
    for (let y = 0; y < this.term.rows; y++) {
      const l = buf.getLine(buf.viewportY + y);
      raw.push(l ? l.translateToString(true) : '');
    }
    return renderScreen(raw, cols, rows);
  }

  permFrame(cols) {
    const p = this.perm;
    return {
      t: 'perm', sid: this.session.sid, req: p.req,
      title: renderSingle(p.title || '', cols),
      detail: renderBlock(cleanDetail(p.detail || []), cols, 8),
      options: (p.options || []).map((o) => ({k: o.k, label: renderSingle(o.label, cols)})),
    };
  }
}

// A WebSocket look-alike the simulator's BridgeClient talks to.
export class ReplaySocket {
  constructor(replay, hooks = {}) {
    this.replay = replay;
    this.hooks = hooks;   // {frame(dir, obj), input(text), keys(keys), subscribed(sid)}
    this.readyState = 0;
    this.onopen = this.onmessage = this.onclose = this.onerror = null;
    this.cols = 40;
    this.rows = 20;
    this.authed = false;
    this.sub = '';
    this.lastLines = null;
    this.lastScreenAt = 0;
    this.lastRev = -1;
    this.seq = 0;
    setTimeout(() => {
      if (this.readyState !== 0) return;
      this.readyState = 1;
      replay.sockets.add(this);
      this.onopen && this.onopen({});
    }, 150);
  }

  emit(obj) {
    if (this.readyState !== 1 || !this.authed) return;
    this.hooks.frame && this.hooks.frame('out', obj);
    this.onmessage && this.onmessage({data: JSON.stringify(obj)});
  }

  close() {
    if (this.readyState === 3) return;
    this.readyState = 3;
    this.replay.sockets.delete(this);
    this.onclose && this.onclose({code: 1000});
  }

  sendPerm() {
    const r = this.replay;
    if (r.perm) this.emit(r.permFrame(this.cols));
  }

  maybeScreen(force) {
    const r = this.replay;
    if (!this.authed || this.sub !== r.session.sid || !r.registered) return;
    const now = performance.now();
    if (!force && (now - this.lastScreenAt < SCREEN_INTERVAL_MS || r.screenRev === this.lastRev)) return;
    this.lastRev = r.screenRev;
    const lines = r.screenLines(this.cols, this.rows);
    const key = lines.join('\n');
    if (key === this.lastLines && !force) return;
    this.lastLines = key;
    this.lastScreenAt = now;
    this.emit({t: 'screen', sid: r.session.sid, seq: ++this.seq, lines});
  }

  send(text) {
    if (this.readyState !== 1) throw new Error('replay bridge: socket not open');
    let msg;
    try { msg = JSON.parse(text); } catch (e) { return; }
    this.hooks.frame && this.hooks.frame('in', msg);
    setTimeout(() => this.handle(msg), 30);
  }

  handle(msg) {
    const r = this.replay;
    if (msg.t === 'hello') {
      this.cols = msg.cols || 40;
      this.rows = msg.rows || 20;
      this.authed = true;
      this.emit({t: 'ok', host: 'demo-laptop', v: 1});
      this.emit(r.sessionsFrame());
      this.sendPerm();
      return;
    }
    if (!this.authed) return;
    const known = msg.sid === r.session.sid && r.registered;
    switch (msg.t) {
      case 'ping': this.emit({t: 'pong'}); break;
      case 'list': this.emit(r.sessionsFrame()); break;
      case 'subscribe':
        this.sub = msg.sid || '';
        this.lastLines = null;
        this.hooks.subscribed && this.hooks.subscribed(this.sub);
        if (known) { this.maybeScreen(true); this.sendPerm(); }
        break;
      case 'decide':
        if (!known) { this.emit({t: 'err', code: 'no_session', msg: String(msg.sid)}); break; }
        if (!r.decide(msg.req, String(msg.choice))) this.emit({t: 'err', code: 'stale_req', msg: String(msg.req)});
        break;
      case 'input':
        if (!known) { this.emit({t: 'err', code: 'no_session', msg: String(msg.sid)}); break; }
        this.hooks.input && this.hooks.input(String(msg.text || ''));
        break;
      case 'keys':
        if (!known) { this.emit({t: 'err', code: 'no_session', msg: String(msg.sid)}); break; }
        this.hooks.keys && this.hooks.keys(msg.keys || []);
        break;
      default:
        this.emit({t: 'err', code: 'bad_request', msg: 'unknown t=' + msg.t});
    }
  }
}
