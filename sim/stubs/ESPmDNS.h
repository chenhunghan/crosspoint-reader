#pragma once
// CrossPoint simulator: mDNS discovery. queryService() answers with the host
// configured by the page (simSetMdnsHost), or finds nothing.
#include <Arduino.h>

#include "IPAddress.h"

class SimMDNSResponder {
 public:
  bool begin(const char*) { return true; }
  void end() {}
  int queryService(const char* service, const char* proto);
  IPAddress address(int) const;
  String hostname(int) const;
  uint16_t port(int) const;
};
extern SimMDNSResponder MDNS;

// Set by the host page / native scenario. Empty host => no results.
void simSetMdnsResult(const char* ip, uint16_t port, const char* hostname);
