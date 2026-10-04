// CrossPoint simulator: Arduino / ESP-IDF runtime pieces (time, serial,
// logging, ESP, WiFi, mDNS, base64, MAC).
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <base64.h>
#include <esp_mac.h>
#include <freertos/task.h>
#include <mbedtls/base64.h>

#include <chrono>
#include <cstdarg>
#include <random>
#include <string>

#include "SimClock.h"
#include "SimHost.h"

namespace sim {
namespace {
bool virtualClock = false;
uint64_t virtualNow = 1000;
const auto startTime = std::chrono::steady_clock::now();
}  // namespace

void useVirtualClock(const bool enabled) { virtualClock = enabled; }
void advanceClock(const uint32_t ms) { virtualNow += ms; }
uint64_t nowMs() {
  if (virtualClock) return virtualNow;
  return 1000 + static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime)
                        .count());
}
}  // namespace sim

unsigned long millis() { return static_cast<unsigned long>(sim::nowMs()); }
unsigned long micros() { return static_cast<unsigned long>(sim::nowMs() * 1000); }
void delay(const unsigned long ms) {
  // Never block the browser's main thread; in virtual time just advance.
  sim::advanceClock(static_cast<uint32_t>(ms));
}
void delayMicroseconds(unsigned int) {}
TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(millis()); }
void vTaskDelay(const TickType_t ticks) { delay(ticks); }

namespace {
std::mt19937& rng() {
  static std::mt19937 r(12345);
  return r;
}
}  // namespace
long random(const long max) { return max <= 0 ? 0 : static_cast<long>(rng()() % static_cast<uint32_t>(max)); }
long random(const long min, const long max) { return max <= min ? min : min + random(max - min); }
uint32_t esp_random() { return rng()(); }

// --- Serial / logging --------------------------------------------------------
HardwareSerial Serial;
HardwareSerial Serial0;

size_t HardwareSerial::write(const uint8_t c) {
  sim::hostLog(std::string(1, static_cast<char>(c)).c_str());
  return 1;
}
size_t HardwareSerial::write(const uint8_t* buffer, const size_t size) {
  sim::hostLog(std::string(reinterpret_cast<const char*>(buffer), size).c_str());
  return size;
}

void logPrintf(const char* level, const char* origin, const char* format, ...) {
  char msg[768];
  int n = snprintf(msg, sizeof(msg), "[%7lu] [%s] [%s] ", millis(), level, origin);
  if (n < 0) n = 0;
  va_list ap;
  va_start(ap, format);
  vsnprintf(msg + n, sizeof(msg) - n, format, ap);
  va_end(ap);
  sim::hostLog(msg);
}

// --- ESP ---------------------------------------------------------------------
EspClass ESP;
void EspClass::restart() {
  sim::hostRestart();
  // hostRestart() does not return on any platform.
  abort();
}

esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
  static const uint8_t m[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0x5E, 0x11};
  memcpy(mac, m, 6);
  return ESP_OK;
}
esp_err_t esp_efuse_mac_get_default(uint8_t* mac) { return esp_read_mac(mac, ESP_MAC_WIFI_STA); }

// --- Buses (inert) -----------------------------------------------------------
TwoWire Wire;
TwoWire Wire1;
SPIClass SPI;

// --- WiFi / mDNS -------------------------------------------------------------
SimWiFiClass WiFi;
SimMDNSResponder MDNS;

namespace {
std::string mdnsIp;
std::string mdnsName;
uint16_t mdnsPort = 7878;
}  // namespace

void simSetMdnsResult(const char* ip, const uint16_t port, const char* hostname) {
  mdnsIp = ip ? ip : "";
  mdnsPort = port ? port : 7878;
  mdnsName = hostname && hostname[0] ? hostname : mdnsIp;
}

int SimMDNSResponder::queryService(const char* service, const char* proto) {
  LOG_INF("MDNS", "query _%s._%s -> %s", service, proto, mdnsIp.empty() ? "(none)" : mdnsIp.c_str());
  return mdnsIp.empty() ? 0 : 1;
}
IPAddress SimMDNSResponder::address(int) const {
  IPAddress ip;
  ip.setLabel(mdnsIp.c_str());
  return ip;
}
String SimMDNSResponder::hostname(int) const { return String(mdnsName.c_str()); }
uint16_t SimMDNSResponder::port(int) const { return mdnsPort; }

// --- base64 ------------------------------------------------------------------
namespace {
const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int b64Value(const unsigned char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}
}  // namespace

String base64::encode(const uint8_t* data, const size_t length) {
  std::string out;
  out.reserve((length + 2) / 3 * 4);
  for (size_t i = 0; i < length; i += 3) {
    const uint32_t v = (data[i] << 16) | (i + 1 < length ? data[i + 1] << 8 : 0) | (i + 2 < length ? data[i + 2] : 0);
    out += B64[(v >> 18) & 63];
    out += B64[(v >> 12) & 63];
    out += i + 1 < length ? B64[(v >> 6) & 63] : '=';
    out += i + 2 < length ? B64[v & 63] : '=';
  }
  return String(out);
}

int mbedtls_base64_decode(unsigned char* dst, const size_t dlen, size_t* olen, const unsigned char* src,
                          const size_t slen) {
  std::string out;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < slen; ++i) {
    const unsigned char c = src[i];
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
    const int v = b64Value(c);
    if (v < 0) return MBEDTLS_ERR_BASE64_INVALID_CHARACTER;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xFF);
    }
  }
  *olen = out.size();
  if (!dst || dlen < out.size()) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  memcpy(dst, out.data(), out.size());
  return 0;
}

int mbedtls_base64_encode(unsigned char* dst, const size_t dlen, size_t* olen, const unsigned char* src,
                          const size_t slen) {
  const String s = base64::encode(src, slen);
  *olen = s.length();
  if (!dst || dlen < s.length() + 1) return MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL;
  memcpy(dst, s.c_str(), s.length() + 1);
  return 0;
}
