#pragma once
// Transport behind the simulated WebSocketsClient. Implemented per platform:
// platform/wasm_main.cpp (JavaScript: real WebSocket or the demo bridge) and
// platform/native_main.cpp (scripted fake bridge for snapshot scenarios).
#include <cstddef>

class WebSocketsClient;

// Starts connecting to url; returns a socket id (>= 0) or -1. The transport
// later calls owner->transportOpened()/transportMessage()/transportClosed().
int simSocketOpen(const char* url, WebSocketsClient* owner);
bool simSocketSend(int id, const char* data, size_t length);
// Closes without calling back into the owner.
void simSocketClose(int id);
