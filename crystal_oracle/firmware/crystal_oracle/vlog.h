#pragma once
// ============================================================================
//  vlog.h — local logs on the TF card (never uploaded, no names):
//   /oracle/logs/visits_<year>.csv
//      ms,crystal,path,kind,visits,number,tier,flags,rank,write
//      (crystal = 8-hex salted hash of the UID, so a returning crystal can be
//       counted without storing the UID itself)
//   /oracle/logs/events.log  free-text boot / error lines (rotated at 256 KB)
//  Everything is also echoed to the USB serial console.
// ============================================================================
#include <Arduino.h>
#include "token.h"
#include "oracle.h"

namespace vlog {
void begin(uint16_t eventYear);
void visit(const char *path, const token::Crystal &c, const Outcome &o, const char *writeResult);
void event(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}
