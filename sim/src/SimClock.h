#pragma once
// Simulator time base behind millis()/delay(). Real time in the browser;
// the native scenario runner switches to a virtual clock it advances itself
// so snapshots are deterministic.
#include <cstdint>

namespace sim {
void useVirtualClock(bool enabled);
void advanceClock(uint32_t ms);
uint64_t nowMs();
}  // namespace sim
