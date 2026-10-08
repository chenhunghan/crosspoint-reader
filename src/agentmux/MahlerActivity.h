#pragma once
#if AGENTMUX

#include <HalMemory.h>
#include <mahler_ui.h>

#include <atomic>
#include <cstdint>

#include "BridgeClient.h"
#include "activities/Activity.h"

namespace agentmux {

// The mahler screen: the sub-agent runs the bridge serves. The screen itself
// (layout, drawing, what a key or a tap does) is the mahler_ui Rust library,
// the same code the bridge draws the terminal's copy with; this activity
// feeds it the bridge's records and the person's input, puts its pixels on
// the panel, and sends the actions it asks for to the bridge. Holding Back
// leaves. Borrows the BridgeClient owned by AgentMuxActivity.
class MahlerActivity final : public Activity {
 public:
  MahlerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, BridgeClient& bridge);
  ~MahlerActivity() override;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  BridgeClient& bridge;
  MahlerUi* ui = nullptr;
  HalMemory::PsramBuffer levelsBuffer;
  bool pendingRedraw = false;
  std::atomic<unsigned long> lastRenderMs{0};
  unsigned long lastMinute = 0;
  // When the link to the bridge last went down (0 = up).
  unsigned long offlineSinceMs = 0;
  uint8_t fastRefreshes = 0;

  int64_t now() const;
  void tellLink();
  void apply(const MahlerEffect& effect);
};

}  // namespace agentmux

#endif  // AGENTMUX
