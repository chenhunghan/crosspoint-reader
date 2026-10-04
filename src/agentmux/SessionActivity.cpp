#if AGENTMUX

#include "SessionActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

namespace fui = freeink::ui;

namespace agentmux {

namespace {
constexpr fui::ActionId ACTION_BAR = 1;
constexpr fui::ActionId ACTION_PERM = 2;

// E-ink: coalesce bridge-driven repaints to at most one per interval.
constexpr unsigned long REDRAW_MIN_MS = 1500;
// Fast refreshes between ghost-clearing half refreshes.
constexpr uint8_t FAST_REFRESHES_PER_HALF = 30;
constexpr size_t REPLY_MAX_BYTES = 800;

enum BarButton : int16_t { BAR_ESC = 0, BAR_ENTER = 1, BAR_REPLY = 2, BAR_COUNT = 3 };
constexpr const char* BAR_LABELS[BAR_COUNT] = {text::ESC, text::ENTER, text::REPLY};
}  // namespace

SessionActivity::SessionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, BridgeClient& bridge,
                                 const char* sessionId)
    : Activity("AgentMuxSession", renderer, mappedInput), UiAppHost(renderer), bridge(bridge) {
  snprintf(sid, sizeof(sid), "%s", sessionId ? sessionId : "");
}

void SessionActivity::onEnter() {
  Activity::onEnter();
  layout = terminalLayout(renderer);
  permSelection = 0;
  permReq[0] = '\0';
  notice[0] = '\0';
  pendingRedraw = false;
  fastRefreshes = 0;
  if (const Session* s = bridge.findSession(sid)) lastState = s->state;

  resetUi();
  app.on(ACTION_BAR, &SessionActivity::onBarEvent, this);
  app.on(ACTION_PERM, &SessionActivity::onPermEvent, this);
  app.setScreen(&SessionActivity::screenTrampoline, this);

  LOG_INF("AMUX", "Open session %s (%ux%u)", sid, layout.cols, layout.rows);
  bridge.subscribe(sid);
  renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
  requestUpdate();
}

void SessionActivity::onExit() {
  // Runs with the render lock held; subscribe("") does not lock.
  bridge.subscribe("");
  Activity::onExit();
}

void SessionActivity::pollBridge() {
  bridge.loop();
  const uint8_t dirty = bridge.consumeDirty();
  if (dirty & (BridgeClient::DIRTY_SCREEN | BridgeClient::DIRTY_LINK)) pendingRedraw = true;
  if (dirty & BridgeClient::DIRTY_PERM) {
    const Perm* perm = bridge.findPerm(sid);
    const char* req = perm ? perm->req : "";
    if (strcmp(req, permReq) != 0) {
      snprintf(permReq, sizeof(permReq), "%s", req);
      permSelection = 0;
      pendingRedraw = true;
    }
  }
  if (dirty & BridgeClient::DIRTY_SESSIONS) {
    const Session* s = bridge.findSession(sid);
    const SessionState state = s ? s->state : SessionState::Exited;
    if (state != lastState) {
      lastState = state;
      pendingRedraw = true;
    }
  }
  if (pendingRedraw && millis() - lastRenderMs.load() >= REDRAW_MIN_MS) {
    pendingRedraw = false;
    requestUpdate();
  }
}

