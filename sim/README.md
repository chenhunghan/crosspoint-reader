# CrossPoint simulator (Agent Mux)

Runs the firmware's real UI code for the Agent Mux app on a desktop or in a browser, so UI
changes can be checked without flashing the Metalio E-Ink 4. The page shows the 480x800 portrait
UI exactly as the firmware draws it into the 800x480 SSD1677 framebuffer.

## Quick start

```sh
sim/dev.sh                 # build once, serve http://127.0.0.1:8931/, rebuild + reload on save
sim/build-native.sh        # host build + scripted scenario -> PNG snapshots in sim/out/
sim/build-wasm.sh          # optimised WebAssembly build -> sim/dist/
sim/deploy.sh              # publish sim/dist to gh-pages on the `fork` remote
```

`sim/dist` holds two pages that share `web/core/` (wasm boot, input, bridges):
`index.html`, the device page below (the default, also on GitHub Pages), and `stage.html`,
the 3D stage. `dev.html` redirects to `index.html`.

## Dev loop (`sim/dev.sh`)

Builds the wasm once with `SIM_DEV=1` (same objects, `-O1` link instead of `-Oz`), serves
`sim/dist` on `PORT` (default 8931; `HOST` default 127.0.0.1) and polls `src/`, `lib/`, `sim/`
and the FreeInk UI/font/board headers (Python, no `fswatch` needed). On a save:

- C/C++/YAML change: `make sim/fast` in `sim/build/wasm` (a `CMakeLists.txt` change or an
  added/removed file runs `cmake --build`; `lib/I18n` changes re-run `scripts/prepare.sh`),
  then the open pages reload.
- `sim/web/` change: the files are copied into `sim/dist`, then the pages reload.
- Build error: the compiler errors print in the terminal and in an overlay on the page; the
  page keeps running the last good firmware.

A reload keeps the zoom, source and panel (localStorage), reopens the session that was open,
and puts a replay back at the same point. The terminal prints the edit-to-screen time; a
one-line string change in `src/agentmux/SessionActivity.cpp` takes about 9 s here (detect
0.0-0.2 s, compile ~4 s + link ~4 s, reload and boot 0.3 s). Most of the build time is process
start-up on this Mac, not compilation. Run `sim/build-wasm.sh` before `sim/deploy.sh`;
`deploy.sh` refuses a dev build.

## Device page (`index.html`)

A front view of the Metalio E-Ink 4 in HTML/CSS with the firmware's 480x800 framebuffer in its
screen.

- **Exact pixels.** The canvas uses `image-rendering: pixelated` at a whole number of *device*
  pixels per framebuffer pixel: *1:1* (one display pixel each; on a Retina screen that is
  close to the real 235 ppi size), *2×*, *4×*, and *Fit* (largest whole scale that fits). The
  device is placed on whole device pixels, and the layout follows `devicePixelRatio` changes.
  `Z` cycles the zoom.
- **Touch.** Pointer down/move/up on the screen are touch down/move/up, so a click is a tap,
  holding 600 ms is a long press and a drag is a swipe.
- **Keys.** Orange HOME and the grey pills (left = `BTN_DOWN`, right = `BTN_UP`) under the
  screen; BOOT (Confirm), the volume rocker (Up/Down) and POWER (tap; hold ≥ 450 ms = sleep)
  on the right edge. Each key is held for as long as the pointer is down. Keyboard: `Enter`
  BOOT, `↑`/`↓` volume, `←`/`→` front Left/Right (in a session: Esc/Enter), `Esc`/`Backspace`
  Back (virtual; on the device it is the header arrow), `H` HOME, `P` POWER.
- **Content** (side panel; `?source=demo|replay|live`):
  - *Demo*: `web/core/demo-bridge.js`, an in-page bridge with a Claude session blocked on a
    3-option Bash prompt, a Codex session that is building and an idle session. Any token
    except `bad` is accepted.
  - *Replay*: `web/demo/claude-demo.cast` (or `?cast=URL`), a real `agentmux run --record`
    session of Claude Code 2.1.289, played by the replay bridge described under the 3D stage.
    Play/pause, restart, speed, *To prompt*; playback holds at the permission prompt until the
    device answers it. The recording's terminal is under *Terminal (recording)*.
  - *Live*: the bridge's `/device` (`ws://HOST:PORT` + token, also `?bridge=…&token=…`). When
    the page is served by `agentmux daemon --web sim/dist`, the bridge defaults to the page's
    host. Chrome and Firefox allow `ws://127.0.0.1` from an https page; Safari does not.
