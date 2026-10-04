cmake_minimum_required(VERSION 3.16)
project(crosspoint_sim C CXX)

# CrossPoint simulator: compiles the REAL firmware UI sources (Agent Mux app,
# activity framework, GfxRenderer, FreeInkUI, themes, keyboard, I18n, fonts)
# against host stubs in sim/stubs (shadowing Arduino/ESP-IDF/FreeRTOS headers)
# and sim implementations of the HAL in sim/src. Nothing outside sim/ is
# modified. Build with sim/build-native.sh or sim/build-wasm.sh.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)  # firmware builds with -std=gnu++2a
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SIM "${CMAKE_CURRENT_SOURCE_DIR}")
set(SDK "${REPO}/freeink-sdk/libs")

# Generated inputs (I18n strings, ArduinoJson) are prepared by the build
# scripts into SIM_GEN_DIR so the repo tree stays untouched.
set(SIM_GEN_DIR "" CACHE PATH "Directory with generated I18n files and third-party headers")
if(NOT SIM_GEN_DIR)
  message(FATAL_ERROR "Pass -DSIM_GEN_DIR=... (see sim/build-native.sh)")
endif()

set(REAL_SOURCES
  # Agent Mux app (the thing under test)
  ${REPO}/src/agentmux/AgentMuxActivity.cpp
  ${REPO}/src/agentmux/AgentMuxConfig.cpp
  ${REPO}/src/agentmux/AgentMuxUi.cpp
  ${REPO}/src/agentmux/BridgeClient.cpp
  ${REPO}/src/agentmux/SessionActivity.cpp
  # Activity framework + shared screens
  ${REPO}/src/activities/Activity.cpp
  ${REPO}/src/activities/UiListActivity.cpp
  ${REPO}/src/activities/util/KeyboardEntryActivity.cpp
  ${REPO}/src/activities/util/KeyboardLayoutSet.cpp
  ${REPO}/src/components/UiAppHost.cpp
  ${REPO}/src/components/UITheme.cpp
  ${REPO}/src/components/themes/BaseTheme.cpp
  ${REPO}/src/components/themes/lyra/LyraTheme.cpp
  ${REPO}/src/components/themes/lyra/Lyra3CoversTheme.cpp
  ${REPO}/src/components/themes/roundedraff/RoundedRaffTheme.cpp
  ${REPO}/src/MappedInputManager.cpp
  ${REPO}/src/util/ButtonNavigator.cpp
  ${REPO}/src/CrossPointSettings.cpp
  ${REPO}/src/RecentBooksStore.cpp
  ${REPO}/src/ReaderFontSizes.cpp
  # Rendering
  ${REPO}/lib/GfxRenderer/GfxRenderer.cpp
  ${REPO}/lib/GfxRenderer/Bitmap.cpp
  ${REPO}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO}/lib/EpdFont/EpdFont.cpp
  ${REPO}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO}/lib/EpdFont/FontDecompressor.cpp
  ${REPO}/lib/EpdFont/SdCardFont.cpp
  ${REPO}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO}/lib/InflateReader/InflateReader.cpp
  ${REPO}/lib/uzlib/src/tinflate.c
  ${REPO}/lib/Utf8/Utf8.cpp
  ${REPO}/lib/MiniBidi/BidiUtils.cpp
  ${REPO}/lib/MiniBidi/minibidi.c
  ${REPO}/lib/Memory/BuildScratch.cpp
  ${REPO}/lib/FsHelpers/FsHelpers.cpp
  ${REPO}/lib/Serialization/PersistableStore.cpp
  ${REPO}/lib/Serialization/ObfuscationUtils.cpp
  ${SDK}/ui/FreeInkUI/src/FreeInkUI.cpp
  # I18n (English-only generated tables; see build scripts)
  ${SIM_GEN_DIR}/i18n/I18n.cpp
  ${SIM_GEN_DIR}/i18n/I18nStrings.cpp
)

file(GLOB SIM_SOURCES ${SIM}/src/*.cpp)

set(SIM_INCLUDES
  ${SIM}/stubs              # shadows Arduino/ESP-IDF/SDK headers (must come first)
  ${SIM}/src
  ${SIM_GEN_DIR}/i18n
  ${SIM_GEN_DIR}/third_party
  ${REPO}/src
  ${REPO}/lib
  ${REPO}/lib/hal
  ${REPO}/lib/GfxRenderer
  ${REPO}/lib/EpdFont
  ${REPO}/lib/Utf8
  ${REPO}/lib/MiniBidi
  ${REPO}/lib/Memory
  ${REPO}/lib/FsHelpers
  ${REPO}/lib/Serialization
  ${REPO}/lib/Logging
  ${REPO}/lib/Epub
  ${SDK}/hardware/BoardConfig/include
  ${SDK}/hardware/MemoryManager/include
  ${SDK}/hardware/BatteryMonitor/include
  ${SDK}/hardware/Rtc/include
  ${SDK}/hardware/FrontlightManager/include
  ${SDK}/hardware/Imu/include
  ${SDK}/font/FreeInkFont/include
  ${REPO}/lib/KOReaderSync
  ${REPO}/lib/InflateReader
  ${REPO}/lib/uzlib/src
  ${SDK}/ui/FreeInkUI/include
  ${SDK}/assets/Icons/include
)

set(SIM_DEFINES
  CROSSPOINT_SIM=1
  CROSSPOINT_EMULATED=1
  AGENTMUX=1
  FREEINK_DEVICE_METALIO_EINK4=1
  BOARD_HAS_PSRAM=1
  ENABLE_SERIAL_LOG=1
  LOG_LEVEL=2
  CROSSPOINT_VERSION="sim"
  ARDUINOJSON_ENABLE_ARDUINO_STRING=1
  ARDUINOJSON_ENABLE_STD_STRING=1
  DESTRUCTOR_CLOSES_FILE=1
)

if(EMSCRIPTEN)
  file(GLOB PLATFORM_SOURCES ${SIM}/platform/wasm_*.cpp)
else()
  file(GLOB PLATFORM_SOURCES ${SIM}/platform/native_*.cpp)
endif()
add_executable(sim ${REAL_SOURCES} ${SIM_SOURCES} ${PLATFORM_SOURCES})

target_include_directories(sim PRIVATE ${SIM_INCLUDES})
target_compile_definitions(sim PRIVATE ${SIM_DEFINES})
target_compile_options(sim PRIVATE -Wno-deprecated-declarations -Wno-unused-parameter -Wno-inconsistent-missing-override -Wno-format -fno-exceptions)

if(EMSCRIPTEN)
  set_target_properties(sim OUTPUT_NAME "sim" SUFFIX ".js")
  target_compile_options(sim PRIVATE -Oz)
  target_link_options(sim PRIVATE
    -Oz
    -sENVIRONMENT=web
    -sMODULARIZE=1
    -sEXPORT_NAME=createSim
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=33554432
    -sSTACK_SIZE=1048576
    -sFILESYSTEM=1
    -lidbfs.js
    -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,UTF8ToString,stringToUTF8,lengthBytesUTF8,ccall,cwrap
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sINCOMING_MODULE_JS_API=print,printErr,preRun,postRun,onRuntimeInitialized,locateFile,canvas,noInitialRun
  )
else()
  target_compile_options(sim PRIVATE -O1 -g)
endif()
