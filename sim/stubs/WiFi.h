#pragma once
// CrossPoint simulator: WiFi is always up (STA, fake IP/MAC), so Agent Mux
// skips the WiFi selection screen.
#include <Arduino.h>

#include "IPAddress.h"

typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED, WL_CONNECT_FAILED,
               WL_CONNECTION_LOST, WL_DISCONNECTED, WL_NO_SHIELD = 255 } wl_status_t;
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA, WIFI_MODE_MAX } wifi_mode_t;
#define WIFI_OFF WIFI_MODE_NULL
#define WIFI_STA WIFI_MODE_STA
#define WIFI_AP WIFI_MODE_AP
#define WIFI_AP_STA WIFI_MODE_APSTA

class SimWiFiClass {
 public:
  wl_status_t status() const { return WL_CONNECTED; }
  wifi_mode_t getMode() const { return mode_; }
  bool mode(wifi_mode_t m) {
    mode_ = m;
    return true;
  }
  bool disconnect(bool = false, bool = false) { return true; }
  wl_status_t begin(const char*, const char* = nullptr) { return WL_CONNECTED; }
  bool isConnected() const { return true; }
  IPAddress localIP() const { return IPAddress(192, 168, 1, 77); }
  String SSID() const { return String("SimulatorLAN"); }
  int32_t RSSI() const { return -48; }
  String macAddress() const { return String("A1:B2:C3:D4:E5:F6"); }
  uint8_t* macAddress(uint8_t* mac) const {
    static const uint8_t m[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    for (int i = 0; i < 6; ++i) mac[i] = m[i];
    return mac;
  }
  bool setHostname(const char*) { return true; }
  bool setSleep(bool) { return true; }
  int16_t scanNetworks(bool = false, bool = false) { return 0; }
  int16_t scanComplete() { return 0; }
  void scanDelete() {}

 private:
  wifi_mode_t mode_ = WIFI_MODE_STA;
};
extern SimWiFiClass WiFi;
