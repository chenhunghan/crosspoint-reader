#pragma once
// CrossPoint simulator: shadows the SDK InputManager. HalGPIO is built with
// CROSSPOINT_EMULATED=1 (no InputManager member); sim/src/SimHalGPIO.cpp feeds
// buttons and touches injected by the host page.
#include <cstdint>
#include <BoardConfig.h>
