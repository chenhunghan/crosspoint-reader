#pragma once
// CrossPoint simulator: minimal Arduino core surface for host / WebAssembly
// builds. Only what the compiled firmware sources actually touch.

#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Print.h"
#include "WString.h"

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif
#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif
#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))
#define pgm_read_word(addr) (*reinterpret_cast<const uint16_t*>(addr))
#define pgm_read_dword(addr) (*reinterpret_cast<const uint32_t*>(addr))
#define pgm_read_ptr(addr) (*reinterpret_cast<const void* const*>(addr))
#define memcpy_P memcpy
#define strlen_P strlen
#define F(s) (s)

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define PULLUP 0x04
#define INPUT_PULLUP 0x05
#define PULLDOWN 0x08
#define INPUT_PULLDOWN 0x09
#define OUTPUT_OPEN_DRAIN 0x13

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
inline void yield() {}
inline void pinMode(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int, int) {}
inline int analogRead(int) { return 0; }
inline uint32_t analogReadMilliVolts(int) { return 0; }
long random(long max);
long random(long min, long max);
inline void randomSeed(unsigned long) {}
uint32_t esp_random();

template <typename T, typename L, typename H>
inline T constrain(T v, L lo, H hi) {
  return v < lo ? static_cast<T>(lo) : (v > hi ? static_cast<T>(hi) : v);
}

class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  void end() {}
  operator bool() const { return true; }
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buffer, size_t size) override;
  void flush() override {}
  int available() { return 0; }
  int read() { return -1; }
  void setTxTimeoutMs(uint32_t) {}
};
using HWCDC = HardwareSerial;
extern HardwareSerial Serial;
extern HardwareSerial Serial0;

class EspClass {
 public:
  [[noreturn]] void restart();
  uint32_t getFreeHeap() { return 256 * 1024; }
  uint32_t getHeapSize() { return 320 * 1024; }
  uint32_t getMinFreeHeap() { return 200 * 1024; }
  uint32_t getMaxAllocHeap() { return 128 * 1024; }
  uint32_t getFreePsram() { return 6 * 1024 * 1024; }
  uint32_t getPsramSize() { return 8 * 1024 * 1024; }
  uint64_t getEfuseMac() { return 0x0000A1B2C3D4E5F6ULL; }
  const char* getSdkVersion() { return "sim"; }
  uint32_t getCpuFreqMHz() { return 240; }
};
extern EspClass ESP;
