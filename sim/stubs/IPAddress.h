#pragma once
#include <Arduino.h>
class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : b_{a, b, c, d} {}
  uint8_t operator[](int i) const { return b_[i & 3]; }
  // Simulator extension: lets the fake mDNS answer with a symbolic host
  // ("demo") that the page's WebSocket layer routes to the demo bridge.
  void setLabel(const char* label) {
    label_[0] = '\0';
    if (label) snprintf(label_, sizeof(label_), "%s", label);
  }
  String toString() const {
    if (label_[0]) return String(label_);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
    return String(buf);
  }
  bool fromString(const char* s) {
    unsigned a, b, c, d;
    if (!s || sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    b_[0] = a; b_[1] = b; b_[2] = c; b_[3] = d;
    return true;
  }
  operator uint32_t() const { return b_[0] | (b_[1] << 8) | (b_[2] << 16) | (uint32_t(b_[3]) << 24); }

 private:
  uint8_t b_[4] = {0, 0, 0, 0};
  char label_[64] = {0};
};
