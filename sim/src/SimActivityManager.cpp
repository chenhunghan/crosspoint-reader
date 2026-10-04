// CrossPoint simulator: ActivityManager + RenderLock for a single-threaded
// host. Mirrors src/activities/ActivityManager.cpp (same push/pop/replace and
// result-handler semantics) but renders synchronously at the end of loop()
// instead of on a FreeRTOS render task, and launches only the screens the
// simulator compiles (goHome() re-enters the simulator's start screen).
#include <HalDisplay.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "SimApp.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "components/HeaderBackTapTarget.h"

namespace {
int renderLockDepth = 0;
bool renderPending = false;
bool rendering = false;
}  // namespace

void ActivityManager::begin() {}

void ActivityManager::renderTaskTrampoline(void*) {}

void ActivityManager::renderTaskLoop() {
  for (;;) {
  }
}

// One render pass of the current activity, as the render task would run it.
static void renderActivity(Activity* activity) {
  renderPending = false;
  if (rendering || !activity) return;
  rendering = true;
  RenderLock lock;
  display.setInverted(SETTINGS.screenInverted != 0);
  activity->render(std::move(lock));
  rendering = false;
}

void ActivityManager::loop() {
  if (mappedInput.consumeSuppressedRelease()) return;

  if (currentActivity) {
    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) return;
      goHome();
      return;
    }
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;
      if (!currentActivity) {
        pendingAction = PendingAction::None;
        continue;
      }
      ActivityResult pendingResult = std::move(currentActivity->result);
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();
        goHome();
        continue;
      }
      currentActivity = std::move(stackActivities.back());
      stackActivities.pop_back();
      if (currentActivity->resultHandler) {
        auto handler = std::move(currentActivity->resultHandler);
        currentActivity->resultHandler = nullptr;
        lock.unlock();
        handler(pendingResult);
      }
      if (pendingAction == PendingAction::None) requestUpdate();
      continue;
    } else if (pendingActivity) {
      RenderLock lock;
      if (pendingAction == PendingAction::Replace) {
        exitActivity(lock);
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        stackActivities.push_back(std::move(currentActivity));
        HeaderBackTapTarget::clear();
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);
      lock.unlock();
      currentActivity->onEnter();
      continue;
    } else {
      pendingAction = PendingAction::None;
    }
  }

  if (requestedUpdate.exchange(false) || renderPending) renderActivity(currentActivity.get());
}

void ActivityManager::exitActivity(const RenderLock&) {
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
  HeaderBackTapTarget::clear();
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  mappedInput.resetHomeButtonInput();
  if (currentActivity) {
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

// Screens outside the simulator's scope: log and stay put.
#define SIM_UNSUPPORTED(name) LOG_INF("SIM", "%s is not part of the simulator", name)
void ActivityManager::goToFileTransfer() { SIM_UNSUPPORTED("File transfer"); }
void ActivityManager::goToJoinNetwork() { SIM_UNSUPPORTED("Join network"); }
void ActivityManager::goToUsbDrive() { SIM_UNSUPPORTED("USB drive"); }
void ActivityManager::goToSettings() { SIM_UNSUPPORTED("Settings"); }
void ActivityManager::goToFileBrowser(std::string) { SIM_UNSUPPORTED("File browser"); }
void ActivityManager::goToLibrary() { SIM_UNSUPPORTED("Library"); }
void ActivityManager::goToBrowser() { SIM_UNSUPPORTED("OPDS browser"); }
void ActivityManager::goToPlugins(bool) { SIM_UNSUPPORTED("Plugins"); }
void ActivityManager::goToReader(std::string, bool) { SIM_UNSUPPORTED("Reader"); }
void ActivityManager::goToSleep(bool) { SIM_UNSUPPORTED("Sleep"); }
void ActivityManager::goToBoot() { SIM_UNSUPPORTED("Boot"); }
void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style) {
  LOG_INF("SIM", "Full screen message: %s", message.c_str());
}
void ActivityManager::goToCrashReport() { SIM_UNSUPPORTED("Crash report"); }

void ActivityManager::goHome(HomeMenuItem, bool) {
  LOG_INF("SIM", "Home -> simulator start screen");
  replaceActivity(sim::makeStartActivity(renderer, mappedInput));
}

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  mappedInput.resetHomeButtonInput();
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  mappedInput.resetHomeButtonInput();
  pendingActivity.reset();
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return true; }
bool ActivityManager::requiresExclusiveStorageLoop() const { return false; }
bool ActivityManager::isReaderActivity() const { return false; }
bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }
bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }
ScreenshotInfo ActivityManager::getScreenshotInfo() const { return currentActivity ? currentActivity->getScreenshotInfo() : ScreenshotInfo{}; }
void ActivityManager::prepareForSleep() {}

void ActivityManager::requestUpdate(bool) {
  // Both immediate and deferred requests render at the end of this loop pass.
  requestedUpdate = true;
  renderPending = true;
}

void ActivityManager::requestUpdateAndWait() {
  if (renderLockDepth > 0 || rendering) {
    LOG_ERR("SIM", "requestUpdateAndWait() while holding the render lock");
    return;
  }
  renderActivity(currentActivity.get());
}

// --- RenderLock: single-threaded, so only depth bookkeeping -----------------
RenderLock::RenderLock(Mode) {
  isLocked = true;
  ++renderLockDepth;
}
RenderLock::RenderLock(Activity&) : RenderLock(Mode::Blocking) {}
RenderLock::~RenderLock() { unlock(); }
void RenderLock::unlock() {
  if (isLocked) {
    isLocked = false;
    --renderLockDepth;
  }
}
bool RenderLock::peek() { return renderLockDepth > 0; }
