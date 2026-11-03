#pragma once
#include <Arduino.h>

// Diagnostic output. The release firmware keeps Serial for the PC protocol only (seriallink), so
// nothing may be printed unsolicited: DBG discards everything. The UI_DEBUG builds (watch_uidebug,
// watch_netdiag) map it to Serial, as before.
#ifdef UI_DEBUG
#define DBG Serial
#else
class NullPrint : public Print {
 public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t n) override { return n; }
};
static NullPrint s_nullPrint;
#define DBG s_nullPrint
#endif
