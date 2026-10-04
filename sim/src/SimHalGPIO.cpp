// CrossPoint simulator: HalGPIO fed by injected key and touch events.
// Each update() consumes at most one queued event so a fast press+release
// still yields a wasPressed() frame followed by a wasReleased() frame.
#include <HalGPIO.h>

#include <cmath>
#include <deque>

#include "SimDisplay.h"
#include "SimInput.h"

HalGPIO gpio;

namespace {
constexpr unsigned long LONG_PRESS_MS = 600;
constexpr unsigned long HOME_LONG_PRESS_MS = 1000;
constexpr float TAP_SLOP_PX = 14.0f;
constexpr float SWIPE_MIN_PX = 60.0f;

enum class EvType : uint8_t { KeyDown, KeyUp, TouchDown, TouchMove, TouchUp };
struct Ev {
  EvType type;
  uint8_t key;
  int x;
  int y;
};
std::deque<Ev> queue;

struct State {
  uint8_t keys = 0;
  uint8_t pressedEdges = 0;
  uint8_t releasedEdges = 0;
  unsigned long keyDownAt = 0;
  unsigned long lastHeldMs = 0;
  unsigned long powerHeldMs = 0;

  bool homeDown = false;
  bool homePressedEdge = false;
  bool homeTapped = false;
  bool homeLong = false;
  bool homeLongFired = false;
  unsigned long homeDownAt = 0;

  bool touching = false;
  bool suppressed = false;
  bool movedBeyondSlop = false;
  bool longFired = false;
  bool touchDownEdge = false;
  bool touchReleasedEdge = false;
  bool tapEdge = false;
  bool longEdge = false;
  bool swipeEdge = false;
  float downX = 0, downY = 0, curX = 0, curY = 0;
  unsigned long touchDownAt = 0;
  unsigned long touchHeldMs = 0;
} st;

// Portrait UI pixel -> normalized physical panel coords (inverse of
// GfxRenderer::tapToLogical for Portrait: x = H-1-phyY, y = phyX).
void toNormalized(const float x, const float y, float& nx, float& ny) {
  const float phyX = y;
  const float phyY = static_cast<float>(sim::PANEL_H - 1) - x;
  nx = (phyX + 0.5f) / sim::PANEL_W;
  ny = (phyY + 0.5f) / sim::PANEL_H;
}

void applyEvent(const Ev& ev, const unsigned long now) {
  switch (ev.type) {
    case EvType::KeyDown:
      if (ev.key == static_cast<uint8_t>(sim::Key::Home)) {
        st.homeDown = true;
        st.homePressedEdge = true;
        st.homeDownAt = now;
        st.homeLongFired = false;
      } else if (!(st.keys & (1u << ev.key))) {
        st.keys |= static_cast<uint8_t>(1u << ev.key);
        st.pressedEdges |= static_cast<uint8_t>(1u << ev.key);
        st.keyDownAt = now;
      }
      break;
    case EvType::KeyUp:
      if (ev.key == static_cast<uint8_t>(sim::Key::Home)) {
        if (st.homeDown && !st.homeLongFired) st.homeTapped = true;
        st.homeDown = false;
      } else if (st.keys & (1u << ev.key)) {
        st.keys &= static_cast<uint8_t>(~(1u << ev.key));
        st.releasedEdges |= static_cast<uint8_t>(1u << ev.key);
        st.lastHeldMs = now - st.keyDownAt;
        if (ev.key == HalGPIO::BTN_POWER) st.powerHeldMs = st.lastHeldMs;
      }
      break;
    case EvType::TouchDown:
      st.touching = true;
      st.suppressed = false;
      st.movedBeyondSlop = false;
      st.longFired = false;
      st.touchDownEdge = true;
      st.downX = st.curX = static_cast<float>(ev.x);
      st.downY = st.curY = static_cast<float>(ev.y);
      st.touchDownAt = now;
      break;
    case EvType::TouchMove:
      if (!st.touching) break;
      st.curX = static_cast<float>(ev.x);
      st.curY = static_cast<float>(ev.y);
      if (std::hypot(st.curX - st.downX, st.curY - st.downY) > TAP_SLOP_PX) st.movedBeyondSlop = true;
      break;
    case EvType::TouchUp: {
      if (!st.touching) break;
      st.curX = static_cast<float>(ev.x);
      st.curY = static_cast<float>(ev.y);
      const float dist = std::hypot(st.curX - st.downX, st.curY - st.downY);
      if (dist > TAP_SLOP_PX) st.movedBeyondSlop = true;
      st.touching = false;
      st.touchHeldMs = now - st.touchDownAt;
      st.touchReleasedEdge = true;
      if (!st.suppressed) {
        if (!st.movedBeyondSlop && !st.longFired) st.tapEdge = true;
        if (dist >= SWIPE_MIN_PX) st.swipeEdge = true;
      }
      st.suppressed = false;
      break;
    }
  }
}
}  // namespace

