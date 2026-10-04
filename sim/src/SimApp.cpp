// CrossPoint simulator: firmware globals + bring-up (mirrors src/main.cpp for
// the pieces the Agent Mux UI needs) and the per-tick loop body.
#include "SimApp.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <builtinFonts/notosans_8_regular.h>
#include <builtinFonts/ubuntu_10_bold.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_bold.h>
#include <builtinFonts/ubuntu_12_regular.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "agentmux/AgentMuxActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());

// UI fonts only: the simulator has no reader, so the Noto reading families
// (several MB of glyph data) stay out of the bundle.
EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);
EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);
EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

namespace sim {

std::unique_ptr<Activity> makeStartActivity(GfxRenderer& r, MappedInputManager& m) {
  return std::make_unique<agentmux::AgentMuxActivity>(r, m);
}

void setup(StartScreen) {
  LOG_INF("SIM", "CrossPoint simulator (Metalio E-Ink 4, Agent Mux)");
  gpio.begin();
  Storage.begin();
  SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  SETTINGS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  display.begin();
  renderer.begin();
  activityManager.begin();
  if (!fontDecompressor.init()) LOG_ERR("SIM", "Font decompressor init failed");
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  activityManager.replaceActivity(makeStartActivity(renderer, mappedInputManager));
}

void tick() {
  mappedInputManager.update();
  activityManager.loop();
  renderer.setFadingFix(SETTINGS.fadingFix);
}

}  // namespace sim
