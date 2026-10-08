#pragma once
// Simulated SSD1677 panel: what the e-ink glass shows after the last refresh.
#include <cstdint>

namespace sim {
constexpr int PANEL_W = 800;  // native panel (landscape) pixels
constexpr int PANEL_H = 480;
constexpr int UI_W = 480;  // portrait UI, as the firmware draws it
constexpr int UI_H = 800;

// Number of completed panel refreshes so far.
uint32_t frameCount();
// HalDisplay::RefreshMode of the last refresh.
int lastRefreshMode();
// Writes the visible image as portrait RGBA (UI_W x UI_H x 4) with e-ink
// paper/ink colours, rotated exactly like GfxRenderer's Portrait mapping.
void renderPortraitRgba(uint8_t* out);
// 1 = ink at portrait UI pixel (x, y).
bool inkAt(int x, int y);
// The gray level shown at a portrait pixel: 0 ink, 1 dark, 2 light, 3 paper.
// After a black-and-white refresh only 0 and 3.
int levelAt(int x, int y);
}  // namespace sim