namespace sim {
void keyDown(const Key key) { queue.push_back({EvType::KeyDown, static_cast<uint8_t>(key), 0, 0}); }
void keyUp(const Key key) { queue.push_back({EvType::KeyUp, static_cast<uint8_t>(key), 0, 0}); }
void touchDown(const int x, const int y) { queue.push_back({EvType::TouchDown, 0, x, y}); }
void touchMove(const int x, const int y) { queue.push_back({EvType::TouchMove, 0, x, y}); }
void touchUp(const int x, const int y) { queue.push_back({EvType::TouchUp, 0, x, y}); }
bool inputPending() { return !queue.empty(); }
}  // namespace sim

bool HalGPIO::isXteinkDevice() const { return false; }
bool HalGPIO::hasEdgeSideButtons() const { return false; }
void HalGPIO::begin() {}

void HalGPIO::update() {
  const unsigned long now = millis();
  st.pressedEdges = st.releasedEdges = 0;
  st.homePressedEdge = st.homeTapped = st.homeLong = false;
  st.touchDownEdge = st.touchReleasedEdge = st.tapEdge = st.longEdge = st.swipeEdge = false;

  // Coalesce consecutive moves; otherwise one event per frame.
  while (!queue.empty()) {
    const Ev ev = queue.front();
    queue.pop_front();
    applyEvent(ev, now);
    if (ev.type != EvType::TouchMove || queue.empty() || queue.front().type != EvType::TouchMove) break;
  }

  if (st.homeDown && !st.homeLongFired && now - st.homeDownAt >= HOME_LONG_PRESS_MS) {
    st.homeLongFired = true;
    st.homeLong = true;
  }
  if (st.touching && !st.suppressed && !st.longFired && !st.movedBeyondSlop &&
      now - st.touchDownAt >= LONG_PRESS_MS) {
    st.longFired = true;
    st.longEdge = true;
  }
}

bool HalGPIO::isPressed(const uint8_t i) const { return i < 8 && (st.keys & (1u << i)); }
bool HalGPIO::wasPressed(const uint8_t i) const { return i < 8 && (st.pressedEdges & (1u << i)); }
bool HalGPIO::wasAnyPressed() const { return st.pressedEdges != 0; }
bool HalGPIO::wasReleased(const uint8_t i) const { return i < 8 && (st.releasedEdges & (1u << i)); }
bool HalGPIO::wasAnyReleased() const { return st.releasedEdges != 0; }
unsigned long HalGPIO::getHeldTime() const { return st.keys ? millis() - st.keyDownAt : st.lastHeldMs; }
unsigned long HalGPIO::getPowerButtonHeldTime() const {
  return isPressed(BTN_POWER) ? millis() - st.keyDownAt : st.powerHeldMs;
}
bool HalGPIO::rawInputActive() { return st.keys != 0 || st.touching || st.homeDown; }
bool HalGPIO::hasTouch() const { return true; }
bool HalGPIO::hasHomeKey() const { return true; }
bool HalGPIO::wasHomeKeyPressed() const { return st.homePressedEdge; }
bool HalGPIO::wasHomeKeyTapped() const { return st.homeTapped; }
bool HalGPIO::wasHomeKeyLongPressed() const { return st.homeLong; }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  if (!st.tapEdge) return false;
  toNormalized(st.curX, st.curY, nx, ny);
  return true;
}
bool HalGPIO::wasTouchDown(float& nx, float& ny) const {
  if (!st.touchDownEdge) return false;
  toNormalized(st.downX, st.downY, nx, ny);
  return true;
}
bool HalGPIO::wasTouchReleased() const { return st.touchReleasedEdge; }
bool HalGPIO::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  if (!st.touching || st.suppressed || st.movedBeyondSlop || st.longFired) return false;
  toNormalized(st.downX, st.downY, nx, ny);
  heldMs = millis() - st.touchDownAt;
  return true;
}
bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const {
  if (!st.touching || st.suppressed) return false;
  toNormalized(st.curX, st.curY, nx, ny);
  return true;
}
bool HalGPIO::wasTouchLongPress(float& nx, float& ny) const {
  if (!st.longEdge) return false;
  toNormalized(st.downX, st.downY, nx, ny);
  return true;
}
void HalGPIO::suppressTouchContact() {
  if (st.touching) st.suppressed = true;
}
unsigned long HalGPIO::lastTouchHeldMs() const { return st.touching ? millis() - st.touchDownAt : st.touchHeldMs; }
bool HalGPIO::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  if (!st.swipeEdge) return false;
  toNormalized(st.downX, st.downY, nxStart, nyStart);
  toNormalized(st.curX, st.curY, nxEnd, nyEnd);
  return true;
}
bool HalGPIO::wasTouchActivity() const { return st.touchDownEdge || st.touchReleasedEdge || st.touching; }
void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool) {}
bool HalGPIO::verifyPowerButtonWakeup() { return true; }
bool HalGPIO::isUsbConnected() const { return true; }
bool HalGPIO::coldBootImpliesPowerButton() const { return false; }
bool HalGPIO::wasUsbStateChanged() const { return false; }
HalGPIO::WakeupReason HalGPIO::getWakeupReason() const { return WakeupReason::Other; }
