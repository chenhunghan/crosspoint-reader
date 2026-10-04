#pragma once
#if AGENTMUX

#include <atomic>
#include <cstdint>

#include "AgentMuxUi.h"
#include "BridgeClient.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

namespace agentmux {

// One agent session: the bridge's pre-wrapped terminal lines, a Reply/Esc/
// Enter action bar, and the permission dialog while the session is blocked.
// Borrows the BridgeClient owned by AgentMuxActivity (below it on the stack).
class SessionActivity final : public Activity, private UiAppHost {
 public:
  SessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, BridgeClient& bridge, const char* sid);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  BridgeClient& bridge;
  char sid[ID_BYTES] = {0};
  TermLayout layout{};

  int permSelection = 0;
  char permReq[ID_BYTES] = {0};  // req the selection belongs to
  SessionState lastState = SessionState::Unknown;

  bool awaitingKeyboard = false;
  bool pendingRedraw = false;
  std::atomic<unsigned long> lastRenderMs{0};
  uint8_t fastRefreshes = 0;
  char notice[48] = {0};

  // Render-task scratch, kept off the stack.
  char headerTitle[112] = {0};
  char permMessage[MAX_PERM_DETAIL * LINE_BYTES] = {0};

  static void screenTrampoline(UiScreen& screen, void* user);
  static void onBarEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onPermEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void buildPermDialog(UiScreen& screen, const Perm& perm);
  void drawTerminal() const;

  void pollBridge();
  bool handlePermButtons(const Perm& perm);
  void choosePermOption(int index);
  void sendKey(const char* key);
  void openReply();
};

}  // namespace agentmux

#endif  // AGENTMUX
