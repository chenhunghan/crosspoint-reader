#!/usr/bin/env python3
"""Dev loop for the WebAssembly simulator (started by sim/dev.sh).

Serves sim/dist, polls the firmware and simulator sources for changes, rebuilds
the wasm incrementally, and tells open pages to reload:

  GET  /__dev/state    {"version", "building", "error", "built"}
  GET  /__dev/events   server-sent "state" events (same object)
  POST /__dev/loaded   the page reports that the new firmware drew its first frame

Changes under sim/web/ are copied into sim/dist/ without a rebuild. Standard
library only; polling, so it needs no fswatch/inotify.
"""
import argparse
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time

SIM = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(SIM)
DIST = os.path.join(SIM, "dist")
WEB = os.path.join(SIM, "web")
BUILD = os.path.join(SIM, "build", "wasm")
GEN = os.path.join(SIM, "build", "gen")

# (root, recursive). Everything the wasm build compiles or includes.
WATCH = [
    (os.path.join(REPO, "src"), True),
    (os.path.join(REPO, "lib"), True),
    (os.path.join(REPO, "freeink-sdk", "libs", "ui"), True),
    (os.path.join(REPO, "freeink-sdk", "libs", "font"), True),
    (os.path.join(REPO, "freeink-sdk", "libs", "hardware", "BoardConfig"), True),
    (SIM, True),
]
SKIP_DIRS = {os.path.join(SIM, d) for d in ("dist", "out", "build")} | {os.path.join(SIM, "scripts")}
SOURCE_EXT = (".cpp", ".c", ".cc", ".h", ".hpp", ".inc", ".yaml")
PAGES = ("index.html", "stage.html")

RED, GREEN, YELLOW, DIM, BOLD, RESET = ("\033[31m", "\033[32m", "\033[33m", "\033[2m", "\033[1m", "\033[0m") \
    if sys.stdout.isatty() else ("",) * 6


def say(msg, color=""):
    print(f"{DIM}{time.strftime('%H:%M:%S')}{RESET} {color}{msg}{RESET}", flush=True)


def git_rev():
    try:
        rev = subprocess.run(["git", "-C", SIM, "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                             check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", REPO, "status", "--porcelain", "--", "src", "lib", "sim"],
                               capture_output=True, text=True).stdout.strip()
        return rev + ("+dev" if dirty else "-dev")
    except (OSError, subprocess.CalledProcessError):
        return "dev"


# ───────────────────────────────────────────── shared state + SSE fan-out
class State:
    def __init__(self):
        self.cond = threading.Condition()
        self.started = int(time.time())
        self.n = 0
        self.building = False
        self.error = ""
        self.built = ""
        self.change_at = None  # mtime of the newest edit that led to the current version
        self.timings = {}

    @property
    def version(self):
        return f"{self.started}-{self.n}"

    def snapshot(self):
        with self.cond:
            return {"version": self.version, "building": self.building, "error": self.error, "built": self.built}

    def update(self, **kw):
        with self.cond:
            for k, v in kw.items():
                setattr(self, k, v)
            self.cond.notify_all()

    def bump(self, change_at, timings):
        with self.cond:
            self.n += 1
            self.building = False
            self.error = ""
            self.built = time.strftime("%H:%M:%S")
            self.change_at = change_at
            self.timings = timings
            self.cond.notify_all()


