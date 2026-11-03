#pragma once
#include <Arduino.h>

// Diagnostic output. The release firmware keeps Serial for the PC protocol only (seriallink), so
// nothing may be printed unsolicited: in release these compile to nothing (arguments are NOT
// evaluated). The UI_DEBUG builds (watch_uidebug, watch_netdiag) map them to Serial, as before.
#ifdef UI_DEBUG
#define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DBG_PRINT(...) Serial.print(__VA_ARGS__)
#else
#define DBG_PRINTF(...) ((void)0)
#define DBG_PRINTLN(...) ((void)0)
#define DBG_PRINT(...) ((void)0)
#endif
