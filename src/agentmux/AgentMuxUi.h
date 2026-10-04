#pragma once
#if AGENTMUX

#include <cstdint>

class GfxRenderer;

namespace agentmux {

// UI strings. Literal English on purpose: adding keys to lib/I18n would widen
// this fork's diff against upstream translation files.
namespace text {
constexpr const char* APP_TITLE = "Agent Mux";
constexpr const char* SETTINGS_ROW = "Bridge settings...";
constexpr const char* SEARCHING = "Looking for an agentmux bridge on this network...";
constexpr const char* FOUND_PREFIX = "Found bridge: ";
constexpr const char* ENTER_HOST = "Bridge host[:port] (empty = auto-discover)";
constexpr const char* ENTER_TOKEN = "Pairing token (agentmux token)";
constexpr const char* NO_SESSIONS = "No agent sessions";
constexpr const char* WAITING_SCREEN = "Waiting for screen...";
constexpr const char* PERM_CAPTION = "Permission needed";
constexpr const char* REPLY = "Reply";
constexpr const char* REPLY_TITLE = "Reply to agent";
constexpr const char* ESC = "Esc";
constexpr const char* ENTER = "Enter";
constexpr const char* OPEN = "Open";
constexpr const char* NOT_SENT = "Not connected - not sent";
}  // namespace text

// Terminal text area of SessionActivity. The same geometry decides the
// cols/rows sent in hello, so the bridge wraps lines to what fits.
struct TermLayout {
  int x;
  int y;
  int width;
  int height;
  int lineHeight;
  uint16_t cols;
  uint16_t rows;
};

int termFontId();
int actionBarHeight();
TermLayout terminalLayout(const GfxRenderer& renderer);

}  // namespace agentmux

#endif  // AGENTMUX
