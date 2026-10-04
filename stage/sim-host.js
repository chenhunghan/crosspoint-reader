// Boots the firmware simulator (sim.js / sim.wasm, loaded as a classic script
// that defines window.createSim) for the 3D stage. Storage is a fresh MEMFS SD
// card with the Agent Mux config pre-written, so the device skips first-run
// setup and connects straight away. Sockets go to `openSocket(url)`, which
// returns a real WebSocket or an in-page bridge with the same surface.
export const UI_W = 480;
export const UI_H = 800;
export const KEY = {Back: 0, Confirm: 1, Left: 2, Right: 3, Up: 4, Down: 5, Power: 6, Home: 7};

const SD = '/sd';
const CONFIG = SD + '/.crosspoint/agentmux.json';

export class SimHost {
  // opts: {host, port, token, openSocket(url) -> socket, onLink(state, text), onLog(line), onFrameSent(text)}
  constructor(opts) {
    this.opts = opts;
    this.Module = null;
    this.sockets = new Map();
    this.frameSeen = -1;
    this.pendingFlash = -1;
    this.dirty = true;
    this.canvas = document.createElement('canvas');
    this.canvas.width = UI_W;
    this.canvas.height = UI_H;
    this.ctx = this.canvas.getContext('2d');
    this.ctx.fillStyle = '#e9e6dd';
    this.ctx.fillRect(0, 0, UI_W, UI_H);
    this.fwPartial = '';
  }

  start() {
    const o = this.opts;
    const host = {
      log: (t) => this.firmwareLog(t),
      framePresented: (mode) => {
        if (mode === 0 || mode === 1) this.pendingFlash = mode;
        this.dirty = true;
      },
      storageChanged: () => {},
      restart: () => location.reload(),
      wsOpen: (id, url) => this.wsOpen(id, url),
      wsSend: (id, text) => this.wsSend(id, text),
      wsClose: (id) => this.wsClose(id),
      mdnsAnswer: () => `${o.host}:${o.port}:${o.host}`,
    };
    return window.createSim({
      simHost: host,
      print: (t) => this.firmwareLog(t + '\n'),
      printErr: (t) => console.warn('[sim]', t),
      preRun: [(mod) => {
        const FS = mod.FS;
        FS.mkdir(SD);
        FS.mkdir(SD + '/.crosspoint');
        // Plain "token": the firmware re-saves it obfuscated (same as dev.html's "skip first-run setup").
        FS.writeFile(CONFIG, JSON.stringify({host: o.host, port: o.port, token: o.token}));
      }],
    }).then((mod) => {
      this.Module = mod;
      window.simModule = mod;
      return mod;
    });
  }

  firmwareLog(text) {
    this.fwPartial += text;
    let nl;
    while ((nl = this.fwPartial.indexOf('\n')) >= 0) {
      const line = this.fwPartial.slice(0, nl);
      this.fwPartial = this.fwPartial.slice(nl + 1);
      if (line && this.opts.onLog) this.opts.onLog(line);
    }
  }

  // ---- sockets (called from C++) ------------------------------------------------
  wsOpen(id, url) {
    const o = this.opts;
    let ws;
    o.onLink && o.onLink('warn', 'connecting');
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
    this.opts.onFrameSent && this.opts.onFrameSent(text);
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
  key(k, down) { if (this.Module) this.Module._sim_key(down ? 1 : 0, k); }
  tap(phase, x, y) {
    if (!this.Module) return;
    x = Math.max(0, Math.min(UI_W - 1, Math.round(x)));
    y = Math.max(0, Math.min(UI_H - 1, Math.round(y)));
    this.Module._sim_touch(phase, x, y);
  }

  // ---- framebuffer --------------------------------------------------------------
  // Copies the panel into the canvas when the firmware presented a new frame.
  // Returns {changed, flash}: flash 0 = FULL, 1 = HALF refresh, -1 = none.
  poll() {
    const M = this.Module;
    if (!M) return {changed: false, flash: -1};
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
  let v = String(text || '').trim().replace(/^wss?:\/\//i, '').replace(/^https?:\/\//i, '').split('/')[0];
  const m = v.match(/^\[?([^\]]+?)\]?(?::(\d+))?$/);
  return {host: m ? m[1] : v, port: m && m[2] ? Number(m[2]) : fallbackPort};
}