- **Device.** *Skip first-run setup* writes `/.crosspoint/agentmux.json` (host, port, plain
  `token`, which the firmware re-saves obfuscated) before every boot. Turn it off and press
  *Reset storage* to go through the device's own setup (mDNS answers with the selected
  bridge, then the token keyboard). The SD card is kept in IndexedDB. *Copy PNG* copies the
  framebuffer. FULL and HALF refreshes flash the glass briefly.
- The log shows frames sent and received and the firmware log. `window.__sim` has hooks for
  automation (`tap(x, y)`, `key(KEY.x)`, `setZoom()`, `clientPointFor(x, y)`, `state()`).

## 3D stage (`stage.html`)

A three.js scene with a laptop whose screen is a Claude Code terminal and a Metalio E-Ink 4 whose
screen is this simulator. Click or tap the e-ink screen to touch it; click the device keys (orange
HOME, grey pills, BOOT, volume rocker, POWER) to press them. The keyboard shortcuts are the same as
on the device page. Drag to orbit; *Overview*, *Laptop* and *Device* (`O`, `L`, `F`) move the camera.
Plain ES modules, no build step: three.js and `@xterm/headless` load from cdn.jsdelivr.net, and the
terminal is drawn cell by cell onto a canvas texture.

- **Demo** (default): replays `web/demo/claude-demo.cast` into the laptop terminal. An in-page
  replay bridge (`web/core/replay.js`) plays the bridge's side of the device protocol from the
  recording's markers and the terminal's screen text (`ok`, `sessions`, `status`, throttled
  `screen`, `perm`, `perm_closed`); its glyph mapping, rules and word wrap follow
  `bridge/src/render.rs`. Playback holds at the permission prompt until the device answers it;
  any option continues the recorded "Yes" path. Replies and keys typed on the device are not
  sent anywhere. `?cast=URL` plays another recording.
- **Live**: `stage.html?mode=live&bridge=ws://HOST:7878&token=TOKEN[&sid=s1]`. The laptop shows
  the session's `/term` stream and the device connects to the bridge's `/device`. Without `sid`
  the first session from `/monitor` is used; without `bridge` the page's own host is used, so
  the daemon can serve it: `agentmux daemon --web sim/dist`, then open
  `http://HOST:7878/stage.html?mode=live&token=TOKEN`.

The stage starts the device with a fresh in-memory SD card and the Agent Mux config pre-written.
`window.__stage` exposes hooks for automation (`tapDevice(x, y)`, `press(name)`,
`clientPointFor(x, y)`, `state()`, `screenText()`).

Requirements: CMake 3.16+, a C++20 compiler and Python 3 for the native build. The wasm build
also needs Emscripten. `build-wasm.sh` uses `emcc` from `PATH`, or sources `$EMSDK_ENV`, or
`../.emsdk/emsdk_env.sh` next to this checkout. On macOS, if the Command Line Tools SDK cannot
link ("tapi error: malformed file"), `build-native.sh` falls back to a working SDK.

## Architecture

```
firmware sources (unchanged)            sim/
 src/agentmux/*  Activity, UiList...      stubs/   header shadows: Arduino.h, WString, Print,
 KeyboardEntryActivity, UITheme+themes             FreeRTOS, WiFi, ESPmDNS, WebSocketsClient,
 MappedInputManager, ButtonNavigator,              EInkDisplay, InputManager, Logging, esp_*
 CrossPointSettings, PersistableStore     src/     SimApp (globals + setup like main.cpp),
 GfxRenderer, EpdFont, FreeInkUI, I18n,            SimActivityManager (synchronous render),
 UI fonts, Utf8, MiniBidi, ...                     SimHal{Display,GPIO,Storage}, SimWebSockets,
                                                   SimArduino, SimLinkStubs
                                          platform/native_*  scenario + fake bridge + PNG writer
                                          platform/wasm_main JS glue (EM_JS), rAF main loop
                                          web/     index.html + app.js (device page), stage.html +
                                                   stage/ (3D), core/ (shared: sim-host, demo-bridge,
                                                   replay, term-canvas, devloop), demo/ (recording)
                                          dev.sh, scripts/devserver.py  watch/rebuild/reload
```

