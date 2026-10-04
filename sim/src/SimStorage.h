#pragma once
// Root directory that stands in for the SD card (native: sim/out/sdcard,
// wasm: /sd on an IndexedDB-backed filesystem).
namespace sim {
void setStorageRoot(const char* hostPath);
const char* storageRoot();
}  // namespace sim
