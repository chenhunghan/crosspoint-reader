#if AGENTMUX

#include "MahlerActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "util/ButtonNavigator.h"

namespace agentmux {

namespace {

// No sooner than this after the last redraw; e-ink cannot keep up with more.
constexpr unsigned long REDRAW_MIN_MS = 300;
// Holding Back this long leaves the screen; a press is the screen's Back.
constexpr unsigned long LEAVE_HOLD_MS = 700;
// Partial updates between cleaning refreshes, as a reader turns pages.
constexpr uint8_t FAST_BETWEEN_CLEAN = 30;

}  // namespace

MahlerActivity::MahlerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, BridgeClient& bridge)
    : Activity("Mahler", renderer, mappedInput), bridge(bridge) {}

MahlerActivity::~MahlerActivity() {
  // A conversation followed here is no one's once the screen is gone.
  bridge.followConvo("", -1);
  mahler_ui_free(ui);
}

void MahlerActivity::onEnter() {
  Activity::onEnter();
  ui = mahler_ui_new();
  levelsBuffer = HalMemory::allocatePsram(MAHLER_WIDTH * MAHLER_HEIGHT);
  const std::string& frame = bridge.runsFrame();
  if (!frame.empty()) mahler_ui_set_runs(ui, frame.data(), frame.size());
  tellLink();
  renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  requestUpdate();
}

// The bridge's clock: when it read the records, plus the time since.
int64_t MahlerActivity::now() const {
  const int64_t at = mahler_ui_records_at(ui);
  return at == 0 ? 0 : at + static_cast<int64_t>(bridge.runsAgeMs() / 1000);
}

// Off the bridge the records stop changing here though they may change there:
// the screen says "offline" and for how long, until the bridge sends them again.
void MahlerActivity::tellLink() {
  const bool online = bridge.link() == LinkState::Online;
  if (online) {
    offlineSinceMs = 0;
  } else if (offlineSinceMs == 0) {
    offlineSinceMs = millis() | 1;
  }
  const uint32_t minutes = online ? 0 : static_cast<uint32_t>((millis() - offlineSinceMs) / 60000);
  mahler_ui_set_link(ui, online, minutes, BridgeClient::linkLabel(bridge.link()));
}

void MahlerActivity::apply(const MahlerEffect& effect) {
  switch (effect.kind) {
    case MAHLER_EFFECT_ACT:
      if (!bridge.sendAct(effect.name, effect.action)) {
        RenderLock lock(*this);
        mahler_ui_set_act_result(ui, false, "Not connected to the bridge");
      }
      break;
    case MAHLER_EFFECT_REPORT:
      bridge.requestReport(effect.name);
      break;
    case MAHLER_EFFECT_CONVO:
      bridge.followConvo(effect.name, effect.end);
      break;
    default:
      break;
  }
  if (effect.kind != MAHLER_EFFECT_NONE) pendingRedraw = true;
}

void MahlerActivity::loop() {
  bridge.loop();
  const uint8_t dirty = bridge.consumeDirty();
  if (dirty & (BridgeClient::DIRTY_RUNS | BridgeClient::DIRTY_LINK)) {
    RenderLock lock(*this);
    const std::string& frame = bridge.runsFrame();
    if ((dirty & BridgeClient::DIRTY_RUNS) && !frame.empty()) mahler_ui_set_runs(ui, frame.data(), frame.size());
    if (dirty & BridgeClient::DIRTY_RUNS) mahler_ui_set_act_result(ui, bridge.lastActOk(), bridge.lastAct().c_str());
    tellLink();
    pendingRedraw = true;
  }
  if (dirty & BridgeClient::DIRTY_CONVO) {
    RenderLock lock(*this);
    const std::string& frame = bridge.convoFrame();
    if (!frame.empty()) mahler_ui_set_convo(ui, frame.data(), frame.size());
    pendingRedraw = true;
  }
  if (dirty & BridgeClient::DIRTY_REPORT) {
    RenderLock lock(*this);
    const std::string& text = bridge.reportText();
    mahler_ui_set_report(ui, bridge.reportName().c_str(), text.data(), text.size());
    pendingRedraw = true;
  }
  // The clock, the times and how long it has been offline move once a minute.
  const unsigned long minute = static_cast<unsigned long>(now() / 60);
  if (minute != lastMinute) {
    lastMinute = minute;
    RenderLock lock(*this);
    tellLink();
    pendingRedraw = true;
  }
  if (pendingRedraw && millis() - lastRenderMs.load() >= REDRAW_MIN_MS) {
    pendingRedraw = false;
    requestUpdate();
  }

  if (mappedInput.wasLongPressed(MappedInputManager::Button::Back, LEAVE_HOLD_MS)) {
    finish();
    return;
  }
  auto press = [&](const uint8_t key) {
    MahlerEffect effect;
    {
      RenderLock lock(*this);
      effect = mahler_ui_press(ui, key);
    }
    apply(effect);
  };
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) return press(MAHLER_KEY_BACK);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return press(MAHLER_KEY_OK);
  bool moved = false;
  ButtonNavigator::onPreviousPress([&] {
    press(MAHLER_KEY_UP);
    moved = true;
  });
  if (moved) return;
  ButtonNavigator::onNextPress([&] { press(MAHLER_KEY_DOWN); });

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    MahlerEffect effect;
    {
      RenderLock lock(*this);
      effect = mahler_ui_tap(ui, x, y);
    }
    apply(effect);
  }
}

void MahlerActivity::render(RenderLock&&) {
  lastRenderMs = millis();
  uint8_t* levels = static_cast<uint8_t*>(levelsBuffer.get());
  if (!levels || !ui) {
    LOG_ERR("AMUX", "OOM: mahler screen");
    return;
  }
  mahler_ui_render(ui, now(), levels);
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  for (int y = 0; y < MAHLER_HEIGHT; ++y) {
    const uint8_t* row = levels + y * MAHLER_WIDTH;
    for (int x = 0; x < MAHLER_WIDTH; ++x) {
      if (row[x] <= 1) renderer.drawPixel(x, y, true);
    }
  }
  // Ink and paper only: the panel updates in place, cleaning now and then.
  const bool clean = ++fastRefreshes >= FAST_BETWEEN_CLEAN;
  if (clean) fastRefreshes = 0;
  renderer.displayBuffer(clean ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}

}  // namespace agentmux

#endif  // AGENTMUX
