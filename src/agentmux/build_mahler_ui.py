# The mahler screen: a Rust static library, ui-ffi in esp32-agent-mux (the
# checkout this fork sits in, or MAHLER_UI_FFI), shared with the bridge. Built
# for the device before each build and linked from where cargo puts it, so a
# screen change needs no copy here; cargo does nothing when it is current.
import os
import subprocess

Import("env")  # noqa: F821 (PlatformIO)

ffi = os.path.abspath(os.environ.get("MAHLER_UI_FFI") or os.path.join(env["PROJECT_DIR"], "..", "ui-ffi"))
if not os.path.isfile(os.path.join(ffi, "Cargo.toml")):
    raise SystemExit(f"mahler_ui: no ui-ffi at {ffi}; check out esp32-agent-mux around this fork, or set MAHLER_UI_FFI")
if not os.path.isfile(os.path.expanduser("~/export-esp.sh")):
    raise SystemExit("mahler_ui: no ~/export-esp.sh; install the Rust toolchain with: espup install --targets esp32s3")

# PlatformIO's own compiler variables would reach cargo's build scripts.
cargo_env = {k: v for k, v in os.environ.items() if k not in ("CC", "CXX", "AR", "CFLAGS", "CXXFLAGS", "LDFLAGS")}
subprocess.check_call(
    ["sh", "-c", '. "$HOME/export-esp.sh" && exec cargo +esp build --release --target xtensa-esp32s3-none-elf -Zbuild-std=core,alloc'],
    cwd=ffi,
    env=cargo_env,
)
env.Append(  # noqa: F821
    CPPPATH=[os.path.join(ffi, "include")],
    LIBPATH=[os.path.join(ffi, "target", "xtensa-esp32s3-none-elf", "release")],
    LIBS=["mahler_ui"],
)
