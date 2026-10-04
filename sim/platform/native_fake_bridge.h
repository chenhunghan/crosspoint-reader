#pragma once
namespace sim {
// Restores the scripted bridge's three sessions and drops connections.
void fakeBridgeReset();
// Delivers due frames to connected clients (call once per tick).
void fakeBridgePump();
}  // namespace sim
