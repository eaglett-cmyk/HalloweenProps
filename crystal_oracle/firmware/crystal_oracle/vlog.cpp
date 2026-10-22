/*
 * ============================================================================
 *  vlog.cpp — local logs on the TF card (never uploaded, no names)
 * ----------------------------------------------------------------------------
 *  Implements vlog.h:
 *    /oracle/logs/visits_<year>.csv   one row per crystal visit (header row is
 *                                     written when the file is new/empty):
 *        ms,crystal,path,kind,visits,number,tier,flags,rank,write
 *        crystal = 8-hex salted hash of the UID (token::Crystal::anonId);
 *        number/tier/flags/rank are empty when the visit had no outcome
 *        (no crystal, foreign, sleepy). flags are decimal (token::SlotFlags).
 *    /oracle/logs/events.log          free text, "[      ms] message";
 *                                     moved to events.old at 256 KB.
 *  Everything is echoed to the USB serial console at once, from the caller.
 *
 *  The card is written by a small background task (core 0, low priority): the
 *  callers only format a line and drop it into a queue (in PSRAM), so logging
 *  never stalls the show's frames and is safe from any task (not from ISRs).
 *  A full queue drops the line (counted, and noted in events.log later) —
 *  nothing here ever blocks. Each batch is appended and the file closed again,
 *  so pulling the plug at the end of the night loses nothing that was written.
 *
 *  No SD card, or [station] log_visits = no  ->  serial console only.
 * ============================================================================
 */
#include "vlog.h"
#include "settings.h"
#include "media.h"
#include <SD_MMC.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

