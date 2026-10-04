#pragma once
// Simulator core shared by the native and wasm front ends: brings up the
// renderer/fonts/settings like main.cpp's setup(), starts the Agent Mux app,
// and runs one firmware loop() iteration per tick().
#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace sim {
enum class StartScreen { AgentMux };

void setup(StartScreen start = StartScreen::AgentMux);
void tick();
// Activity the simulator (re)starts at, also used by ActivityManager::goHome().
std::unique_ptr<Activity> makeStartActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
}  // namespace sim
