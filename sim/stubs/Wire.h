#pragma once
// CrossPoint simulator: inert I2C bus (no devices answer).
#include <Arduino.h>
class TwoWire {
 public:
  bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
  bool end() { return true; }
  void setClock(uint32_t) {}
  void setTimeOut(uint16_t) {}
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission(bool = true) { return 2; }
  size_t requestFrom(uint8_t, size_t, bool = true) { return 0; }
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  int available() { return 0; }
  int read() { return -1; }
};
extern TwoWire Wire;
extern TwoWire Wire1;
