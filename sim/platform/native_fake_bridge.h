#pragma once
namespace sim {
// Restores the scripted bridge's three sessions and drops connections.
void fakeBridgeReset();
// Delivers due frames to connected clients (call once per tick).
void fakeBridgePump();
// The run records (docs/protocol.md, "Runs"): a device that asks for them in
// hello gets this runs frame. Empty = none.
void fakeBridgeSetRuns(const char* frame);
// The acts and report requests runs devices sent, as their JSON, oldest first.
const char* fakeBridgeDeviceEvent(unsigned long index);
}  // namespace sim