STATE = State()


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=DIST, **kw)

    def log_message(self, fmt, *args):
        pass

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        path = self.path.split("?")[0]
        if path.endswith("/__dev/state"):
            body = json.dumps(STATE.snapshot()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path.endswith("/__dev/events"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            last = None
            try:
                while True:
                    snap = STATE.snapshot()
                    if snap != last:
                        self.wfile.write(f"event: state\ndata: {json.dumps(snap)}\n\n".encode())
                        self.wfile.flush()
                        last = snap
                    with STATE.cond:
                        if not STATE.cond.wait(timeout=15):
                            self.wfile.write(b": keep-alive\n\n")
                            self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                return
        super().do_GET()

    def do_POST(self):
        if self.path.split("?")[0].endswith("/__dev/loaded"):
            n = int(self.headers.get("Content-Length") or 0)
            version = self.rfile.read(n).decode(errors="replace").strip()
            with STATE.cond:
                fresh = version == STATE.version and STATE.change_at
                change_at, timings = STATE.change_at, STATE.timings
                STATE.change_at = None
            if fresh:
                total = time.time() - change_at
                parts = " · ".join(f"{k} {v:.1f}s" for k, v in timings.items())
                say(f"page reloaded: edit → new firmware on screen {total:.1f}s ({parts} · reload+boot "
                    f"{total - sum(timings.values()):.1f}s)", GREEN)
            self.send_response(204)
            self.end_headers()
            return
        self.send_error(404)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ───────────────────────────────────────────── watching
def scan():
    files = {}
    for root, recursive in WATCH:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if not d.startswith(".") and os.path.join(dirpath, d) not in SKIP_DIRS]
            if not recursive:
                dirnames[:] = []
            for f in filenames:
                if f.startswith(".") or f.endswith(("~", ".swp", ".tmp")) or f.startswith("#"):
                    continue
                p = os.path.join(dirpath, f)
                try:
                    st = os.stat(p)
                except FileNotFoundError:
                    continue
                files[p] = (st.st_mtime_ns, st.st_size)
    return files


def diff(old, new):
    changed = {p for p, v in new.items() if old.get(p) != v}
    removed = set(old) - set(new)
    added = set(new) - set(old)
    return changed | removed, bool(added or removed)


def is_web(p):
    return p.startswith(WEB + os.sep)


def is_build_input(p):
    if is_web(p):
        return False
    name = os.path.basename(p)
    if p.startswith(os.path.join(SIM, "platform", "native_")):
        return False
    return name == "CMakeLists.txt" or name.endswith(SOURCE_EXT)


# ───────────────────────────────────────────── build steps
def sync_web(rev):
    for dirpath, _, filenames in os.walk(WEB):
        rel = os.path.relpath(dirpath, WEB)
        out_dir = os.path.normpath(os.path.join(DIST, rel))
        os.makedirs(out_dir, exist_ok=True)
        for f in filenames:
            src, dst = os.path.join(dirpath, f), os.path.join(out_dir, f)
            if rel == "." and f in PAGES:
                with open(src, encoding="utf-8") as fh:
                    text = fh.read().replace("__SIM_BUILD__", rev)
                with open(dst, "w", encoding="utf-8") as fh:
                    fh.write(text)
            else:
                shutil.copy2(src, dst)


def run(cmd):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return p.returncode, p.stdout


def error_excerpt(out):
    lines = out.splitlines()
    idx = [i for i, l in enumerate(lines) if re.search(r"\berror\b|undefined symbol|fatal", l, re.I)]
    if not idx:
        return "\n".join(lines[-60:])
    keep = []
    for i in idx[:12]:
        keep.extend(range(max(0, i - 1), min(len(lines), i + 6)))
    seen, out_lines = set(), []
    for i in keep:
        if i not in seen:
            seen.add(i)
            out_lines.append(lines[i])
    return "\n".join(out_lines)


def build_wasm(structural, i18n):
    jobs = str(os.cpu_count() or 4)
    if i18n:
        code, out = run([os.path.join(SIM, "scripts", "prepare.sh"), GEN])
        if code:
            return code, out
    makefile = os.path.join(BUILD, "Makefile")
    if structural or not os.path.exists(makefile):
        return run(["cmake", "--build", BUILD, "-j", jobs])
    # Skips CMake's re-check and dependency consolidation (several seconds);
    # header dependencies from the last full build still apply.
    return run(["make", "-C", BUILD, "-j", jobs, "sim/fast"])


def install_wasm():
    for f in ("sim.js", "sim.wasm"):
        tmp = os.path.join(DIST, f + ".tmp")
        shutil.copy2(os.path.join(BUILD, f), tmp)
        os.replace(tmp, os.path.join(DIST, f))


def short(p):
    return os.path.relpath(p, REPO)


def watch_loop(interval):
    rev = git_rev()
    sync_web(rev)
    files = scan()
    say(f"watching {len(files)} files under src/, lib/, sim/ and freeink-sdk UI/font/board", DIM)
    while True:
        time.sleep(interval)
        new = scan()
        changed, structural = diff(files, new)
        if not changed:
            continue
        detected = time.time()
        time.sleep(0.12)  # coalesce editors that write in several steps
        new = scan()
        more, more_struct = diff(files, new)
        changed |= more
        structural |= more_struct
        files = new
        edit_at = max([new[p][0] / 1e9 for p in changed if p in new] or [detected])

        web = sorted(p for p in changed if is_web(p))
        src = sorted(p for p in changed if is_build_input(p))
        if not web and not src:
            continue
        names = ", ".join(short(p) for p in (src + web)[:4]) + (" …" if len(src) + len(web) > 4 else "")
        timings = {"detect": max(0.0, detected - edit_at)}
        if src:
            structural = structural or any(os.path.basename(p) == "CMakeLists.txt" for p in src)
            i18n = any(os.sep + os.path.join("lib", "I18n") + os.sep in p for p in src)
            say(f"changed: {names} → {'full' if structural else 'incremental'} wasm build", YELLOW)
            STATE.update(building=True)
            t0 = time.time()
            code, out = build_wasm(structural, i18n)
            timings["build"] = time.time() - t0
            if code:
                excerpt = error_excerpt(out)
                say(f"build FAILED in {timings['build']:.1f}s:", RED + BOLD)
                print(f"{RED}{excerpt}{RESET}", flush=True)
                STATE.update(building=False, error=excerpt)
                continue
            install_wasm()
            say(f"built in {timings['build']:.1f}s", GREEN)
        else:
            say(f"changed: {names} → page files only", YELLOW)
        if web:
            sync_web(rev)
        STATE.bump(edit_at, timings)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8931")))
    ap.add_argument("--host", default=os.environ.get("HOST", "127.0.0.1"))
    ap.add_argument("--interval", type=float, default=0.25, help="poll interval in seconds")
    args = ap.parse_args()
    if not os.path.exists(os.path.join(DIST, "sim.wasm")):
        sys.exit("sim/dist has no sim.wasm: run SIM_DEV=1 sim/build-wasm.sh first (sim/dev.sh does)")
    server = Server((args.host, args.port), Handler)
    threading.Thread(target=watch_loop, args=(args.interval,), daemon=True).start()
    say(f"serving sim/dist at {BOLD}http://{args.host}:{args.port}/{RESET} (stage: /stage.html) · Ctrl-C to stop",
        GREEN)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
