# CrossPoint simulator (Agent Mux)

Runs the firmware's real UI code for the Agent Mux app on a desktop or in a browser, so UI
changes can be checked without flashing the Metalio E-Ink 4. The page shows the 480x800 portrait
UI exactly as the firmware draws it into the 800x480 SSD1677 framebuffer.

## Quick start

```sh
sim/build-native.sh        # host build + scripted scenario -> PNG snapshots in sim/out/
sim/build-wasm.sh          # WebAssembly build -> sim/dist/
python3 -m http.server -d sim/dist 8000   # open http://127.0.0.1:8000/
sim/deploy.sh              # publish sim/dist to gh-pages on the `fork` remote
```

Requirements: CMake 3.16+, a C++20 compiler and Python 3 for the native build. The wasm build
also needs Emscripten. `build-wasm.sh` uses `emcc` from `PATH`, or sources `$EMSDK_ENV`, or
`../.emsdk/emsdk_env.sh` next to this checkout. On macOS, if the Command Line Tools SDK cannot
link ("tapi error: malformed file"), `build-native.sh` falls back to a working SDK.

## Using the page

- Click or tap the screen to touch. Drags become swipes.
- Device keys sit around the screen like on the Metalio: orange HOME, the two grey pills
  (left = `BTN_DOWN`, right = `BTN_UP`, as on the hardware), and on the right edge BOOT
  (Confirm), Volume +/- (Up/Down) and POWER.
- Keyboard: `Enter` Confirm, `↑`/`↓` Up/Down, `←`/`→` front Left/Right (the Metalio has no
  such keys; in a session they send Esc/Enter), `Esc`/`Backspace` Back (also virtual: on the
  device Back is the header arrow), `H` Home, `P` Power.
- **Bridge**: `demo` (default) runs a mock bridge inside the page (`web/demo-bridge.js`). It has
  a blocked Claude session with a 3-option Bash permission prompt, a Codex session that is
  building, and an idle session. It accepts any token except `bad`, which tests token rejection.
  For the real bridge run `agentmux daemon` and use `ws://127.0.0.1:7878/device` with the token
  from `agentmux token`. Chrome and Firefox allow `ws://127.0.0.1` from an https page because
  localhost is a secure context. Safari blocks it, so serve the page from http://localhost there.
- **Skip first-run setup**: when checked, the page writes `/.crosspoint/agentmux.json` (host,
  port, plain `token`, which the firmware re-saves obfuscated) before boot, and again on
  *Apply*. Uncheck it and press *Reset storage* to go through the device's own setup instead:
  mDNS discovery (the simulated mDNS answers with the bridge field) and the token keyboard.
- The simulated SD card is stored in IndexedDB, so it survives reloads. *Reset storage* wipes it.
- FULL and HALF refreshes flash the glass briefly. FAST refreshes do not.
- *Log · frames* shows every frame sent and received, plus the firmware log.
- URL parameters: `?bridge=ws://127.0.0.1:7878/device&token=abcd1234&seed=0`.

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
                                          web/     index.html, app.js, demo-bridge.js
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
