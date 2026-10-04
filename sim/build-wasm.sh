#!/bin/sh
# Builds the WebAssembly simulator into sim/dist/: sim.js + sim.wasm, the device
# page (index.html), the 3D stage (stage.html) and their modules.
#   sim/build-wasm.sh
#   EMSDK_ENV=/path/to/emsdk_env.sh sim/build-wasm.sh
#   SIM_DEV=1 sim/build-wasm.sh     # fast, larger link (used by sim/dev.sh)
# Serve with: python3 -m http.server -d sim/dist 8000, or use sim/dev.sh.
set -e
SIM_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$SIM_DIR/build/wasm"
GEN="$SIM_DIR/build/gen"
DIST="$SIM_DIR/dist"

if ! command -v emcc >/dev/null 2>&1; then
  env_sh="${EMSDK_ENV:-}"
  if [ -z "$env_sh" ]; then
    for candidate in "$SIM_DIR/../../.emsdk/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh"; do
      [ -f "$candidate" ] && env_sh="$candidate" && break
    done
  fi
  [ -n "$env_sh" ] || { echo "emcc not found: install emsdk or set EMSDK_ENV" >&2; exit 1; }
  # shellcheck disable=SC1090
  . "$env_sh" >/dev/null 2>&1
fi

"$SIM_DIR/scripts/prepare.sh" "$GEN"
emcmake cmake -S "$SIM_DIR" -B "$BUILD" -DSIM_GEN_DIR="$GEN" -DCMAKE_BUILD_TYPE=Release \
  -DSIM_DEV="$([ "${SIM_DEV:-0}" = 1 ] && echo ON || echo OFF)" >/dev/null
cmake --build "$BUILD" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

rm -rf "$DIST"
mkdir -p "$DIST"
cp "$BUILD/sim.js" "$BUILD/sim.wasm" "$DIST/"
cp -R "$SIM_DIR"/web/. "$DIST/"
GIT_REV="$(git -C "$SIM_DIR" rev-parse --short HEAD 2>/dev/null || echo dev)"
for page in index.html stage.html; do
  sed -i.bak "s/__SIM_BUILD__/$GIT_REV/g" "$DIST/$page" && rm -f "$DIST/$page.bak"
done
# Dev-loop links are larger and unoptimised; deploy.sh refuses them.
[ "${SIM_DEV:-0}" = 1 ] && echo "SIM_DEV=1 build (not for deploy)" >"$DIST/.sim-dev"
echo "[sim] dist ready:"
ls -l "$DIST"
