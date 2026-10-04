#pragma once
// CrossPoint simulator: the subset of links2004/WebSockets' WebSocketsClient
// that BridgeClient uses. Transport is pluggable (sim/src/SimSocket.h): the
// wasm build goes through the page's JavaScript (real WebSocket or the
// built-in demo bridge), the native build through a scripted fake bridge.
// Like the real library, events are only delivered from loop(), and a
// dropped/closed connection is retried every reconnect interval.

#include <Arduino.h>

#include <deque>
#include <functional>
#include <string>

typedef enum {
  WStype_ERROR,
  WStype_DISCONNECTED,
  WStype_CONNECTED,
  WStype_TEXT,
  WStype_BIN,
  WStype_FRAGMENT_TEXT_START,
  WStype_FRAGMENT_BIN_START,
  WStype_FRAGMENT,
  WStype_FRAGMENT_FIN,
  WStype_PING,
  WStype_PONG,
} WStype_t;

class WebSocketsClient {
 public:
  typedef std::function<void(WStype_t type, uint8_t* payload, size_t length)> WebSocketClientEvent;

  WebSocketsClient();
  ~WebSocketsClient();

  void begin(const char* host, uint16_t port, const char* url = "/", const char* protocol = "arduino");
  void begin(const String& host, uint16_t port, const String& url = "/", const String& protocol = "arduino") {
    begin(host.c_str(), port, url.c_str(), protocol.c_str());
  }
  void loop();
  void onEvent(WebSocketClientEvent cbEvent) { callback = std::move(cbEvent); }
  void disconnect();
  void setReconnectInterval(unsigned long time) { reconnectIntervalMs = time; }
  bool sendTXT(char* payload, size_t length = 0, bool headerToPayload = false);
  bool sendTXT(const char* payload, size_t length = 0) { return sendTXT(const_cast<char*>(payload), length); }
  bool sendTXT(String& payload) { return sendTXT(payload.c_str(), payload.length()); }
  bool isConnected() const { return state == State::Open; }
  void enableHeartbeat(uint32_t, uint32_t, uint8_t) {}

  // Called by the transport (any time); delivered from loop().
  void transportOpened();
  void transportMessage(const char* data, size_t length);
  void transportClosed();

 private:
  enum class State : uint8_t { Idle, Connecting, Open };
  struct Event {
    WStype_t type;
    std::string payload;
  };

  WebSocketClientEvent callback;
  std::deque<Event> events;
  std::string url;
  State state = State::Idle;
  bool begun = false;
  int socketId = -1;
  unsigned long reconnectIntervalMs = 500;
  unsigned long lastAttemptMs = 0;

  void connectNow();
  void closeSocket();
};