void SessionActivity::loop() {
  pollBridge();

  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return;  // dispatched to onBarEvent / onPermEvent

  if (const Perm* perm = bridge.findPerm(sid)) {
    if (handlePermButtons(*perm)) return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openReply();
    return;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    sendKey("esc");
    return;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    sendKey("enter");
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

bool SessionActivity::handlePermButtons(const Perm& perm) {
  const int count = perm.optionCount;
  if (count == 0) return false;
  bool handled = false;
  ButtonNavigator::onNextPress([&] {
    permSelection = ButtonNavigator::nextIndex(permSelection, count);
    handled = true;
  });
  ButtonNavigator::onPreviousPress([&] {
    permSelection = ButtonNavigator::previousIndex(permSelection, count);
    handled = true;
  });
  if (handled) {
    requestUpdate();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    choosePermOption(permSelection);
    return true;
  }
  return false;
}

void SessionActivity::choosePermOption(const int index) {
  const Perm* perm = bridge.findPerm(sid);
  if (!perm || index < 0 || index >= perm->optionCount) return;
  // decide() clears the dialog in the model; copy what the frame needs first.
  char req[ID_BYTES];
  char key[sizeof(PermOption::key)];
  snprintf(req, sizeof(req), "%s", perm->req);
  snprintf(key, sizeof(key), "%s", perm->options[index].key);
  if (bridge.decide(sid, req, key)) {
    notice[0] = '\0';
  } else {
    snprintf(notice, sizeof(notice), "%s", text::NOT_SENT);
  }
  requestUpdate();
}

void SessionActivity::sendKey(const char* key) {
  if (bridge.sendKey(sid, key)) {
    notice[0] = '\0';
    LOG_DBG("AMUX", "keys %s -> %s", key, sid);
  } else {
    snprintf(notice, sizeof(notice), "%s", text::NOT_SENT);
    requestUpdate();
  }
}

void SessionActivity::openReply() {
  app.clearTapFlash();  // the keyboard replaces this screen
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, text::REPLY_TITLE, "",
                                                           REPLY_MAX_BYTES, InputType::Text);
  if (!keyboard) {
    LOG_ERR("AMUX", "OOM: reply keyboard");
    return;
  }
  awaitingKeyboard = true;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    awaitingKeyboard = false;
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    if (result.isCancelled) return;
    const std::string& reply = std::get<KeyboardResult>(result.data).text;
    if (reply.empty()) return;
    if (bridge.sendInput(sid, reply.c_str(), true)) {
      notice[0] = '\0';
      LOG_INF("AMUX", "input %u bytes -> %s", static_cast<unsigned>(reply.size()), sid);
    } else {
      snprintf(notice, sizeof(notice), "%s", text::NOT_SENT);
    }
  });
}

void SessionActivity::onBarEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SessionActivity*>(user);
  switch (event.value) {
    case BAR_ESC:
      self->sendKey("esc");
      break;
    case BAR_ENTER:
      self->sendKey("enter");
      break;
    case BAR_REPLY:
      self->openReply();
      break;
    default:
      break;
  }
}

void SessionActivity::onPermEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SessionActivity*>(user);
  self->permSelection = event.value;
  self->choosePermOption(event.value);
}

void SessionActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<SessionActivity*>(user)->buildScreen(screen);
}

void SessionActivity::buildScreen(UiScreen& screen) {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(layout.y), static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  if (const Perm* perm = bridge.findPerm(sid)) {
    buildPermDialog(screen, *perm);
    return;
  }

  const int16_t barHeight = static_cast<int16_t>(actionBarHeight() - 8);
  const fui::Rect bar = screen.takeBottom(barHeight);
  constexpr int16_t gap = 8;
  const int16_t sidePad = static_cast<int16_t>(layout.x - safe.x);
  const int16_t width = static_cast<int16_t>((bar.width - 2 * sidePad - gap * (BAR_COUNT - 1)) / BAR_COUNT);
  for (int16_t i = 0; i < BAR_COUNT; ++i) {
    fui::ButtonProps props;
    props.label = BAR_LABELS[i];
    props.action = ACTION_BAR;
    props.value = i;
    props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
    const fui::Rect rect{static_cast<int16_t>(bar.x + sidePad + i * (width + gap)), bar.y, width, bar.height};
    screen.button(props, rect);
  }
}

