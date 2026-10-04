#if AGENTMUX

#include "AgentMuxUi.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>

#include "BridgeClient.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace agentmux {

namespace {
// Height of SessionActivity's action-button band (buttons + gap).
constexpr int ACTION_BAR_HEIGHT = 52;
// Representative terminal text for the average glyph advance of the
// proportional UI font; lines wrapped to the resulting cols mostly fit.
constexpr const char* SAMPLE = "npm run build -- src/index.ts 0123456789 ERROR ok [y/N]";
}  // namespace

int termFontId() { return SMALL_FONT_ID; }

TermLayout terminalLayout(const GfxRenderer& renderer) {
  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const Rect safe = theme.getScreenSafeArea(renderer, true, false);

  TermLayout layout{};
  layout.x = safe.x + metrics.contentSidePadding;
  layout.width = std::max(1, safe.width - 2 * metrics.contentSidePadding);
  layout.y = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = safe.y + safe.height - ACTION_BAR_HEIGHT;
  layout.height = std::max(1, bottom - layout.y);
  layout.lineHeight = std::max(1, renderer.getLineHeight(termFontId()));

  const int sampleLen = static_cast<int>(strlen(SAMPLE));
  const int sampleWidth = std::max(sampleLen, renderer.getTextWidth(termFontId(), SAMPLE));
  // Width per column in 1/100 px, padded 8% for glyph-width variance.
  const int colWidth100 = std::max(1, sampleWidth * 108 / sampleLen);
  layout.cols = static_cast<uint16_t>(std::clamp(layout.width * 100 / colWidth100, 20, MAX_COLS));
  layout.rows = static_cast<uint16_t>(std::clamp(layout.height / layout.lineHeight, 4, MAX_SCREEN_LINES));
  return layout;
}

int actionBarHeight() { return ACTION_BAR_HEIGHT; }

}  // namespace agentmux

#endif  // AGENTMUX
