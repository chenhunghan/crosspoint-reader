// CrossPoint simulator (native): runs a scripted Agent Mux scenario on a
// virtual clock against the in-process fake bridge and writes PNG snapshots
// of the simulated panel to the output directory (default sim/out).
//
//   sim-native [out_dir]
#include <HalStorage.h>
#include <Logging.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "SimApp.h"
#include "SimClock.h"
#include "SimDisplay.h"
#include "SimHost.h"
#include "SimInput.h"
#include "SimStorage.h"
#include "activities/Activity.h"
#include "native_fake_bridge.h"

extern ActivityManager activityManager;
void simSetMdnsResult(const char* ip, uint16_t port, const char* hostname);

namespace {
std::string outDir = "sim/out";
int snapshotCount = 0;
int failures = 0;

// --- PNG (stored deflate blocks; no zlib dependency) -------------------------
uint32_t crcTable[256];
void initCrc() {
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
}
uint32_t crc(const uint8_t* buf, size_t len, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < len; ++i) c = crcTable[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
  return c;
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24);
  v.push_back(x >> 16);
  v.push_back(x >> 8);
  v.push_back(x);
}
void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
  put32(png, static_cast<uint32_t>(data.size()));
  std::vector<uint8_t> td(type, type + 4);
  td.insert(td.end(), data.begin(), data.end());
  png.insert(png.end(), td.begin(), td.end());
  put32(png, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

bool writePng(const std::string& path, const uint8_t* rgba, int w, int h) {
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(h) * (w * 3 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    for (int x = 0; x < w; ++x) {
      const uint8_t* p = rgba + (static_cast<size_t>(y) * w + x) * 4;
      raw.insert(raw.end(), p, p + 3);
    }
  }
  std::vector<uint8_t> z = {0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  for (size_t off = 0; off < raw.size(); off += 65535) {
    const size_t n = std::min<size_t>(65535, raw.size() - off);
    z.push_back(off + n == raw.size() ? 1 : 0);
    z.push_back(n & 0xFF);
    z.push_back(n >> 8);
    z.push_back(~n & 0xFF);
    z.push_back((~n >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + static_cast<long>(off), raw.begin() + static_cast<long>(off + n));
  }
  put32(z, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  put32(ihdr, w);
  put32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  fwrite(png.data(), 1, png.size(), f);
  fclose(f);
  return true;
}

// --- scenario helpers ---------------------------------------------------------
void run(const uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += 20) {
    sim::advanceClock(20);
    sim::fakeBridgePump();
    sim::tick();
  }
}

void snapshot(const char* name) {
  std::vector<uint8_t> rgba(sim::UI_W * sim::UI_H * 4);
  sim::renderPortraitRgba(rgba.data());
  char path[512];
  snprintf(path, sizeof(path), "%s/%02d-%s.png", outDir.c_str(), ++snapshotCount, name);
  if (writePng(path, rgba.data(), sim::UI_W, sim::UI_H)) {
    printf("[scenario] wrote %s (frame %u)\n", path, sim::frameCount());
  } else {
    printf("[scenario] FAILED to write %s\n", path);
    ++failures;
  }
}

void press(const sim::Key key) {
  sim::keyDown(key);
  run(120);
  sim::keyUp(key);
  run(200);
}

void tap(const int x, const int y) {
  sim::touchDown(x, y);
  run(140);
  sim::touchUp(x, y);
  run(200);
}

void expect(const bool ok, const char* what) {
  printf("[scenario] %s: %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++failures;
}

// True if any ink exists in the given UI rectangle.
bool inkIn(const int x0, const int y0, const int x1, const int y1) {
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      if (sim::inkAt(x, y)) return true;
    }
  }
  return false;
}

void removeTree(const std::string& path) {
  const std::string cmd = "rm -rf '" + path + "'";
  if (system(cmd.c_str()) != 0) printf("[scenario] could not clear %s\n", path.c_str());
}
}  // namespace

namespace sim {
void hostLog(const char* text) { fputs(text, stdout); }
void hostRestart() {
  printf("[scenario] ESP.restart() requested\n");
  exit(2);
}
void hostFramePresented(int) {}
void hostStorageChanged() {}
}  // namespace sim

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  if (argc > 1) outDir = argv[1];
  mkdir(outDir.c_str(), 0755);
  initCrc();
  sim::useVirtualClock(true);
  const std::string sd = outDir + "/sdcard";
  removeTree(sd);
  sim::setStorageRoot(sd.c_str());
  sim::fakeBridgeReset();

  // 1) Fresh device: mDNS finds a bridge, the app asks for the pairing token.
  simSetMdnsResult("192.168.1.20", 7878, "studio-mac");
  sim::setup();
  run(1500);
  snapshot("token-keyboard");
  expect(sim::frameCount() > 0, "first frame rendered");

  // 2) Paired: seed the token like a returning user and restart the app.
  expect(Storage.mkdir("/.crosspoint") &&
             Storage.writeFile("/.crosspoint/agentmux.json",
                               String(R"({"host":"192.168.1.20","port":7878,"token":"demo1234"})")),
         "seed agentmux.json");
  activityManager.goHome();
  run(3000);
  snapshot("session-list");
  expect(inkIn(0, 120, 480, 600), "session rows drawn");

  // 3) Open the blocked session (first row) with BOOT/Confirm.
  press(sim::Key::Confirm);
  run(2500);
  snapshot("permission-dialog");

  // 4) Move the selection to the second option (Vol-/left pill = Down).
  press(sim::Key::Down);
  run(300);
  snapshot("permission-option-2");

  // 5) Approve with the first option via touch on the dialog would need its
  //    geometry; use the buttons: back to option 1 and confirm.
  press(sim::Key::Up);
  press(sim::Key::Confirm);
  run(3500);
  snapshot("session-terminal");

  // 6) Reply keyboard (Confirm opens Reply when no dialog is up).
  press(sim::Key::Confirm);
  run(800);
  snapshot("reply-keyboard");

  // 7) Back out of the keyboard and the session to the list.
  press(sim::Key::Back);
  run(800);
  press(sim::Key::Back);
  run(2500);
  snapshot("session-list-after");

  // 8) Touch: tap the second row (codex, working).
  tap(240, 260);
  run(2500);
  snapshot("session-touch-open");

  printf("[scenario] %d snapshot(s), %d failure(s)\n", snapshotCount, failures);
  fflush(stdout);
  // Like the firmware, never run global destructors (they would tear down
  // the fake bridge before the app's socket).
  _exit(failures == 0 ? 0 : 1);
}
