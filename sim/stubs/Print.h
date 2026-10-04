#pragma once
// CrossPoint simulator: Arduino Print base class.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

class String;

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  virtual void flush() {}
  size_t write(const char* str) { return str ? write(reinterpret_cast<const uint8_t*>(str), strlen(str)) : 0; }
  size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }
  size_t print(const char* s) { return write(s); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int v) { return printf("%d", v); }
  size_t print(unsigned v) { return printf("%u", v); }
  size_t print(long v) { return printf("%ld", v); }
  size_t print(unsigned long v) { return printf("%lu", v); }
  size_t print(double v, int digits = 2) { return printf("%.*f", digits, v); }
  size_t print(const String& s);
  size_t println() { return write("\n"); }
  template <typename T>
  size_t println(const T& v) {
    size_t n = print(v);
    return n + println();
  }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return 0;
    return write(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(n) < sizeof(buf) ? n : sizeof(buf) - 1);
  }
};
