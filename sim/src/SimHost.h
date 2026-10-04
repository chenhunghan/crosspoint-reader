#pragma once
// Platform hooks the shared simulator core calls (implemented in
// platform/native_main.cpp and platform/wasm_main.cpp).
#include <cstdint>

namespace sim {
// One log line (or fragment) from LOG_* / Serial.
void hostLog(const char* text);
// ESP.restart(): restart the simulated firmware.
[[noreturn]] void hostRestart();
// The panel finished a refresh (mode = HalDisplay::RefreshMode).
void hostFramePresented(int refreshMode);
// A file under the simulated SD card changed (wasm persists to IndexedDB).
void hostStorageChanged();
}  // namespace sim