- **Real code.** All of `src/agentmux/`, the activity framework, `UiListActivity`,
  `KeyboardEntryActivity`, `UiAppHost`, `UITheme` with all themes, `MappedInputManager`,
  `ButtonNavigator`, `CrossPointSettings`, `RecentBooksStore`, `PersistableStore` and
  obfuscation, `GfxRenderer`, `EpdFont`, `SdCardFont`, the font decompressor, `FreeInkUI`, the
  I18n tables (English only) and the built-in UI fonts (Ubuntu 10/12, Noto Sans 8). The board
  profile comes from the real `BoardConfig.h` with `FREEINK_DEVICE_METALIO_EINK4=1`.
- **Simulated.** The HAL is replaced at link level (same headers, sim `.cpp`):
  - `HalDisplay` keeps the framebuffer in memory.
  - `HalGPIO` takes injected key and touch events. Touch points are converted with the inverse
    of `GfxRenderer::tapToLogical`, so taps reach the UI in its own coordinates.
  - `HalStorage` uses host files under a root directory (`/sd` on IDBFS in the browser).
  - `ActivityManager` keeps the push/pop/replace and result-handler semantics, but renders
    synchronously at the end of each `loop()`, so there is no FreeRTOS render task.
    `goHome()` re-enters Agent Mux.
  - `WebSocketsClient` keeps the links2004 API, delivers events from `loop()` and reconnects on
    its interval. Its transport is JavaScript in the browser (a real `WebSocket` or the demo)
    and an in-process scripted bridge natively.
  - WiFi is always connected. mDNS answers from the page's settings. SD/TTF fonts, KOReader
    sync, RTC, IMU, power and restarts are inert stand-ins (`SimLinkStubs.cpp`).
- **Time.** In the browser `millis()` is real time, and `delay()` never blocks. The native
  scenario uses a virtual clock, so its snapshots are deterministic.

## Upstream-sync rule

Everything for the simulator lives in `sim/`. Firmware files are **not** edited for it:
no `#ifdef CROSSPOINT_SIM` seams exist today. Stubbing happens through include-path shadowing
(`sim/stubs` comes first) and link-level replacement (sim `.cpp` files implement real headers).
`CROSSPOINT_EMULATED=1` is an existing upstream knob; it drops `HalGPIO`'s `InputManager` member.
If an upstream change breaks the sim build, fix it inside `sim/`. Usually that means a missing
stub symbol or a new source file to add to `REAL_SOURCES` in `CMakeLists.txt`. If a seam in
firmware code ever becomes unavoidable, keep it to a tiny, commented `#if CROSSPOINT_SIM`.

## Native scenario

`sim/build-native.sh` runs `platform/native_main.cpp`, which goes through these steps:

1. Token keyboard (mDNS finds a bridge, no token yet).
2. Session list.
3. Permission dialog.
4. Second option selected.
5. Terminal after approving.
6. Reply keyboard.
7. List after Back.
8. A session opened by touch.

It also checks a few basic expectations and exits non-zero on failure. Use it as a quick
regression check, and look at the PNGs after UI changes.

## Known gaps

- Only Agent Mux and the screens it opens are compiled in. Home, Settings, the reader and
  WiFi selection are not, and HOME just restarts the app.
- WiFi selection is never shown, because WiFi is always connected.
- Physical-keyboard typing into the on-screen keyboard is not wired up. Tap the keys.
- Refresh timing and ghosting are not modelled. Only the flash on FULL/HALF is shown.
- The battery reads a constant 87%, and the clock is the host clock.
