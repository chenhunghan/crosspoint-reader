#pragma once
#if AGENTMUX

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

namespace agentmux {

// Bridge connection settings, persisted to /.crosspoint/agentmux.json.
// The pairing token is stored XOR-obfuscated (same scheme as the KOReader
// password); a plain "token" key written by hand on a PC is accepted and
// re-saved obfuscated.
class AgentMuxConfig : public PersistableStore<AgentMuxConfig> {
 public:
  static constexpr uint16_t DEFAULT_PORT = 7878;
  static constexpr size_t TOKEN_LEN = 8;
  static constexpr size_t HOST_MAX = 63;

  static const char* getFilePath() { return "/.crosspoint/agentmux.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::string& getHost() const { return host; }
  uint16_t getPort() const { return port; }
  const std::string& getToken() const { return token; }

  // Accepts "host" or "host:port"; an empty host means "discover via mDNS".
  void setHostPort(const std::string& hostPort);
  void setHost(const std::string& value, uint16_t newPort);
  void setToken(const std::string& value);

 private:
  std::string host;
  uint16_t port = DEFAULT_PORT;
  std::string token;

  AgentMuxConfig() = default;
  ~AgentMuxConfig() = default;
  friend class PersistableStore<AgentMuxConfig>;
};

}  // namespace agentmux

#define AGENTMUX_CONFIG agentmux::AgentMuxConfig::getInstance()

#endif  // AGENTMUX
