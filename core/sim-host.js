// Boots the firmware simulator (sim.js / sim.wasm, loaded as a classic script
// that defines window.createSim) and connects it to the page: the 480x800
// framebuffer canvas, key and touch injection, the SD card (in memory or kept
// in IndexedDB), and the firmware's WebSocket, which goes to `openSocket(url)`
// (a real WebSocket or an in-page bridge with the same surface).
// Shared by the device page (index.html) and the 3D stage (stage.html).
export const UI_W = 480;
export const UI_H = 800;
export const KEY = {Back: 0, Confirm: 1, Left: 2, Right: 3, Up: 4, Down: 5, Power: 6, Home: 7};

// Keyboard shortcuts: event.key -> [device button name | null, KEY].
export const KEYBOARD = {
  Enter: ['boot', KEY.Confirm],
  ArrowUp: ['volUp', KEY.Up],
  ArrowDown: ['volDown', KEY.Down],
  ArrowLeft: [null, KEY.Left],
  ArrowRight: [null, KEY.Right],
  Escape: [null, KEY.Back],
  Backspace: [null, KEY.Back],
  h: ['home', KEY.Home], H: ['home', KEY.Home],
  p: ['power', KEY.Power], P: ['power', KEY.Power],
};

const SD = '/sd';
const CONFIG = SD + '/.crosspoint/agentmux.json';

export class SimHost {
  // opts: {
  //   canvas?: HTMLCanvasElement (480x800; created when omitted),
  //   storage: 'memory' (fresh SD each load, default) | 'idb' (kept in IndexedDB),
  //   config: {host, port, token} | null: written to the SD card before boot (skips first-run setup),
  //   mdns(): "host:port:name" answer for the device's bridge discovery (default: config),
  //   openSocket(url) -> socket, onLink(state, text), onLog(line), onFrame('tx'|'rx', text),
  // }
  constructor(opts) {
    this.opts = opts;
    this.Module = null;
    this.ready = false; // runtime initialised (Module is set earlier, in preRun)
    this.sockets = new Map();
    this.frameSeen = -1;
    this.pendingFlash = -1;
    this.dirty = true;
    this.canvas = opts.canvas || document.createElement('canvas');
    this.canvas.width = UI_W;
    this.canvas.height = UI_H;
    this.ctx = this.canvas.getContext('2d');
    this.ctx.fillStyle = '#e9e6dd';
    this.ctx.fillRect(0, 0, UI_W, UI_H);
    this.fwPartial = '';
    this.syncTimer = null;
  }

  start() {
    const o = this.opts;
    const idb = o.storage === 'idb';
    const host = {
      log: (t) => this.firmwareLog(t),
      framePresented: (mode) => {
        if (mode === 0 || mode === 1) this.pendingFlash = mode;
        this.dirty = true;
      },
      storageChanged: () => idb && this.persistSoon(),
      restart: () => location.reload(),
      wsOpen: (id, url) => this.wsOpen(id, url),
      wsSend: (id, text) => this.wsSend(id, text),
      wsClose: (id) => this.wsClose(id),
      mdnsAnswer: () => {
        if (o.mdns) return o.mdns();
        const c = o.config || {host: 'demo', port: 7878};
        return `${c.host}:${c.port}:${c.host}`;
      },
    };
    return window.createSim({
      simHost: host,
      print: (t) => this.firmwareLog(t + '\n'),
      printErr: (t) => this.log('[sim] ' + t),
      preRun: [(mod) => {
        this.Module = mod;
        const FS = mod.FS;
        FS.mkdir(SD);
        if (!idb) {
          if (o.config) this.writeConfig(o.config);
          return;
        }
        FS.mount(mod.IDBFS, {}, SD);
        mod.addRunDependency('idbfs');
        FS.syncfs(true, (err) => {
          if (err) this.log('IndexedDB unavailable; storage resets on reload: ' + err);
          if (o.config) this.writeConfig(o.config);
          mod.removeRunDependency('idbfs');
        });
      }],
    }).then((mod) => {
      this.Module = mod;
      this.ready = true;
      window.simModule = mod;
      return mod;
    });
  }

  log(line) { if (this.opts.onLog) this.opts.onLog(line); }

  firmwareLog(text) {
    this.fwPartial += text;
    let nl;
    while ((nl = this.fwPartial.indexOf('\n')) >= 0) {
      const line = this.fwPartial.slice(0, nl);
      this.fwPartial = this.fwPartial.slice(nl + 1);
      if (line) this.log(line);
    }
  }

  // ---- storage ------------------------------------------------------------------
  // Plain "token": the firmware re-saves it obfuscated, as if hand-written.
  writeConfig(cfg) {
    const FS = this.Module.FS;
    try { FS.mkdir(SD + '/.crosspoint'); } catch (e) { /* exists */ }
    FS.writeFile(CONFIG, JSON.stringify({host: cfg.host, port: cfg.port, token: cfg.token}));
    if (this.opts.storage === 'idb') this.persistSoon();
  }

