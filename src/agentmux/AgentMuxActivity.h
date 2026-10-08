#pragma once
#if AGENTMUX

#include <atomic>
#include <cstdint>
#include <memory>

#include "AgentMuxUi.h"
#include "BridgeClient.h"
#include "activities/UiListActivity.h"

namespace agentmux {

// Agent Mux entry screen: brings up Wi-Fi, resolves the bridge (config,
// mDNS `_agentmux._tcp`, or keyboard entry), asks for the pairing token, then
// lists the bridge's sessions (blocked first). Owns the BridgeClient that
// SessionActivity borrows.
class AgentMuxActivity final : public UiListActivity {
 public:
  AgentMuxActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum class Phase : uint8_t { WaitWifi, Setup, Prompting, Running, Failed };

  std::unique_ptr<BridgeClient> bridge;
  Phase phase = Phase::Setup;
  bool tearDownWifiOnExit = false;
  bool discoveryTried = false;
  bool editHost = false;
  bool editToken = false;
  bool pendingRedraw = false;
  // The mahler screen opens by itself once, when the first run records
  // arrive; after the person leaves it, the "Mahler screen" row reopens it.
  bool panelOpened = false;
  bool hasPanelRow = false;
  std::atomic<unsigned long> lastRenderMs{0};
  TermLayout layout{};

  char status[96] = {0};      // centered message outside Running
  char notice[96] = {0};      // e.g. "Found bridge: ..." under the header
  char tokenTitle[64] = {0};  // keyboard title (mentions a rejected token)

  // Row cache, rebuilt on sessions frames (loop task, under the render lock).
  static constexpr int MAX_ROWS = MAX_SESSIONS + 2;  // + panel row + settings row
  int rowCount = 0;
  freeink::ui::ListItem rows[MAX_ROWS]{};
  char rowLabels[MAX_SESSIONS][40] = {};
  char rowSubtitles[MAX_SESSIONS][168] = {};
  char rowSids[MAX_SESSIONS][ID_BYTES] = {};
  char settingsSubtitle[96] = {0};
  char hostLine[96] = {0};

  int listCount() const override { return phase == Phase::Running ? rowCount : 0; }
  const char* headerTitle() const override { return text::APP_TITLE; }
  void drawChrome() override;
  void drawFooter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  void advanceSetup();
  void runDiscovery();
  void promptHost();
  void promptToken();
  void startBridge();
  void editSettings();
  void openSession(int index);
  void openPanel();
  void rebuildRows();
  void setStatus(const char* message);
};

// Settings -> System -> "Agent Mux" launch hook (SettingsActivity.cpp).
void openFromSettings(Activity& parent, GfxRenderer& renderer, MappedInputManager& mappedInput);

#if AGENTMUX_AUTOSTART
// Dev builds (env metalio_eink4_agentmux_dev): opens Agent Mux over Home once,
// a few seconds after boot, so the device is testable with nobody at it.
// Called from main.cpp's loop().
void autostartOnce(ActivityManager& activities, GfxRenderer& renderer, MappedInputManager& mappedInput);
#endif

}  // namespace agentmux

#endif  // AGENTMUX
