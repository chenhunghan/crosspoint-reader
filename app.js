// CrossPoint simulator page: hosts the firmware compiled to WebAssembly
// (sim.js / sim.wasm), draws its panel on the canvas, feeds keys and touches,
// bridges its WebSocket to a real bridge or the in-page demo bridge, and
// keeps the simulated SD card in IndexedDB.
"use strict";

(function () {
  const W = 480;
  const H = 800;
  const SD = "/sd";
  const CONFIG = SD + "/.crosspoint/agentmux.json";
  const PREFS_KEY = "crosspoint-sim-prefs";
  const KEY = { Back: 0, Confirm: 1, Left: 2, Right: 3, Up: 4, Down: 5, Power: 6, Home: 7 };
  const REFRESH = ["FULL", "HALF", "FAST"];

  const $ = (id) => document.getElementById(id);
  const canvas = $("screen");
  const ctx = canvas.getContext("2d");
  const logEl = $("log");

  let Module = null;
  let frameSeen = 0;
  let needsDraw = false;
  let pendingFlash = -1;
  const sockets = new Map();

  // ---- prefs (per-viewer convenience) -------------------------------------
  function loadPrefs() {
    const prefs = { bridge: "demo", token: "demo1234", seed: true };
    try {
      Object.assign(prefs, JSON.parse(localStorage.getItem(PREFS_KEY) || "{}"));
    } catch (e) {}
    const q = new URLSearchParams(location.search);
    if (q.get("bridge")) prefs.bridge = q.get("bridge");
    if (q.get("token")) prefs.token = q.get("token");
    if (q.get("seed")) prefs.seed = q.get("seed") !== "0";
    return prefs;
  }
  function savePrefs(prefs) {
    try {
      localStorage.setItem(PREFS_KEY, JSON.stringify(prefs));
    } catch (e) {}
  }
  let prefs = loadPrefs();
  $("bridge").value = prefs.bridge;
  $("token").value = prefs.token;
  $("seed").checked = prefs.seed;

  // "demo" | "host[:port]" | "ws://host:port/path" -> {host, port}
  function parseBridge(text) {
    let v = String(text || "").trim() || "demo";
    if (v.toLowerCase() === "demo") return { host: "demo", port: 7878 };
    v = v.replace(/^wss?:\/\//i, "").replace(/^https?:\/\//i, "");
    v = v.split("/")[0];
    const m = v.match(/^\[?([^\]]+?)\]?(?::(\d+))?$/);
    return { host: m ? m[1] : v, port: m && m[2] ? Number(m[2]) : 7878 };
  }

  // ---- log ------------------------------------------------------------------
  const MAX_LOG = 600;
  function log(text, cls) {
    if (cls === "fw" && !$("showFw").checked) return;
    const line = document.createElement("div");
    line.className = cls || "sys";
    line.textContent = text;
    const stick = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 8;
    logEl.appendChild(line);
    while (logEl.childNodes.length > MAX_LOG) logEl.removeChild(logEl.firstChild);
    if (stick) logEl.scrollTop = logEl.scrollHeight;
  }
  let fwPartial = "";
  function firmwareLog(text) {
    fwPartial += text;
    let nl;
    while ((nl = fwPartial.indexOf("\n")) >= 0) {
      const line = fwPartial.slice(0, nl);
      fwPartial = fwPartial.slice(nl + 1);
      if (line) {
        console.log("[fw] " + line);
        log(line, line.includes("[ERR]") ? "err" : "fw");
      }
    }
  }
  $("clearLog").onclick = () => (logEl.textContent = "");

  function setLink(state, text) {
    $("linkDot").className = "dot " + state;
    $("linkText").textContent = text;
  }

  // ---- storage --------------------------------------------------------------
  let syncTimer = null;
  function persistSoon() {
    if (syncTimer) return;
    syncTimer = setTimeout(() => {
      syncTimer = null;
      Module.FS.syncfs(false, (err) => err && log("IndexedDB sync failed: " + err, "err"));
    }, 400);
  }

  function writeConfig(FS) {
    const b = parseBridge(prefs.bridge);
    try {
      FS.mkdir(SD + "/.crosspoint");
    } catch (e) {}
    // Plain "token" is what a user would hand-write; the firmware re-saves it obfuscated.
    FS.writeFile(CONFIG, JSON.stringify({ host: b.host, port: b.port, token: prefs.token }));
  }

  function removeTree(FS, path) {
    for (const name of FS.readdir(path)) {
      if (name === "." || name === "..") continue;
      const p = path + "/" + name;
      if (FS.isDir(FS.stat(p).mode)) {
        removeTree(FS, p);
        FS.rmdir(p);
      } else {
        FS.unlink(p);
      }
    }
  }

  // ---- sockets (called from C++ via Module.simHost) ------------------------
  function toWasm(text) {
    const len = Module.lengthBytesUTF8(text);
    const ptr = Module._malloc(len + 1);
    Module.stringToUTF8(text, ptr, len + 1);
    return { ptr, len };
  }

  function wsOpen(id, url) {
    let host = "";
    try {
      host = new URL(url).hostname;
    } catch (e) {}
    const demo = host.toLowerCase() === "demo";
    log("connect " + url + (demo ? "  (in-page demo bridge)" : ""), "sys");
    setLink("warn", "Connecting to " + (demo ? "demo bridge" : url));
    let ws;
    try {
      ws = demo ? new window.DemoBridgeSocket(url) : new WebSocket(url);
    } catch (e) {
      log("WebSocket failed: " + e.message, "err");
      setLink("err", "Cannot open " + url);
      setTimeout(() => Module._sim_ws_closed(id), 0);
      return;
    }
    sockets.set(id, ws);
    ws.onopen = () => {
      setLink("ok", "Connected: " + (demo ? "demo bridge" : url));
      Module._sim_ws_opened(id);
    };
    ws.onmessage = (ev) => {
      if (typeof ev.data !== "string") return;
      log("↓ " + ev.data, "rx");
      const { ptr, len } = toWasm(ev.data);
      Module._sim_ws_message(id, ptr, len);
      Module._free(ptr);
    };
    ws.onerror = () => log("socket error (" + url + ")", "err");
    ws.onclose = (ev) => {
      if (!sockets.has(id)) return;
      sockets.delete(id);
      log("closed " + url + (ev && ev.code ? " (" + ev.code + ")" : ""), "sys");
      setLink("err", "Disconnected - the firmware retries with backoff");
      Module._sim_ws_closed(id);
    };
  }

  function wsSend(id, text) {
    const ws = sockets.get(id);
    if (!ws || ws.readyState !== 1) return false;
    log("↑ " + text.replace(/"token":"[^"]*"/, '"token":"***"'), "tx");
    ws.send(text);
    return true;
  }

  function wsClose(id) {
    const ws = sockets.get(id);
    sockets.delete(id);
    if (ws) {
      ws.onclose = null;
      try {
        ws.close();
      } catch (e) {}
    }
  }

  // ---- drawing -------------------------------------------------------------
  function draw() {
    const ptr = Module._sim_frame_rgba();
    const pixels = new Uint8ClampedArray(Module.HEAPU8.buffer, ptr, W * H * 4);
    ctx.putImageData(new ImageData(pixels.slice(), W, H), 0, 0);
  }

  function flash(mode) {
    const cls = mode === 0 ? "flash-full" : "flash-half";
    canvas.classList.remove("flash-full", "flash-half");
    void canvas.offsetWidth; // restart the animation
    canvas.classList.add(cls);
  }

  function frameLoop() {
    if (Module) {
      const count = Module._sim_frame_count();
      if (count !== frameSeen || needsDraw) {
        frameSeen = count;
        needsDraw = false;
        draw();
        if (pendingFlash === 0 || pendingFlash === 1) flash(pendingFlash);
        pendingFlash = -1;
      }
    }
    requestAnimationFrame(frameLoop);
  }

  // ---- input ----------------------------------------------------------------
  function uiPoint(ev) {
    const r = canvas.getBoundingClientRect();
    const x = Math.round(((ev.clientX - r.left) * W) / r.width);
    const y = Math.round(((ev.clientY - r.top) * H) / r.height);
    return [Math.max(0, Math.min(W - 1, x)), Math.max(0, Math.min(H - 1, y))];
  }
  let touching = false;
  canvas.addEventListener("pointerdown", (ev) => {
    if (!Module) return;
    canvas.setPointerCapture(ev.pointerId);
    touching = true;
    const [x, y] = uiPoint(ev);
    Module._sim_touch(0, x, y);
    ev.preventDefault();
  });
  canvas.addEventListener("pointermove", (ev) => {
    if (!touching) return;
    const [x, y] = uiPoint(ev);
    Module._sim_touch(1, x, y);
  });
  const endTouch = (ev) => {
    if (!touching) return;
    touching = false;
    const [x, y] = uiPoint(ev);
    Module._sim_touch(2, x, y);
  };
  canvas.addEventListener("pointerup", endTouch);
  canvas.addEventListener("pointercancel", endTouch);

  function keyDown(k) {
    if (Module) Module._sim_key(1, k);
  }
  function keyUp(k) {
    if (Module) Module._sim_key(0, k);
  }

  for (const btn of document.querySelectorAll("button.hw[data-key]")) {
    const k = Number(btn.dataset.key);
    let down = false;
    btn.addEventListener("pointerdown", (ev) => {
      btn.setPointerCapture(ev.pointerId);
      down = true;
      btn.classList.add("pressed");
      keyDown(k);
      ev.preventDefault();
    });
    const up = () => {
      if (!down) return;
      down = false;
      btn.classList.remove("pressed");
      keyUp(k);
    };
    btn.addEventListener("pointerup", up);
    btn.addEventListener("pointercancel", up);
    btn.addEventListener("lostpointercapture", up);
  }
  $("back").addEventListener("click", () => {
    keyDown(KEY.Back);
    setTimeout(() => keyUp(KEY.Back), 60);
  });

  const KEYMAP = {
    Escape: KEY.Back,
    Backspace: KEY.Back,
    Enter: KEY.Confirm,
    ArrowUp: KEY.Up,
    ArrowDown: KEY.Down,
    ArrowLeft: KEY.Left,
    ArrowRight: KEY.Right,
    h: KEY.Home,
    H: KEY.Home,
    p: KEY.Power,
    P: KEY.Power,
  };
  const typing = (ev) => ev.target && (ev.target.tagName === "INPUT" || ev.target.tagName === "TEXTAREA");
  window.addEventListener("keydown", (ev) => {
    if (typing(ev) || ev.metaKey || ev.ctrlKey || ev.altKey) return;
    const k = KEYMAP[ev.key];
    if (k === undefined) return;
    ev.preventDefault();
    if (!ev.repeat) keyDown(k);
  });
  window.addEventListener("keyup", (ev) => {
    if (typing(ev)) return;
    const k = KEYMAP[ev.key];
    if (k !== undefined) keyUp(k);
  });

  // ---- settings -------------------------------------------------------------
  function readForm() {
    prefs = { bridge: $("bridge").value.trim() || "demo", token: $("token").value.trim(), seed: $("seed").checked };
    savePrefs(prefs);
  }
  $("apply").addEventListener("click", () => {
    readForm();
    if (!Module) return;
    if (prefs.seed) writeConfig(Module.FS);
    persistSoon();
    log("applied bridge=" + prefs.bridge + (prefs.seed ? " (config written)" : ""), "sys");
    Module._sim_restart_app();
  });
  $("reset").addEventListener("click", () => {
    readForm();
    if (!Module) return;
    removeTree(Module.FS, SD);
    if (window.DemoBridgeSocket) window.DemoBridgeSocket.reset();
    Module.FS.syncfs(false, () => location.reload());
  });
  $("seed").addEventListener("change", readForm);

  // ---- boot -----------------------------------------------------------------
  window.startSimulator = function () {
    const host = {
      log: firmwareLog,
      framePresented(mode) {
        if (mode === 0 || mode === 1) pendingFlash = mode;
        needsDraw = true;
      },
      storageChanged: persistSoon,
      restart: () => location.reload(),
      wsOpen,
      wsSend,
      wsClose,
      mdnsAnswer() {
        const b = parseBridge(prefs.bridge);
        return b.host + ":" + b.port + ":" + (b.host === "demo" ? "demo-bridge" : b.host);
      },
    };
    window
      .createSim({
        simHost: host,
        print: (t) => firmwareLog(t + "\n"),
        printErr: (t) => {
          console.warn(t);
          log(t, "err");
        },
        preRun: [
          (mod) => {
            Module = mod;
            const FS = mod.FS;
            FS.mkdir(SD);
            FS.mount(mod.IDBFS, {}, SD);
            mod.addRunDependency("idbfs");
            FS.syncfs(true, (err) => {
              if (err) log("IndexedDB unavailable, storage resets on reload: " + err, "err");
              let exists = true;
              try {
                FS.stat(CONFIG);
              } catch (e) {
                exists = false;
              }
              if (prefs.seed && !exists) {
                writeConfig(FS);
                log("seeded " + CONFIG.slice(SD.length) + " for " + prefs.bridge, "sys");
              }
              mod.removeRunDependency("idbfs");
            });
          },
        ],
      })
      .then((mod) => {
        Module = mod;
        window.simModule = mod; // for debugging / automation
        $("loading").hidden = true;
        log("firmware started (" + REFRESH.join("/") + " refreshes flash the glass)", "sys");
      })
      .catch((e) => {
        $("loading").textContent = "Failed to load: " + e;
        log(String(e), "err");
      });
    requestAnimationFrame(frameLoop);
  };
})();
