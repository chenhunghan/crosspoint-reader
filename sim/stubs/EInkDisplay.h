#pragma once
// CrossPoint simulator: shadows FreeInkDisplay's EInkDisplay.h. HalDisplay
// only needs the type and its panel constants; sim/src/SimHalDisplay.cpp owns
// the framebuffer.
#include <cstdint>

#include "../../freeink-sdk/libs/display/FreeInkDisplay/include/GrayscaleCapabilities.h"

namespace freeink {
class FreeInkDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
};
}  // namespace freeink
using EInkDisplay = freeink::FreeInkDisplay;
