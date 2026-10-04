#!/bin/sh
# Builds the native (host) simulator and runs its snapshot scenario.
#   sim/build-native.sh            build + run, PNGs land in sim/out/
#   sim/build-native.sh --no-run   build only
set -e
SIM_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILD="$SIM_DIR/build/native"
GEN="$SIM_DIR/build/gen"

"$SIM_DIR/scripts/prepare.sh" "$GEN"

# macOS: some Command Line Tools releases ship an SDK the bundled linker
# cannot read ("tapi error: malformed file"). Fall back to the first SDK that
# links a trivial program.
if [ "$(uname -s)" = "Darwin" ] && [ -z "$SDKROOT" ]; then
  probe="$BUILD/sdk-probe"
  mkdir -p "$BUILD"
  echo 'int main(){return 0;}' > "$probe.c"
  if ! cc "$probe.c" -o "$probe" 2>/dev/null; then
    for sdk in "$(xcrun --sdk macosx --show-sdk-path 2>/dev/null)" \
               $(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX[0-9]*.sdk 2>/dev/null | sort -r); do
      [ -d "$sdk" ] || continue
      if SDKROOT="$sdk" cc "$probe.c" -o "$probe" 2>/dev/null; then
        export SDKROOT="$sdk"
        echo "[sim] using SDKROOT=$SDKROOT"
        break
      fi
    done
  fi
fi
cmake -S "$SIM_DIR" -B "$BUILD" -DSIM_GEN_DIR="$GEN" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

if [ "$1" != "--no-run" ]; then
  mkdir -p "$SIM_DIR/out"
  "$BUILD/sim" "$SIM_DIR/out"
fi
