#!/bin/sh
# Publishes sim/dist to the gh-pages branch of the `fork` remote
# (served at https://<user>.github.io/crosspoint-reader/).
#   sim/build-wasm.sh && sim/deploy.sh
#   REMOTE=fork BRANCH=gh-pages sim/deploy.sh
# Uses a temporary worktree, so the current checkout is untouched.
set -e
SIM_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO="$(git -C "$SIM_DIR" rev-parse --show-toplevel)"
REMOTE="${REMOTE:-fork}"
BRANCH="${BRANCH:-gh-pages}"
DIST="$SIM_DIR/dist"

[ -f "$DIST/index.html" ] && [ -f "$DIST/sim.wasm" ] || { echo "Run sim/build-wasm.sh first" >&2; exit 1; }
[ ! -f "$DIST/.sim-dev" ] || { echo "sim/dist is a dev-loop build; run sim/build-wasm.sh first" >&2; exit 1; }
git -C "$REPO" remote get-url "$REMOTE" >/dev/null || { echo "No remote '$REMOTE'" >&2; exit 1; }

SRC_REV="$(git -C "$REPO" rev-parse --short HEAD)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/crosspoint-sim-pages.XXXXXX")"
cleanup() { git -C "$REPO" worktree remove --force "$WORK" >/dev/null 2>&1 || true; rm -rf "$WORK"; }
trap cleanup EXIT

if git -C "$REPO" fetch "$REMOTE" "$BRANCH" 2>/dev/null; then
  git -C "$REPO" worktree add --detach "$WORK" "$REMOTE/$BRANCH" >/dev/null
else
  # First deploy: start an orphan branch with no files.
  git -C "$REPO" worktree add --detach "$WORK" HEAD >/dev/null
  git -C "$WORK" checkout --orphan "$BRANCH-deploy" >/dev/null
  git -C "$WORK" rm -rf --quiet . >/dev/null 2>&1 || true
fi

find "$WORK" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -R "$DIST"/. "$WORK"/
touch "$WORK/.nojekyll"

git -C "$WORK" add -A
if git -C "$WORK" diff --cached --quiet; then
  echo "[deploy] nothing changed"
  exit 0
fi
git -C "$WORK" commit -q -m "sim: deploy from $SRC_REV"
git -C "$WORK" push "$REMOTE" "HEAD:refs/heads/$BRANCH"
echo "[deploy] pushed $BRANCH to $REMOTE (from $SRC_REV)"
