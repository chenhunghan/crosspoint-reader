#if AGENTMUX

#include "AgentMuxConfig.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstdlib>
#include <cstring>

namespace agentmux {

namespace {
std::string trimmed(const std::string& in) {
  const size_t begin = in.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const size_t end = in.find_last_not_of(" \t\r\n");
  return in.substr(begin, end - begin + 1);
}
}  // namespace

void AgentMuxConfig::toJson(JsonDocument& doc) const {
  doc["host"] = host;
  doc["port"] = port;
  doc["token_obf"] = obfuscation::obfuscateToBase64(token);
}

bool AgentMuxConfig::fromJson(JsonVariantConst doc) {
  host = trimmed(doc["host"] | "");
  if (host.size() > HOST_MAX) host.resize(HOST_MAX);
  const uint16_t p = doc["port"] | DEFAULT_PORT;
  port = p != 0 ? p : DEFAULT_PORT;

  const char* obf = doc["token_obf"] | "";
  if (obf[0] != '\0') {
    bool ok = false;
    token = obfuscation::deobfuscateFromBase64(obf, &ok);
    if (!ok) {
      LOG_ERR("AMUX", "Stored token could not be decoded; clearing it");
      token.clear();
    }
  } else {
    // Hand-edited config: plain token, re-saved obfuscated.
    const char* plain = doc["token"] | "";
    token = trimmed(plain);
    if (!token.empty()) requestResave();
  }
  if (token.size() > TOKEN_LEN) token.resize(TOKEN_LEN);
  return true;
}

void AgentMuxConfig::setHostPort(const std::string& hostPort) {
  std::string value = trimmed(hostPort);
  // Tolerate a pasted URL.
  for (const char* scheme : {"ws://", "http://"}) {
    const size_t len = strlen(scheme);
    if (value.compare(0, len, scheme) == 0) value.erase(0, len);
  }
  const size_t slash = value.find('/');
  if (slash != std::string::npos) value.resize(slash);

  uint16_t newPort = DEFAULT_PORT;
  const size_t colon = value.rfind(':');
  if (colon != std::string::npos) {
    const long parsed = strtol(value.c_str() + colon + 1, nullptr, 10);
    if (parsed > 0 && parsed <= 65535) newPort = static_cast<uint16_t>(parsed);
    value.resize(colon);
  }
  setHost(value, newPort);
}

void AgentMuxConfig::setHost(const std::string& value, const uint16_t newPort) {
  host = trimmed(value);
  if (host.size() > HOST_MAX) host.resize(HOST_MAX);
  port = newPort != 0 ? newPort : DEFAULT_PORT;
}

void AgentMuxConfig::setToken(const std::string& value) {
  token = trimmed(value);
  if (token.size() > TOKEN_LEN) token.resize(TOKEN_LEN);
}

}  // namespace agentmux

#endif  // AGENTMUX
