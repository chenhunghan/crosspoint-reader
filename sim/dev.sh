#!/bin/sh
# Fast UI iteration: builds the wasm simulator once (fast-link SIM_DEV build),
# serves sim/dist, rebuilds incrementally on every save under src/, lib/, sim/
# and reloads the open pages (http://127.0.0.1:8931/ by default).
#   sim/dev.sh
#   PORT=9000 HOST=0.0.0.0 sim/dev.sh
# Before sim/deploy.sh, run sim/build-wasm.sh for an optimised build.
set -e
SIM_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

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

mkdir -p "$SIM_DIR/build"
log="$SIM_DIR/build/dev-initial.log"
echo "[dev] building the wasm simulator (SIM_DEV fast link)..."
if ! SIM_DEV=1 "$SIM_DIR/build-wasm.sh" >"$log" 2>&1; then
  grep -iE -B1 -A4 "error|fatal" "$log" || tail -40 "$log"
  echo "[dev] initial build failed (full log: $log)" >&2
  exit 1
fi
exec python3 "$SIM_DIR/scripts/devserver.py" --port "${PORT:-8931}" --host "${HOST:-127.0.0.1}"
