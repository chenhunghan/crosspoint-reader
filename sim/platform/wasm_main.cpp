// CrossPoint simulator (WebAssembly): browser front end. The page
// (sim/web/sim.js) provides Module.simHost; this file exports the input,
// socket and framebuffer entry points it calls, and runs the firmware loop
// from requestAnimationFrame.
#include <ESPmDNS.h>
#include <Logging.h>
#include <WebSocketsClient.h>
#include <emscripten/emscripten.h>

#include <map>
#include <string>
#include <vector>

#include "SimApp.h"
#include "SimDisplay.h"
#include "SimHost.h"
#include "SimInput.h"
#include "SimSocket.h"
#include "SimStorage.h"
#include "activities/Activity.h"

extern ActivityManager activityManager;

// --- JavaScript side (sim/web/sim.js implements Module.simHost) -------------
EM_JS(void, js_log, (const char* text), { Module.simHost.log(UTF8ToString(text)); });
EM_JS(void, js_frame, (int mode), { Module.simHost.framePresented(mode); });
EM_JS(void, js_storage_changed, (), { Module.simHost.storageChanged(); });
EM_JS(void, js_restart, (), { Module.simHost.restart(); });
EM_JS(void, js_ws_open, (int id, const char* url), { Module.simHost.wsOpen(id, UTF8ToString(url)); });
EM_JS(int, js_ws_send, (int id, const char* data, int len),
      { return Module.simHost.wsSend(id, UTF8ToString(data, len)) ? 1 : 0; });
EM_JS(void, js_ws_close, (int id), { Module.simHost.wsClose(id); });
EM_JS(char*, js_mdns_answer, (), {
  const s = Module.simHost.mdnsAnswer() || "";
  const n = lengthBytesUTF8(s) + 1;
  const p = _malloc(n);
  stringToUTF8(s, p, n);
  return p;
});

namespace sim {
void hostLog(const char* text) { js_log(text); }
void hostRestart() {
  js_restart();
  emscripten_force_exit(0);
  __builtin_unreachable();
}
void hostFramePresented(const int refreshMode) { js_frame(refreshMode); }
void hostStorageChanged() { js_storage_changed(); }
}  // namespace sim

// --- sockets -------------------------------------------------------------------
namespace {
int nextSocketId = 1;
std::map<int, WebSocketsClient*> sockets;
std::vector<uint8_t> rgba(sim::UI_W* sim::UI_H * 4);

// "host:port:name" from the page (empty = mDNS finds nothing).
void refreshMdnsAnswer() {
  char* raw = js_mdns_answer();
  std::string s = raw ? raw : "";
  free(raw);
  if (s.empty()) {
    simSetMdnsResult("", 0, "");
    return;
  }
  std::string host = s, name;
  uint16_t port = 7878;
  const auto c1 = s.find(':');
  if (c1 != std::string::npos) {
    host = s.substr(0, c1);
    const auto c2 = s.find(':', c1 + 1);
    port = static_cast<uint16_t>(atoi(s.substr(c1 + 1, c2 - c1 - 1).c_str()));
    if (c2 != std::string::npos) name = s.substr(c2 + 1);
  }
  simSetMdnsResult(host.c_str(), port, name.c_str());
}

void loopOnce() { sim::tick(); }
}  // namespace

int simSocketOpen(const char* url, WebSocketsClient* owner) {
  const int id = nextSocketId++;
  sockets[id] = owner;
  js_ws_open(id, url);
  return id;
}
bool simSocketSend(const int id, const char* data, const size_t length) {
  return sockets.count(id) && js_ws_send(id, data, static_cast<int>(length));
}
void simSocketClose(const int id) {
  if (sockets.erase(id)) js_ws_close(id);
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void sim_key(const int down, const int key) {
  const auto k = static_cast<sim::Key>(key);
  down ? sim::keyDown(k) : sim::keyUp(k);
}
// phase: 0 = down, 1 = move, 2 = up. Coordinates in portrait UI pixels.
EMSCRIPTEN_KEEPALIVE void sim_touch(const int phase, const int x, const int y) {
  if (phase == 0) sim::touchDown(x, y);
  if (phase == 1) sim::touchMove(x, y);
  if (phase == 2) sim::touchUp(x, y);
}
EMSCRIPTEN_KEEPALIVE void sim_ws_opened(const int id) {
  if (auto it = sockets.find(id); it != sockets.end()) it->second->transportOpened();
}
EMSCRIPTEN_KEEPALIVE void sim_ws_message(const int id, const char* data, const int len) {
  if (auto it = sockets.find(id); it != sockets.end()) it->second->transportMessage(data, static_cast<size_t>(len));
}
EMSCRIPTEN_KEEPALIVE void sim_ws_binary(const int id, const uint8_t* data, const int len) {
  if (auto it = sockets.find(id); it != sockets.end()) it->second->transportBinary(data, static_cast<size_t>(len));
}
EMSCRIPTEN_KEEPALIVE void sim_ws_closed(const int id) {
  auto it = sockets.find(id);
  if (it == sockets.end()) return;
  WebSocketsClient* owner = it->second;
  sockets.erase(it);
  owner->transportClosed();
}
EMSCRIPTEN_KEEPALIVE uint8_t* sim_frame_rgba() {
  sim::renderPortraitRgba(rgba.data());
  return rgba.data();
}
EMSCRIPTEN_KEEPALIVE uint32_t sim_frame_count() { return sim::frameCount(); }
// Re-reads the page's mDNS answer and re-enters the start screen (after the
// page rewrote /.crosspoint/agentmux.json).
EMSCRIPTEN_KEEPALIVE void sim_restart_app() {
  refreshMdnsAnswer();
  activityManager.goHome();
}
}

int main() {
  sim::setStorageRoot("/sd");
  refreshMdnsAnswer();
  sim::setup();
  emscripten_set_main_loop(loopOnce, 0, false);
  return 0;
}
