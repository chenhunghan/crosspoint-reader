// CrossPoint simulator: WebSocketsClient (links2004 API subset) over SimSocket.
#include <Logging.h>
#include <WebSocketsClient.h>

#include "SimSocket.h"

WebSocketsClient::WebSocketsClient() = default;

WebSocketsClient::~WebSocketsClient() {
  callback = nullptr;
  closeSocket();
}

void WebSocketsClient::begin(const char* host, const uint16_t port, const char* path, const char*) {
  closeSocket();
  events.clear();
  char buf[256];
  snprintf(buf, sizeof(buf), "ws://%s:%u%s", host ? host : "", port, path ? path : "/");
  url = buf;
  begun = true;
  state = State::Idle;
  lastAttemptMs = 0;
  connectNow();
}

void WebSocketsClient::connectNow() {
  lastAttemptMs = millis();
  socketId = simSocketOpen(url.c_str(), this);
  state = socketId >= 0 ? State::Connecting : State::Idle;
}

void WebSocketsClient::closeSocket() {
  if (socketId >= 0) simSocketClose(socketId);
  socketId = -1;
}

void WebSocketsClient::disconnect() {
  const bool wasOpen = state == State::Open;
  closeSocket();
  state = State::Idle;
  lastAttemptMs = millis();
  events.clear();
  if (wasOpen && callback) callback(WStype_DISCONNECTED, nullptr, 0);
}

bool WebSocketsClient::sendTXT(char* payload, size_t length, bool) {
  if (state != State::Open || socketId < 0 || !payload) return false;
  if (length == 0) length = strlen(payload);
  return simSocketSend(socketId, payload, length);
}

void WebSocketsClient::transportOpened() { events.push_back({WStype_CONNECTED, std::string("/device")}); }

void WebSocketsClient::transportMessage(const char* data, const size_t length) {
  events.push_back({WStype_TEXT, std::string(data, length)});
}

void WebSocketsClient::transportClosed() {
  socketId = -1;
  events.push_back({WStype_DISCONNECTED, std::string()});
}

void WebSocketsClient::loop() {
  // Deliver at most a handful of events per loop, like the real client which
  // reads one frame per loop() call.
  for (int budget = 0; budget < 8 && !events.empty(); ++budget) {
    Event ev = std::move(events.front());
    events.pop_front();
    if (ev.type == WStype_CONNECTED) {
      state = State::Open;
    } else if (ev.type == WStype_DISCONNECTED) {
      const bool wasOpen = state == State::Open;
      state = State::Idle;
      lastAttemptMs = millis();
      if (!wasOpen) continue;  // failed connect attempt: no event, like links2004
    }
    if (callback) {
      ev.payload.push_back('\0');
      callback(ev.type, reinterpret_cast<uint8_t*>(ev.payload.data()), ev.payload.size() - 1);
    }
    if (state == State::Idle && socketId < 0 && ev.type != WStype_DISCONNECTED) break;
  }
  if (begun && state == State::Idle && socketId < 0 && millis() - lastAttemptMs >= reconnectIntervalMs) {
    connectNow();
  }
}
