// CrossPoint simulator: link-level stand-ins for firmware pieces the Agent
// Mux screens reference but never exercise in the simulator (SD-card/TTF
// fonts, KOReader sync, RTC, IMU, power, PSRAM, WiFi selection, restarts).
// Declarations come from the real headers; only the bodies are simulated.
#include <FontAlloc.h>
#include <FtFont.h>
#include <HalClock.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalTiltSensor.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>
#include <MemoryManager.h>
#include <TtfEpdFont.h>

#include <cstdlib>
#include <ctime>

#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"

// --- HAL singletons ----------------------------------------------------------
HalClock halClock;
HalTiltSensor halTiltSensor;
HalPowerManager powerManager;

bool HalClock::formatTime(char* buf, const size_t bufSize, const bool use12Hour) const {
  const time_t now = time(nullptr);
  struct tm tmv {};
  localtime_r(&now, &tmv);
  if (use12Hour) {
    const int h = tmv.tm_hour % 12 == 0 ? 12 : tmv.tm_hour % 12;
    snprintf(buf, bufSize, "%d:%02d %s", h, tmv.tm_min, tmv.tm_hour < 12 ? "AM" : "PM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  }
  return true;
}

uint16_t HalPowerManager::getBatteryPercentage() const { return 87; }

void HalMemory::PsramDeleter::operator()(uint8_t* buffer) const { free(buffer); }
HalMemory::PsramBuffer HalMemory::allocatePsram(const size_t bytes) {
  return PsramBuffer(static_cast<uint8_t*>(malloc(bytes)));
}
HalMemory::HeapStats HalMemory::getDefaultHeap() { return {256 * 1024, 320 * 1024, 200 * 1024, 128 * 1024}; }
HalMemory::HeapStats HalMemory::getInternalHeap() { return getDefaultHeap(); }
HalMemory::HeapStats HalMemory::getPsramHeap() {
  return {6 * 1024 * 1024, 8 * 1024 * 1024, 6 * 1024 * 1024, 4 * 1024 * 1024};
}

// --- FreeInk SDK memory manager (no cache sinks to drive in the simulator) ---
freeink::MemoryManager& freeink::MemoryManager::instance() {
  static MemoryManager manager;
  return manager;
}
int freeink::MemoryManager::registerSink(const CacheSink&) { return 0; }
size_t freeink::MemoryManager::freeBytes(MemPool) const { return 4 * 1024 * 1024; }

void* fiFontMalloc(const size_t size) { return malloc(size); }
void fiFontFree(void* ptr) { free(ptr); }

// --- TTF fonts: none are loaded in the simulator ------------------------------
void TtfEpdFont::clearCache() {}
void TtfEpdFont::releaseResidentCaches() {}
bool TtfEpdFont::build(const char*) { return false; }
bool TtfEpdFont::addCoverage(const char*) { return false; }

// No FreeType in the simulator: SD-card TTF discovery reports "unavailable".
freeink::font::FtFont::InspectResult freeink::font::FtFont::inspectStream(ReadFn, void*, unsigned long, FaceInfo&,
                                                                          char*, size_t) {
  return InspectResult::Unavailable;
}

// --- KOReader sync settings (only touched by settings migration) ------------
void KOReaderCredentialStore::toJson(JsonDocument&) const {}
void KOReaderCredentialStore::setCredentials(const std::string&, const std::string&) {}
void KOReaderCredentialStore::setServerUrl(const std::string&) {}
void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod) {}
void KOReaderCredentialStore::setSendMetadata(bool) {}
void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior) {}

// --- Restarts -----------------------------------------------------------------
// WiFi screens reboot on exit to defragment the heap; the simulator goes Home.
void silentRestart() {
  LOG_INF("SIM", "silentRestart() -> Home");
  activityManager.goHome();
}
void silentRestartToReader() { silentRestart(); }
void silentRestartToSettings() { silentRestart(); }
void silentRestartToJoinNetwork() {}

// --- WiFi selection: WiFi is always connected in the simulator --------------
WifiSelectionActivity::WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const bool autoConnect)
    : Activity("WifiSelection", renderer, mappedInput), UiAppHost(renderer), allowAutoConnect(autoConnect) {}
void WifiSelectionActivity::onEnter() {
  Activity::onEnter();
  WifiResult result;
  result.connected = true;
  result.ssid = "SimulatorLAN";
  result.ip = "192.168.1.77";
  setResult(std::move(result));
  finish();
}
void WifiSelectionActivity::onExit() { Activity::onExit(); }
void WifiSelectionActivity::loop() {}
void WifiSelectionActivity::render(RenderLock&&) {}

// --- uzlib checksums (the firmware links these away; tinflate references them)
extern "C" uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prev_sum) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint32_t a = prev_sum & 0xFFFF, b = prev_sum >> 16;
  for (unsigned int i = 0; i < length; ++i) {
    a = (a + p[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}
extern "C" uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (unsigned int i = 0; i < length; ++i) {
    crc ^= p[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return crc;
}
