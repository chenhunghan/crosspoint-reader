#pragma once
// CrossPoint simulator: shadows lib/Logging/Logging.h. LOG_* go to
// simLogPrintf (stdout natively, console + the page's log panel in wasm).

#include <Arduino.h>

#include <string>

#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

void logPrintf(const char* level, const char* origin, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

#define LOG_ERR(origin, format, ...) logPrintf("ERR", origin, format "\n", ##__VA_ARGS__)
#if LOG_LEVEL >= 1
#define LOG_INF(origin, format, ...) logPrintf("INF", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_INF(origin, format, ...)
#endif
#if LOG_LEVEL >= 2
#define LOG_DBG(origin, format, ...) logPrintf("DBG", origin, format "\n", ##__VA_ARGS__)
#else
#define LOG_DBG(origin, format, ...)
#endif

static HardwareSerial& logSerial = Serial;
#define LOG_SERIAL_HAS_TX_TIMEOUT 0

inline std::string getLastLogs() { return {}; }
inline void clearLastLogs() {}
inline bool sanitizeLogHead() { return false; }
