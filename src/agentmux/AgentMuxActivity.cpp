#if AGENTMUX

#include "AgentMuxActivity.h"

#include <ESPmDNS.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "AgentMuxConfig.h"
#include "MappedInputManager.h"
#include "SessionActivity.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace agentmux {

namespace {
constexpr unsigned long REDRAW_MIN_MS = 1500;
constexpr const char* MDNS_SELF_NAME = "crosspoint-agentmux";
constexpr size_t HOST_INPUT_MAX = 80;

int stateRank(const SessionState state) {
  switch (state) {
    case SessionState::Blocked:
      return 0;
    case SessionState::Working:
      return 1;
    case SessionState::Idle:
      return 2;
    case SessionState::Unknown:
      return 3;
    case SessionState::Exited:
    default:
      return 4;
  }
}
}  // namespace

void openFromSettings(Activity& parent, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  auto activity = makeUniqueNoThrow<AgentMuxActivity>(renderer, mappedInput);
  if (!activity) {
    LOG_ERR("AMUX", "OOM: AgentMuxActivity");
    return;
  }
  parent.startActivityForResult(std::move(activity), nullptr);
}

AgentMuxActivity::AgentMuxActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("AgentMux", renderer, mappedInput) {}

void AgentMuxActivity::setStatus(const char* message) { snprintf(status, sizeof(status), "%s", message); }

void AgentMuxActivity::onEnter() {
  UiListActivity::onEnter();
  layout = terminalLayout(renderer);
  rowCount = 0;
  notice[0] = '\0';
  tokenTitle[0] = '\0';
  discoveryTried = editHost = editToken = false;
  renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);

  bridge = makeUniqueNoThrow<BridgeClient>();
  if (!bridge || !bridge->init()) {
    LOG_ERR("AMUX", "OOM: BridgeClient");
    bridge.reset();
    phase = Phase::Failed;
    setStatus("Out of memory");
    return;
  }

  {
    RenderLock lock(*this);
    AGENTMUX_CONFIG.loadFromFile();
  }

  setStatus(tr(STR_CONNECTING));
  if (WiFi.status() == WL_CONNECTED) {
    phase = Phase::Setup;
    return;
  }

  tearDownWifiOnExit = true;
  phase = Phase::WaitWifi;
  LOG_INF("AMUX", "No WiFi, launching WiFi selection");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             finish();
                             return;
                           }
                           renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
                           phase = Phase::Setup;
                         });
}