  persistSoon() {
    if (this.syncTimer) return;
    this.syncTimer = setTimeout(() => {
      this.syncTimer = null;
      this.Module.FS.syncfs(false, (err) => err && this.log('IndexedDB sync failed: ' + err));
    }, 400);
  }

  // Wipes the SD card (and its IndexedDB copy). Resolves when persisted.
  resetStorage() {
    const FS = this.Module && this.Module.FS;
    if (!FS) return Promise.resolve();
    const rm = (path) => {
      for (const name of FS.readdir(path)) {
        if (name === '.' || name === '..') continue;
        const p = path + '/' + name;
        if (FS.isDir(FS.stat(p).mode)) { rm(p); FS.rmdir(p); } else FS.unlink(p);
      }
    };
    rm(SD);
    if (this.opts.storage !== 'idb') return Promise.resolve();
    return new Promise((resolve) => FS.syncfs(false, () => resolve()));
  }

  // Re-reads the config / mDNS answer and re-enters the app's start screen.
  restartApp() { if (this.ready) this.Module._sim_restart_app(); }

  // ---- sockets (called from C++) ------------------------------------------------
  wsOpen(id, url) {
    const o = this.opts;
    let ws;
    o.onLink && o.onLink('warn', 'connecting');
    this.log('[page] connect ' + url);
    try {
      ws = o.openSocket(url);
    } catch (e) {
      o.onLink && o.onLink('err', 'cannot open ' + url);
      setTimeout(() => this.Module._sim_ws_closed(id), 0);
      return;
    }
    this.sockets.set(id, ws);
    ws.onopen = () => {
      o.onLink && o.onLink('ok', 'connected');
      this.Module._sim_ws_opened(id);
    };
    ws.onmessage = (ev) => {
      if (typeof ev.data !== 'string') return;
      o.onFrame && o.onFrame('rx', ev.data);
      const M = this.Module;
      const len = M.lengthBytesUTF8(ev.data);
      const ptr = M._malloc(len + 1);
      M.stringToUTF8(ev.data, ptr, len + 1);
      M._sim_ws_message(id, ptr, len);
      M._free(ptr);
    };
    ws.onerror = () => {};
    ws.onclose = () => {
      if (!this.sockets.has(id)) return;
      this.sockets.delete(id);
      o.onLink && o.onLink('err', 'disconnected, retrying');
      this.Module._sim_ws_closed(id);
    };
  }

  wsSend(id, text) {
    const ws = this.sockets.get(id);
    if (!ws || ws.readyState !== 1) return false;
    ws.send(text);
    this.opts.onFrame && this.opts.onFrame('tx', text);
    return true;
  }

  wsClose(id) {
    const ws = this.sockets.get(id);
    this.sockets.delete(id);
    if (!ws) return;
    ws.onclose = null;
    try { ws.close(); } catch (e) { /* already closed */ }
  }

  // ---- input --------------------------------------------------------------------
  key(k, down) { if (this.ready) this.Module._sim_key(down ? 1 : 0, k); }
  // phase 0 = down, 1 = move, 2 = up; x/y in framebuffer (portrait UI) pixels.
  tap(phase, x, y) {
    if (!this.ready) return;
    x = Math.max(0, Math.min(UI_W - 1, Math.floor(x)));
    y = Math.max(0, Math.min(UI_H - 1, Math.floor(y)));
    this.Module._sim_touch(phase, x, y);
  }

  // ---- framebuffer --------------------------------------------------------------
  // Copies the panel into the canvas when the firmware presented a new frame.
  // Returns {changed, flash}: flash 0 = FULL, 1 = HALF refresh, -1 = none.
  poll() {
    const M = this.Module;
    if (!this.ready) return {changed: false, flash: -1};
    const count = M._sim_frame_count();
    if (count === this.frameSeen && !this.dirty) return {changed: false, flash: -1};
    this.frameSeen = count;
    this.dirty = false;
    const ptr = M._sim_frame_rgba();
    const px = new Uint8ClampedArray(M.HEAPU8.buffer, ptr, UI_W * UI_H * 4);
    this.ctx.putImageData(new ImageData(px.slice(), UI_W, UI_H), 0, 0);
    const flash = this.pendingFlash;
    this.pendingFlash = -1;
    return {changed: true, flash};
  }
}

// "ws://host:port[/...]" | "host[:port]" -> {host, port}
export function parseBridge(text, fallbackPort = 7878) {
  const v = String(text || '').trim().replace(/^wss?:\/\//i, '').replace(/^https?:\/\//i, '').split('/')[0];
  const m = v.match(/^\[?([^\]]+?)\]?(?::(\d+))?$/);
  return {host: m ? m[1] : v, port: m && m[2] ? Number(m[2]) : fallbackPort};
}