void SessionActivity::buildPermDialog(UiScreen& screen, const Perm& perm) {
  fui::DialogOption options[MAX_PERM_OPTIONS];
  for (int i = 0; i < perm.optionCount; ++i) {
    options[i].label = perm.options[i].label;
    options[i].action = ACTION_PERM;
    options[i].value = static_cast<int16_t>(i);
    options[i].state = i == permSelection ? fui::StateFocused : fui::StateNormal;
  }

  fui::OptionDialogProps props;
  props.title = text::PERM_CAPTION;
  props.headline = perm.title[0] ? perm.title : nullptr;
  // Detail lines arrive pre-wrapped. The dialog's message slot wraps on width only
  // (newlines are not line breaks there), so draw them one per row in the content band.
  const fui::TextStyle detailText = screen.theme().smallText;
  const int16_t detailLineH = screen.target().lineHeight(detailText.font);
  props.contentHeight = static_cast<int16_t>(perm.detailCount * detailLineH);
  props.options = options;
  props.optionCount = perm.optionCount;
  props.verticalOptions = true;
  props.titleText = screen.theme().smallText;
  props.titleText.bold = true;
  props.headlineText = screen.theme().bodyText;
  props.headlineText.bold = true;
  props.headlineText.maxLines = 2;
  props.buttonText = screen.theme().smallText;
  props.buttonText.maxLines = 2;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()

  const auto& metrics = UITheme::getInstance().getMetrics();
  props.styles = fui::defaultPopupStyles();
  props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = props.styles.normal;
  props.styles.focused = props.styles.normal;
  props.styles.active = props.styles.normal;
  props.styles.disabled = props.styles.normal;

  const fui::Rect body = screen.body();
  const int16_t height = fui::optionDialogHeight(screen.target(), props, body.width);
  const fui::Rect content =
      fui::optionDialog(screen.frame(), fui::centeredRect(body, fui::Size{body.width, height}), props);
  for (int i = 0; i < perm.detailCount; ++i) {
    const fui::Rect row{content.x, static_cast<int16_t>(content.y + i * detailLineH), content.width, detailLineH};
    screen.target().text(row, perm.detail[i], detailText);
  }
}

void SessionActivity::drawTerminal() const {
  const ScreenBuffer& scr = bridge.model().screen;
  const int font = termFontId();
  if (strcmp(scr.sid, sid) != 0 || scr.lineCount == 0) {
    const Rect area{layout.x, layout.y, layout.width, layout.height};
    UITheme::drawCenteredText(renderer, area, font, layout.y + layout.height / 2, text::WAITING_SCREEN);
    return;
  }
  const int fit = layout.height / layout.lineHeight;
  const int first = scr.lineCount > fit ? scr.lineCount - fit : 0;
  int y = layout.y;
  for (int i = first; i < scr.lineCount; ++i) {
    if (scr.lines[i][0] != '\0') renderer.drawText(font, layout.x, y, scr.lines[i]);
    y += layout.lineHeight;
  }
}

void SessionActivity::render(RenderLock&&) {
  if (awaitingKeyboard) return;  // returning from the keyboard; its result handler repaints

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Session* session = bridge.findSession(sid);
  if (session) {
    const char* name = session->title[0] ? session->title : session->cwd;
    snprintf(headerTitle, sizeof(headerTitle), "%s: %s", session->agent[0] ? session->agent : "agent",
             name[0] ? name : sid);
  } else {
    snprintf(headerTitle, sizeof(headerTitle), "%s", sid);
  }
  const char* status = notice[0] ? notice
                       : bridge.link() != LinkState::Online
                           ? BridgeClient::linkLabel(bridge.link())
                           : BridgeClient::stateLabel(session ? session->state : SessionState::Exited);

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, headerTitle,
                 status);
  drawTerminal();
  renderUi();

  const bool perm = bridge.findPerm(sid) != nullptr;
  const auto labels = perm ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN))
                           : mappedInput.mapLabels(tr(STR_BACK), text::REPLY, text::ESC, text::ENTER);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (++fastRefreshes >= FAST_REFRESHES_PER_HALF) {
    fastRefreshes = 0;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  lastRenderMs = millis();
}

}  // namespace agentmux

#endif  // AGENTMUX