namespace vlog {

namespace {

constexpr const char *LOG_DIR     = "/oracle/logs";
constexpr const char *EVENTS_PATH = "/oracle/logs/events.log";
constexpr const char *EVENTS_OLD  = "/oracle/logs/events.old";
constexpr uint32_t    ROTATE_BYTES = 256u * 1024u;
constexpr const char *CSV_HEADER  = "ms,crystal,path,kind,visits,number,tier,flags,rank,write\n";

constexpr UBaseType_t QUEUE_LEN   = 24;
constexpr size_t      LINE_CHARS  = 237;    // text per queued line (+ NUL); longer lines are cut
constexpr int         BATCH_MAX   = 32;     // lines per open/close of a file
constexpr uint32_t    TASK_STACK  = 5120;   // internal RAM (FatFs + SDMMC driver run on it)
constexpr UBaseType_t TASK_PRIO   = 2;      // below the audio streamer (6) and mixer (18)
constexpr BaseType_t  TASK_CORE   = 0;

enum : uint8_t { F_EVENTS = 0, F_VISITS = 1 };

struct Msg {
  uint8_t file;
  uint8_t len;
  char text[LINE_CHARS + 1];
};

QueueHandle_t s_q = nullptr;
TaskHandle_t  s_task = nullptr;
char          s_visitsPath[48] = "";
uint32_t      s_dropped = 0;        // lines lost to a full queue (atomic exchange)
uint32_t      s_eventsSize = 0;     // events.log size as of the last batch

bool cardLogging() { return s_q != nullptr && settings::cfg.logVisits && media::sdOk(); }

void enqueue(uint8_t file, const char *text, size_t len) {
  if (!cardLogging()) return;
  Msg m;
  m.file = file;
  if (len > LINE_CHARS) len = LINE_CHARS;
  m.len = (uint8_t)len;
  memcpy(m.text, text, len);
  m.text[len] = 0;
  if (len && m.text[len - 1] != '\n') {   // a cut line still ends with a newline
    m.text[len - 1] = '\n';
  }
  if (xQueueSend(s_q, &m, 0) != pdTRUE) __atomic_add_fetch(&s_dropped, 1, __ATOMIC_RELAXED);
}

// CSV fields must not contain commas / line breaks.
void csvSafe(char *dst, size_t n, const char *src) {
  size_t i = 0;
  if (src) {
    for (; src[i] && i + 1 < n; i++) {
      const char ch = src[i];
      dst[i] = (ch == ',' || ch == '\n' || ch == '\r' || ch == '"') ? ';' : ch;
    }
  }
  dst[i] = 0;
}

File openLog(uint8_t file) {
  const char *path = (file == F_VISITS) ? s_visitsPath : EVENTS_PATH;
  File f = SD_MMC.open(path, FILE_APPEND);
  if (!f) {                       // folder gone (card swapped / reformatted)? make it and retry once
    SD_MMC.mkdir("/oracle");
    SD_MMC.mkdir(LOG_DIR);
    f = SD_MMC.open(path, FILE_APPEND);
  }
  if (!f) return f;
  if (file == F_VISITS) {
    if (f.size() == 0) f.write((const uint8_t *)CSV_HEADER, strlen(CSV_HEADER));
  } else {
    s_eventsSize = (uint32_t)f.size();
  }
  return f;
}

void rotateEventsIfBig() {
  if (s_eventsSize < ROTATE_BYTES) return;
  SD_MMC.remove(EVENTS_OLD);
  SD_MMC.rename(EVENTS_PATH, EVENTS_OLD);
  s_eventsSize = 0;   // even if the rename failed: openLog() re-reads the real size next batch
}

void writerTask(void *) {
  Msg m;   // on this task's (internal RAM) stack: the SD writes come from here
  for (;;) {
    if (xQueueReceive(s_q, &m, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
    File f;
    uint8_t cur = 0xFF;
    bool wroteEvents = false;
    int lines = 0;
    do {
      if (m.file != cur) {
        if (f) f.close();
        f = openLog(m.file);
        cur = m.file;
      }
      if (f && m.len) {
        const size_t w = f.write((const uint8_t *)m.text, m.len);
        if (m.file == F_EVENTS) {
          s_eventsSize += (uint32_t)w;
          wroteEvents = true;
        }
      }
    } while (++lines < BATCH_MAX && xQueueReceive(s_q, &m, 0) == pdTRUE);

    const uint32_t dropped = __atomic_exchange_n(&s_dropped, 0, __ATOMIC_RELAXED);
    if (dropped) {
      if (cur != F_EVENTS) {
        if (f) f.close();
        f = openLog(F_EVENTS);
        cur = F_EVENTS;
      }
      if (f) {
        char note[64];
        const int n = snprintf(note, sizeof(note), "[%7lu] vlog: %lu line(s) dropped (queue full)\n",
                               (unsigned long)millis(), (unsigned long)dropped);
        if (n > 0) s_eventsSize += (uint32_t)f.write((const uint8_t *)note, (size_t)n);
        wroteEvents = true;
      }
    }
    if (f) f.close();
    if (wroteEvents) rotateEventsIfBig();
  }
}

// One finished line ("...\n") to the serial console, in a single write so lines
// from different tasks don't interleave mid-line.
void toSerial(const char *line, size_t len) { Serial.write((const uint8_t *)line, len); }

}  // namespace

void begin(uint16_t eventYear) {
  snprintf(s_visitsPath, sizeof(s_visitsPath), "%s/visits_%u.csv", LOG_DIR, (unsigned)eventYear);
  if (s_q) return;   // already running (only the year may change)
  if (!settings::cfg.logVisits) {
    Serial.println("vlog: log_visits = no - serial console only");
    return;
  }
  if (!media::sdOk()) {
    Serial.println("vlog: no TF card - serial console only");
    return;
  }
  SD_MMC.mkdir("/oracle");
  if (!SD_MMC.mkdir(LOG_DIR)) Serial.println("vlog: can't create /oracle/logs (will retry per write)");

  bool withCaps = true;
  QueueHandle_t q = xQueueCreateWithCaps(QUEUE_LEN, sizeof(Msg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!q) {                                    // no PSRAM: a 5.8 KB internal queue
    withCaps = false;
    q = xQueueCreate(QUEUE_LEN, sizeof(Msg));
  }
  if (!q) {
    Serial.println("vlog: out of memory - serial console only");
    return;
  }
  s_q = q;   // set before the task starts (it reads s_q)
  if (xTaskCreatePinnedToCore(writerTask, "vlog", TASK_STACK, nullptr, TASK_PRIO, &s_task, TASK_CORE) != pdPASS) {
    // No writer: drop the queue so nothing piles up, and stay on the console.
    s_q = nullptr;
    s_task = nullptr;
    if (withCaps) vQueueDeleteWithCaps(q);
    else vQueueDelete(q);
    Serial.println("vlog: can't start the writer task - serial console only");
  }
}

void visit(const char *path, const token::Crystal &c, const Outcome &o, const char *writeResult) {
  char p[24], w[32], id[9] = "";
  csvSafe(p, sizeof(p), path ? path : "");
  csvSafe(w, sizeof(w), writeResult ? writeResult : "");
  if (c.uidLen) snprintf(id, sizeof(id), "%08lx", (unsigned long)c.anonId);

  char line[200];
  int n;
  if (o.number) {   // a real outcome (rolled or replayed)
    n = snprintf(line, sizeof(line), "%lu,%s,%s,%s,%u,%u,%u,%u,%u,%s\n", (unsigned long)millis(), id, p,
                 token::kindName(c.kind), o.visitsAfter, o.number, o.tier, o.flags, o.rank, w);
  } else {          // no outcome: no crystal / foreign / sleepy
    n = snprintf(line, sizeof(line), "%lu,%s,%s,%s,%u,,,,,%s\n", (unsigned long)millis(), id, p,
                 token::kindName(c.kind), c.visits, w);
  }
  if (n <= 0) return;
  size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;

  char echo[220];
  const int e = snprintf(echo, sizeof(echo), "[%7lu] visit %.*s", (unsigned long)millis(), (int)len, line);
  if (e > 0) toSerial(echo, (size_t)e < sizeof(echo) ? (size_t)e : sizeof(echo) - 1);
  enqueue(F_VISITS, line, len);
}

void event(const char *fmt, ...) {
  char line[LINE_CHARS + 1];
  int n = snprintf(line, sizeof(line), "[%7lu] ", (unsigned long)millis());
  if (n < 0) n = 0;
  va_list ap;
  va_start(ap, fmt);
  const int m = vsnprintf(line + n, sizeof(line) - (size_t)n, fmt, ap);
  va_end(ap);
  size_t len = (size_t)n + (m > 0 ? (size_t)m : 0);
  if (len > sizeof(line) - 2) len = sizeof(line) - 2;   // room for the newline
  line[len++] = '\n';
  line[len] = 0;
  toSerial(line, len);
  enqueue(F_EVENTS, line, len);
}

}  // namespace vlog
