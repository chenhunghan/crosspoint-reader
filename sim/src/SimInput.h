#pragma once
// Input injected by the host page / native scenario. Coordinates are in the
// portrait UI space the firmware draws in (480x800, origin top-left).
#include <cstdint>

namespace sim {
enum class Key : uint8_t {
  Back = 0,     // HalGPIO::BTN_BACK (virtual on Metalio; the header back arrow is touch)
  Confirm = 1,  // BOOT (GPIO0)
  Left = 2,
  Right = 3,
  Up = 4,       // Volume + / right grey pill ("PREV" cover key)
  Down = 5,     // Volume - / left grey pill ("NEXT" cover key)
  Power = 6,    // POWER (GPIO3)
  Home = 7,     // orange HOME cover key (capacitive)
};

void keyDown(Key key);
void keyUp(Key key);
void touchDown(int x, int y);
void touchMove(int x, int y);
void touchUp(int x, int y);
// True while events are still waiting to be consumed by HalGPIO::update().
bool inputPending();
}  // namespace sim