void AgentMuxActivity::onExit() {
  // Runs with the render lock held: nothing below may take it.
  if (bridge) {
    bridge->stop();
    bridge.reset();
  }
  Activity::onExit();

  if (tearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void AgentMuxActivity::loop() {
  switch (phase) {
    case Phase::Setup:
      advanceSetup();
      return;
    case Phase::Running:
      break;
    case Phase::Failed:
      UiListActivity::loop();  // Back exits
      return;
    default:
      return;  // a sub-activity is on top
  }

  bridge->loop();
  const uint8_t dirty = bridge->consumeDirty();
  if (dirty & BridgeClient::DIRTY_SESSIONS) {
    RenderLock lock(*this);
    rebuildRows();
  }
  if (dirty & (BridgeClient::DIRTY_SESSIONS | BridgeClient::DIRTY_LINK)) pendingRedraw = true;
  if ((dirty & BridgeClient::DIRTY_LINK) && bridge->link() == LinkState::AuthFailed) {
    snprintf(tokenTitle, sizeof(tokenTitle), "Token rejected - %s", text::ENTER_TOKEN);
    editToken = true;
    phase = Phase::Setup;
    return;
  }
  if (pendingRedraw && millis() - lastRenderMs.load() >= REDRAW_MIN_MS) {
    pendingRedraw = false;
    requestUpdate();
  }

  UiListActivity::loop();
}

void AgentMuxActivity::render(RenderLock&& lock) {
  UiListActivity::render(std::move(lock));
  lastRenderMs = millis();
}

void AgentMuxActivity::advanceSetup() {
  auto& cfg = AGENTMUX_CONFIG;
  if (cfg.getHost().empty() && !discoveryTried && !editHost) {
    runDiscovery();
    return;
  }
  if (cfg.getHost().empty() || editHost) {
    promptHost();
    return;
  }
  if (cfg.getToken().empty() || editToken) {
    promptToken();
    return;
  }

  {
    RenderLock lock(*this);
    if (!cfg.saveToFile()) LOG_ERR("AMUX", "Failed to save config");
  }
  startBridge();
}

void AgentMuxActivity::runDiscovery() {
  discoveryTried = true;
  setStatus(text::SEARCHING);
  requestUpdateAndWait();

  if (!MDNS.begin(MDNS_SELF_NAME)) {
    LOG_ERR("AMUX", "mDNS start failed");
    return;
  }
  const int found = MDNS.queryService("agentmux", "tcp");
  LOG_INF("AMUX", "mDNS _agentmux._tcp: %d result(s)", found);
  if (found > 0) {
    const String ip = MDNS.address(0).toString();
    const String name = MDNS.hostname(0);
    const uint16_t port = MDNS.port(0);
    AGENTMUX_CONFIG.setHost(ip.c_str(), port);
    snprintf(notice, sizeof(notice), "%s%s (%s:%u)", text::FOUND_PREFIX, name.c_str(), ip.c_str(), port);
    LOG_INF("AMUX", "%s", notice);
  }
  MDNS.end();
}

void AgentMuxActivity::promptHost() {
  auto& cfg = AGENTMUX_CONFIG;
  char initial[HOST_INPUT_MAX] = {0};
  if (!cfg.getHost().empty()) {
    if (cfg.getPort() == AgentMuxConfig::DEFAULT_PORT) {
      snprintf(initial, sizeof(initial), "%s", cfg.getHost().c_str());
    } else {
      snprintf(initial, sizeof(initial), "%s:%u", cfg.getHost().c_str(), cfg.getPort());
    }
  }
  phase = Phase::Prompting;
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, text::ENTER_HOST, initial,
                                                                 HOST_INPUT_MAX, InputType::Url),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             // Cancelling an edit keeps the old bridge; cancelling first-time setup leaves.
                             editHost = editToken = false;
                             if (AGENTMUX_CONFIG.getHost().empty() || AGENTMUX_CONFIG.getToken().empty()) {
                               finish();
                             } else {
                               phase = Phase::Setup;
                             }
                             return;
                           }
                           AGENTMUX_CONFIG.setHostPort(std::get<KeyboardResult>(result.data).text);
                           editHost = false;
                           // An empty host means "discover again".
                           discoveryTried = !AGENTMUX_CONFIG.getHost().empty();
                           notice[0] = '\0';
                           phase = Phase::Setup;
                         });
}

void AgentMuxActivity::promptToken() {
  phase = Phase::Prompting;
  const char* title = tokenTitle[0] ? tokenTitle : text::ENTER_TOKEN;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, title, AGENTMUX_CONFIG.getToken(),
                                              AgentMuxConfig::TOKEN_LEN, InputType::Text),
      [this](const ActivityResult& result) {
        tokenTitle[0] = '\0';
        editToken = false;
        if (result.isCancelled) {
          if (AGENTMUX_CONFIG.getToken().empty() || (bridge && bridge->link() == LinkState::AuthFailed)) {
            finish();
          } else {
            phase = Phase::Setup;
          }
          return;
        }
        AGENTMUX_CONFIG.setToken(std::get<KeyboardResult>(result.data).text);
        phase = Phase::Setup;
      });
}

void AgentMuxActivity::startBridge() {
  const auto& cfg = AGENTMUX_CONFIG;
  snprintf(hostLine, sizeof(hostLine), "%s:%u", cfg.getHost().c_str(), cfg.getPort());
  snprintf(settingsSubtitle, sizeof(settingsSubtitle), "%s", hostLine);
  bridge->begin(cfg.getHost().c_str(), cfg.getPort(), cfg.getToken().c_str(), layout.cols, layout.rows);
  {
    RenderLock lock(*this);
    rebuildRows();
  }
  phase = Phase::Running;
  nav.selected = 0;
  requestUpdate();
}

