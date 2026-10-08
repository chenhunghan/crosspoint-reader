// The mahler screen as a C library (ui-ffi, a Rust static library over the
// mahler-ui crate): give it the bridge's run records, keys and taps; it lays
// out and draws the 480x800 panel and says what to ask of the bridge.
// One source with the desktop's copy of the screen, which the bridge draws.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MahlerUi MahlerUi;

enum { MAHLER_KEY_BACK = 0, MAHLER_KEY_OK = 1, MAHLER_KEY_UP = 2, MAHLER_KEY_DOWN = 3 };
enum { MAHLER_EFFECT_NONE = 0, MAHLER_EFFECT_REDRAW = 1, MAHLER_EFFECT_ACT = 2, MAHLER_EFFECT_REPORT = 3 };

// What a key or a tap asks of the host: redraw, run `action` ("stop", "ack",
// "reclaim") on run `name` through the bridge, or fetch `name`'s report.
typedef struct {
  uint8_t kind;
  char name[160];
  char action[16];
} MahlerEffect;

enum { MAHLER_WIDTH = 480, MAHLER_HEIGHT = 800 };

MahlerUi* mahler_ui_new(void);
void mahler_ui_free(MahlerUi* ui);

// The bridge's `runs` frame (JSON, not NUL-terminated); false if it is not one.
bool mahler_ui_set_runs(MahlerUi* ui, const char* json, size_t len);
bool mahler_ui_has_runs(const MahlerUi* ui);
// When the bridge read the records (seconds); the screen's clock starts there.
int64_t mahler_ui_records_at(const MahlerUi* ui);
// A report the screen asked for (MAHLER_EFFECT_REPORT): the whole text, joined.
void mahler_ui_set_report(MahlerUi* ui, const char* name, const char* text, size_t len);
// The bridge's answer to an action; a refusal shows on the screen.
void mahler_ui_set_act_result(MahlerUi* ui, bool ok, const char* msg);
// The link to the bridge: off it, the list says "offline" and for how long.
void mahler_ui_set_link(MahlerUi* ui, bool online, uint32_t offline_minutes, const char* label);

MahlerEffect mahler_ui_press(MahlerUi* ui, uint8_t key);
MahlerEffect mahler_ui_tap(MahlerUi* ui, int32_t x, int32_t y);

// Draws the screen at `now` (seconds, the bridge's clock) into
// MAHLER_WIDTH * MAHLER_HEIGHT bytes, row by row: 0 ink, 3 paper.
void mahler_ui_render(MahlerUi* ui, int64_t now, uint8_t* levels);

#ifdef __cplusplus
}
#endif
