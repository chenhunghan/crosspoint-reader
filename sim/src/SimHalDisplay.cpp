// CrossPoint simulator: HalDisplay backed by an in-memory framebuffer. The
// "glass" keeps the image of the last displayBuffer(); the page shows it and
// flashes briefly on FULL/HALF refreshes.
#include <HalDisplay.h>

#include <cstring>

#include "SimDisplay.h"
#include "SimHost.h"

HalDisplay display;

namespace {
uint8_t frameBuffer[HalDisplay::BUFFER_SIZE];
uint8_t glass[HalDisplay::BUFFER_SIZE];
uint8_t grayLsb[HalDisplay::BUFFER_SIZE];
uint8_t grayMsb[HalDisplay::BUFFER_SIZE];
bool lent = false;
bool inverted = false;
uint32_t frames = 0;
int lastMode = HalDisplay::FULL_REFRESH;

void present(const int mode) {
  memcpy(glass, frameBuffer, sizeof(glass));
  if (inverted) {
    for (auto& b : glass) b = static_cast<uint8_t>(~b);
  }
  ++frames;
  lastMode = mode;
  sim::hostFramePresented(mode);
}
}  // namespace

namespace sim {
uint32_t frameCount() { return frames; }
int lastRefreshMode() { return lastMode; }

bool inkAt(const int x, const int y) {
  // Inverse of GfxRenderer's Portrait rotateCoordinates: phyX = y, phyY = H-1-x.
  const int phyX = y;
  const int phyY = PANEL_H - 1 - x;
  const uint8_t byte = glass[phyY * (PANEL_W / 8) + (phyX >> 3)];
  return (byte & (0x80 >> (phyX & 7))) == 0;
}

void renderPortraitRgba(uint8_t* out) {
  // Paper and ink tones of an SSD1677 panel under room light.
  static constexpr uint8_t PAPER[3] = {0xE9, 0xE6, 0xDD};
  static constexpr uint8_t INK[3] = {0x26, 0x26, 0x2A};
  for (int y = 0; y < UI_H; ++y) {
    for (int x = 0; x < UI_W; ++x) {
      const uint8_t* c = inkAt(x, y) ? INK : PAPER;
      out[0] = c[0];
      out[1] = c[1];
      out[2] = c[2];
      out[3] = 0xFF;
      out += 4;
    }
  }
}
}  // namespace sim

HalDisplay::HalDisplay() {
  memset(frameBuffer, 0xFF, sizeof(frameBuffer));
  memset(glass, 0xFF, sizeof(glass));
}
HalDisplay::~HalDisplay() = default;

HalDisplay::Controller HalDisplay::getController() const { return BoardConfig::ACTIVE.displayController; }
HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode) const { return {}; }

void HalDisplay::begin(bool) {}
void HalDisplay::clearScreen(const uint8_t color) const { memset(frameBuffer, color, sizeof(frameBuffer)); }

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, bool) const {
  const int wb = w / 8;
  for (int row = 0; row < h && y + row < DISPLAY_HEIGHT; ++row) {
    for (int col = 0; col < wb && x / 8 + col < DISPLAY_WIDTH_BYTES; ++col) {
      frameBuffer[(y + row) * DISPLAY_WIDTH_BYTES + x / 8 + col] = imageData[row * wb + col];
    }
  }
}
void HalDisplay::drawImageTransparent(const uint8_t* imageData, const uint16_t x, const uint16_t y,
                                      const uint16_t w, const uint16_t h, bool) const {
  const int wb = w / 8;
  for (int row = 0; row < h && y + row < DISPLAY_HEIGHT; ++row) {
    for (int col = 0; col < wb && x / 8 + col < DISPLAY_WIDTH_BYTES; ++col) {
      frameBuffer[(y + row) * DISPLAY_WIDTH_BYTES + x / 8 + col] &= imageData[row * wb + col];
    }
  }
}

void HalDisplay::displayBuffer(const RefreshMode mode, bool) { present(mode); }
void HalDisplay::displayBufferAsync(const RefreshMode mode) { present(mode); }
void HalDisplay::waitRefreshComplete() {}
bool HalDisplay::supportsAsyncRefresh() const { return false; }
bool HalDisplay::supportsAsyncGrayscaleBase() const { return false; }
void HalDisplay::refreshDisplay(const RefreshMode mode, bool) { present(mode); }

void HalDisplay::setInverted(const bool value) { inverted = value; }
bool HalDisplay::toggleInverted() {
  inverted = !inverted;
  return inverted;
}
bool HalDisplay::isInverted() const { return inverted; }
void HalDisplay::deepSleep() {}
uint8_t* HalDisplay::getFrameBuffer() const { return frameBuffer; }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (lent) return nullptr;
  lent = true;
  if (sizeOut) *sizeOut = sizeof(frameBuffer);
  return frameBuffer;
}
void HalDisplay::returnFrameBufferStorage() {
  lent = false;
  memset(frameBuffer, 0xFF, sizeof(frameBuffer));
}

void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
void HalDisplay::displayGrayscaleBase(const RefreshMode fallback, bool) { present(fallback); }
bool HalDisplay::displayGrayscaleBase(GrayscaleMode, const RefreshMode fallback, bool) {
  present(fallback);
  return false;
}
void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsb, const uint8_t* msb) {
  memcpy(grayLsb, lsb, sizeof(grayLsb));
  memcpy(grayMsb, msb, sizeof(grayMsb));
}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsb) { memcpy(grayLsb, lsb, sizeof(grayLsb)); }
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msb) { memcpy(grayMsb, msb, sizeof(grayMsb)); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool) {}
void HalDisplay::writeGrayscalePlaneStrip(bool, const uint8_t*, uint16_t, uint16_t) {}
bool HalDisplay::supportsStripGrayscale() const { return false; }
bool HalDisplay::combinesGrayscaleBase() const { return false; }

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