void AgentMuxActivity::editSettings() {
  if (bridge) bridge->stop();
  editHost = editToken = true;
  phase = Phase::Setup;
  setStatus(tr(STR_CONNECTING));
}

void AgentMuxActivity::openSession(const int index) {
  auto activity = makeUniqueNoThrow<SessionActivity>(renderer, mappedInput, *bridge, rowSids[index]);
  if (!activity) {
    LOG_ERR("AMUX", "OOM: SessionActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    // The session screen consumed the bridge's dirty bits; refresh rows now.
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    RenderLock lock(*this);
    rebuildRows();
  });
}

void AgentMuxActivity::activateIndex(const int index) {
  if (phase != Phase::Running || index < 0 || index >= rowCount) return;
  app.clearTapFlash();  // both targets leave this screen
  if (index == rowCount - 1) {
    editSettings();
  } else {
    openSession(index);
  }
}

void AgentMuxActivity::rebuildRows() {
  const Model& m = bridge->model();
  int order[MAX_SESSIONS];
  const int count = m.sessionCount;
  for (int i = 0; i < count; ++i) order[i] = i;
  // Stable insertion sort by state rank: blocked first, exited last.
  for (int i = 1; i < count; ++i) {
    const int v = order[i];
    int j = i - 1;
    while (j >= 0 && stateRank(m.sessions[order[j]].state) > stateRank(m.sessions[v].state)) {
      order[j + 1] = order[j];
      --j;
    }
    order[j + 1] = v;
  }

  for (int i = 0; i < count; ++i) {
    const Session& s = m.sessions[order[i]];
    snprintf(rowLabels[i], sizeof(rowLabels[i]), "%s %s", BridgeClient::stateLabel(s.state),
             s.agent[0] ? s.agent : "agent");
    if (s.title[0] && s.cwd[0]) {
      snprintf(rowSubtitles[i], sizeof(rowSubtitles[i]), "%s - %s", s.title, s.cwd);
    } else {
      snprintf(rowSubtitles[i], sizeof(rowSubtitles[i]), "%s", s.title[0] ? s.title : s.cwd);
    }
    snprintf(rowSids[i], sizeof(rowSids[i]), "%s", s.sid);
    fui::ListItem item;
    item.label = rowLabels[i];
    item.subtitle = rowSubtitles[i][0] ? rowSubtitles[i] : nullptr;
    item.actionValue = static_cast<int16_t>(i);
    rows[i] = item;
  }

  fui::ListItem settings;
  settings.label = text::SETTINGS_ROW;
  settings.subtitle = settingsSubtitle[0] ? settingsSubtitle : nullptr;
  settings.actionValue = static_cast<int16_t>(count);
  if (count == 0) settings.sectionHeading = text::NO_SESSIONS;
  rows[count] = settings;
  rowCount = count + 1;
  if (nav.selected >= rowCount) nav.selected = rowCount - 1;
}

void AgentMuxActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const char* linkText = bridge ? BridgeClient::linkLabel(bridge->link()) : "";
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, text::APP_TITLE,
                 phase == Phase::Running ? linkText : nullptr);

  const char* left = notice[0] ? notice : hostLine;
  const char* right = nullptr;
  if (bridge && phase == Phase::Running) {
    const Model& m = bridge->model();
    right = m.lastError[0] ? m.lastError : (m.bridgeHost[0] ? m.bridgeHost : nullptr);
  }
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, width, metrics.tabBarHeight}, left,
                    right);
}

void AgentMuxActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), phase == Phase::Running ? text::OPEN : "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void AgentMuxActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  if (phase != Phase::Running) {
    fui::TextStyle style = screen.theme().bodyText;
    style.align = fui::TextAlign::Center;
    style.maxLines = 3;
    screen.centeredText(status, style);
    return;
  }

  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

}  // namespace agentmux

#endif  // AGENTMUX
