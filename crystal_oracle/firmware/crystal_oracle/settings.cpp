/*
 * ============================================================================
 *  settings.cpp — station settings, on-screen words and persistent counters
 * ----------------------------------------------------------------------------
 *  Implements settings.h:
 *    * settings::load() / save()   /oracle/config.ini   <->  settings::cfg
 *    * settings::text() / color()  /oracle/captions.ini -> story_defaults.h -> fallback
 *    * settings::colorCount() / crystalRgb() / crystalStops() ...  the crystal
 *        colours (color.N_hex: "#RRGGBB", 2-4 stops "#..,#..", or "rainbow"),
 *        parsed once into a table (again after every load())
 *    * counters::load() / save() / resetTonight()
 *        /oracle/state/station.ini          all time: stationCrystals, stationAwakened
 *        /oracle/state/tonight_<year>.ini   this Halloween: everything else
 *                                           (incl. the communal meter: moon_points, moon_full)
 *
 *  INI dialect (one tiny parser for all four files):
 *    [section]              keys below it are looked up as "section.key"
 *    key = value            key is case-insensitive; value is trimmed and may
 *                           contain spaces
 *    key = "value"          quotes are optional (use them to keep leading or
 *                           trailing blanks, or a " ;" inside the value)
 *    ; comment  /  # comment    whole-line comments
 *    key = value   ; note   a ';' preceded by a blank starts a trailing comment.
 *                           '#' is NOT a trailing comment, so "#7C3AED" works.
 *    rank.3 = Glow          a key that already contains a '.' is used as-is, so
 *                           captions.ini may be written flat or in sections
 *    [nobutton]             ... except in [nobutton]: every key there gets the
 *    caption.charge = TAP!  prefix, dots or not ("nobutton.caption.charge"):
 *                           the no-button overrides of text() (settings.h)
 *    Keys above the first [section] have no prefix ("title", "character").
 *    Unknown keys are ignored. A UTF-8 BOM (Notepad) and CRLF line ends are fine.
 *
 *  Memory: files are read through a small internal DMA-capable buffer (the S3's
 *  SDMMC host can't DMA into PSRAM) and the text is kept in PSRAM. captions.ini
 *  stays loaded for the whole run: text() pointers must stay valid, so a reload
 *  never frees the previous copy (a few KB of PSRAM, only on an explicit reload).
 *
 *  Flash: NOTHING is written to the ESP32's internal flash (NVS / Preferences /
 *  FFat writes stall the PSRAM cache and make the RGB panel drift). Every write
 *  goes to the TF card as <file>.tmp first, then the old file is moved away and
 *  the .tmp renamed into place, so a power cut leaves the old or the new file
 *  (and load() falls back to a leftover .tmp). No SD card = defaults, and every
 *  save() quietly returns false.
 * ============================================================================
 */
#include "settings.h"
#include "story_defaults.h"   // included ONLY here (it defines a static table)
#include "token.h"            // token::MAX_YEARS for the event-year range
#include "media.h"            // media::sdOk()
#include "vlog.h"
#include <SD_MMC.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include "esp_heap_caps.h"

namespace settings {
Settings cfg;
}
namespace counters {
Counters c;
}

namespace {

// ---------------------------------------------------------------- paths ----
constexpr const char *CONFIG_PATH   = "/oracle/config.ini";
constexpr const char *CONFIG_BAK    = "/oracle/config.ini.bak";
constexpr const char *CAPTIONS_PATH = "/oracle/captions.ini";
constexpr const char *ORACLE_DIR    = "/oracle";
constexpr const char *STATE_DIR     = "/oracle/state";
constexpr const char *STATION_PATH  = "/oracle/state/station.ini";

constexpr size_t   MAX_INI_BYTES = 64 * 1024;   // bigger files are cut here (and logged)
constexpr size_t   IO_CHUNK      = 2048;        // internal DMA bounce buffer for SD I/O
constexpr uint16_t MAX_ENTRIES   = 1024;        // per file

// ---------------------------------------------------------------- memory ----
// Big/long-lived buffers live in PSRAM; falls back to any heap if PSRAM is absent.
void *bigAlloc(size_t n) {
  void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_malloc(n, MALLOC_CAP_8BIT);
  return p;
}

inline bool isBlank(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\v' || ch == '\f';
}
inline char lower(char ch) { return (ch >= 'A' && ch <= 'Z') ? (char)(ch - 'A' + 'a') : ch; }

// ============================================================ INI parser ====
// One classified line; pointers point into the source text (not terminated).
struct IniLine {
  enum Type : uint8_t { Skip, Section, Pair } type = Skip;
  const char *a = nullptr;   // section name or key
  size_t aLen = 0;
  const char *v = nullptr;   // value
  size_t vLen = 0;
};

IniLine parseLine(const char *p, size_t n) {
  IniLine out;
  while (n && isBlank(*p)) { p++; n--; }
  while (n && isBlank(p[n - 1])) n--;
  if (!n || *p == ';' || *p == '#') return out;

  if (*p == '[') {
    const char *close = (const char *)memchr(p, ']', n);
    if (!close) return out;                       // "[broken" -> ignore the line
    const char *s = p + 1;
    size_t sl = (size_t)(close - s);
    while (sl && isBlank(*s)) { s++; sl--; }
    while (sl && isBlank(s[sl - 1])) sl--;
    out.type = IniLine::Section;
    out.a = s;
    out.aLen = sl;
    return out;
  }

  const char *eq = (const char *)memchr(p, '=', n);
  if (!eq) return out;                            // not "key = value"
  size_t kl = (size_t)(eq - p);
  while (kl && isBlank(p[kl - 1])) kl--;
  if (!kl) return out;

  const char *v = eq + 1;
  size_t vl = n - (size_t)(v - p);
  while (vl && isBlank(*v)) { v++; vl--; }
  if (vl && (*v == '"' || *v == '\'')) {          // quoted: take what's inside
    const char q = *v;
    const char *endq = (const char *)memchr(v + 1, q, vl - 1);
    if (endq) {
      vl = (size_t)(endq - (v + 1));
      v = v + 1;
    } else {                                      // no closing quote: rest of the line
      v++;
      vl--;
    }
  } else {                                        // " ;" starts a trailing comment
    for (size_t i = 0; i < vl; i++) {             // (i == 0: "key = ; note" -> empty value)
      if (v[i] == ';' && (i == 0 || isBlank(v[i - 1]))) { vl = i; break; }
    }
    while (vl && isBlank(v[vl - 1])) vl--;
  }
  out.type = IniLine::Pair;
  out.a = p;
  out.aLen = kl;
  out.v = v;
  out.vLen = vl;
  return out;
}

struct IniEntry {
  const char *key;   // lower-case "section.key"
  const char *val;
};

// Parsed key/value pairs of one file, all strings in one PSRAM arena.
class IniStore {
 public:
  // Parse `len` bytes of text (needn't be terminated). Replaces any previous
  // contents WITHOUT freeing them (call release() first for temporary stores).
  bool parse(const char *text, size_t len);
  const char *get(const char *key) const;   // case-insensitive; nullptr if absent
  uint16_t size() const { return _n; }
  const IniEntry &at(uint16_t i) const { return _e[i]; }
  void release() {
    if (_arena) heap_caps_free(_arena);
    if (_e) heap_caps_free(_e);
    _arena = nullptr;
    _e = nullptr;
    _n = 0;
  }

 private:
  char *_arena = nullptr;
  IniEntry *_e = nullptr;
  uint16_t _n = 0;
};

// The no-button overrides (settings.h text()): "nobutton.<key>", written in captions.ini
// as section [nobutton] with the overridden key inside ("caption.charge = TAP! TAP!").
constexpr char   NOBUTTON_PREFIX[] = "nobutton.";
constexpr size_t NOBUTTON_PREFIX_LEN = sizeof(NOBUTTON_PREFIX) - 1;   // 9
constexpr size_t MAX_KEY_LEN = 64;                                     // longest key text() overrides

// A section whose name always prefixes its keys, even keys with a dot (= [nobutton]).
inline bool prefixSection(const char *sec, size_t secLen) {
  return secLen == NOBUTTON_PREFIX_LEN - 1 && strncasecmp(sec, NOBUTTON_PREFIX, secLen) == 0;
}

// Length of the full key for a pair under `sec` (secLen 0 = no section).
inline size_t fullKeyLen(const char *sec, size_t secLen, const char *key, size_t keyLen) {
  if (!secLen) return keyLen;
  if (memchr(key, '.', keyLen) && !prefixSection(sec, secLen)) return keyLen;
  return secLen + 1 + keyLen;
}

bool IniStore::parse(const char *text, size_t len) {
  _arena = nullptr;
  _e = nullptr;
  _n = 0;
  if (!text || !len) return true;

  // a UTF-8 BOM in front of the first line (Windows Notepad)
  if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) {
    text += 3;
    len -= 3;
  }

  // Two passes over the same lines: 1) count pairs + bytes, 2) fill the arena.
  size_t bytes = 0;
  uint16_t pairs = 0;
  for (int pass = 0; pass < 2; pass++) {
    const char *sec = nullptr;
    size_t secLen = 0;
    uint16_t seen = 0;   // pairs taken so far in this pass (pass 1 stops where pass 0 did)
    char *w = _arena;
    const char *p = text;
    const char *end = text + len;
    while (p < end) {
      const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
      const char *lineEnd = nl ? nl : end;
      const IniLine ln = parseLine(p, (size_t)(lineEnd - p));
      p = nl ? nl + 1 : end;

      if (ln.type == IniLine::Section) {
        sec = ln.a;
        secLen = ln.aLen;
        continue;
      }
      if (ln.type != IniLine::Pair) continue;
      const size_t kl = fullKeyLen(sec, secLen, ln.a, ln.aLen);

      if (pass == 0) {
        if (seen >= MAX_ENTRIES) continue;
        seen++;
        pairs++;
        bytes += kl + 1 + ln.vLen + 1;
        continue;
      }

      if (seen >= pairs) continue;   // exactly the lines pass 0 sized the arena for
      seen++;
      // key: "section.key" lower-cased (or the key alone)
      char *key = w;
      if (kl != ln.aLen) {
        for (size_t i = 0; i < secLen; i++) *w++ = lower(sec[i]);
        *w++ = '.';
      }
      for (size_t i = 0; i < ln.aLen; i++) *w++ = lower(ln.a[i]);
      *w++ = 0;
      char *val = w;
      memcpy(w, ln.v, ln.vLen);
      w += ln.vLen;
      *w++ = 0;

      // a repeated key: the last one wins
      bool dup = false;
      for (uint16_t i = 0; i < _n; i++) {
        if (strcmp(_e[i].key, key) == 0) {
          _e[i].val = val;
          dup = true;
          break;
        }
      }
      if (!dup) _e[_n++] = {key, val};
    }

    if (pass == 0) {
      if (!pairs) return true;
      _arena = (char *)bigAlloc(bytes);
      _e = (IniEntry *)bigAlloc(sizeof(IniEntry) * pairs);
      if (!_arena || !_e) {
        release();
        return false;
      }
    }
  }
  return true;
}

const char *IniStore::get(const char *key) const {
  if (!key) return nullptr;
  for (uint16_t i = 0; i < _n; i++) {
    if (strcasecmp(_e[i].key, key) == 0) return _e[i].val;
  }
  return nullptr;
}

// ============================================================== SD I/O ====
// Whole small file -> new NUL-terminated PSRAM buffer (caller frees with
// heap_caps_free). nullptr if missing/unreadable. Reads land in an internal
// DMA-capable chunk first and are copied out.
char *readFile(const char *path, size_t &len) {
  len = 0;
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return nullptr;
  if (f.isDirectory()) {
    f.close();
    return nullptr;
  }
  size_t want = f.size();
  if (want > MAX_INI_BYTES) {
    vlog::event("settings: %s is %u bytes - only the first %u are used", path, (unsigned)want,
                (unsigned)MAX_INI_BYTES);
    want = MAX_INI_BYTES;
  }
  char *out = (char *)bigAlloc(want + 1);
  uint8_t *chunk = (uint8_t *)heap_caps_malloc(IO_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!out || !chunk) {
    if (out) heap_caps_free(out);
    if (chunk) heap_caps_free(chunk);
    f.close();
    vlog::event("settings: out of memory reading %s", path);
    return nullptr;
  }
  while (len < want) {
    size_t n = want - len;
    if (n > IO_CHUNK) n = IO_CHUNK;
    const size_t got = f.read(chunk, n);
    if (got == 0 || got > n) break;   // EOF / read error: keep what we have
    memcpy(out + len, chunk, got);
    len += got;
  }
  heap_caps_free(chunk);
  f.close();
  out[len] = 0;
  return out;
}

// Reads `path`, else a leftover "<path>.tmp" (power cut between the two renames),
// else `bakPath` (if given). Parses it into `store`. false if nothing was found.
bool loadIni(const char *path, const char *bakPath, IniStore &store) {
  size_t len = 0;
  char *text = readFile(path, len);
  if (!text) {
    char tmp[72];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    text = readFile(tmp, len);
    if (text) vlog::event("settings: %s missing - using %s", path, tmp);
  }
  if (!text && bakPath) {
    text = readFile(bakPath, len);
    if (text) vlog::event("settings: %s missing - using %s", path, bakPath);
  }
  if (!text) return false;
  const bool ok = store.parse(text, len);
  heap_caps_free(text);
  if (!ok) vlog::event("settings: out of memory parsing %s", path);
  return ok;
}

// Text being built for a file (PSRAM, grows as needed).
class TextBuf {
 public:
  TextBuf() = default;
  TextBuf(const TextBuf &) = delete;
  TextBuf &operator=(const TextBuf &) = delete;
  ~TextBuf() {
    if (_p) heap_caps_free(_p);
  }
  void add(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  const char *data() const { return _p ? _p : ""; }
  size_t len() const { return _len; }
  bool ok() const { return !_oom; }

 private:
  bool reserve(size_t need) {
    if (need <= _cap) return true;
    size_t cap = _cap ? _cap : 1024;
    while (cap < need) cap *= 2;
    char *np = (char *)bigAlloc(cap);
    if (!np) return false;
    if (_p) {
      memcpy(np, _p, _len + 1);
      heap_caps_free(_p);
    }
    _p = np;
    _cap = cap;
    return true;
  }
  char *_p = nullptr;
  size_t _len = 0, _cap = 0;
  bool _oom = false;
};

void TextBuf::add(const char *fmt, ...) {
  if (_oom) return;
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  const int n = vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  if (n < 0 || !reserve(_len + (size_t)n + 1)) {
    _oom = (n >= 0);
    va_end(ap2);
    return;
  }
  vsnprintf(_p + _len, _cap - _len, fmt, ap2);
  va_end(ap2);
  _len += (size_t)n;
}

// Writes `len` bytes through an internal DMA-capable chunk buffer.
bool writeAll(File &f, const char *data, size_t len) {
  uint8_t *chunk = (uint8_t *)heap_caps_malloc(IO_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!chunk) return f.write((const uint8_t *)data, len) == len;
  size_t done = 0;
  while (done < len) {
    size_t n = len - done;
    if (n > IO_CHUNK) n = IO_CHUNK;
    memcpy(chunk, data + done, n);
    if (f.write(chunk, n) != n) break;
    done += n;
  }
  heap_caps_free(chunk);
  return done == len;
}

// Atomic-ish replace: write "<path>.tmp", move the old file to `bakPath` (or
// delete it), rename the .tmp into place. FAT can't rename over an existing
// file, hence the remove/rename dance.
bool replaceFile(const char *path, const TextBuf &t, const char *bakPath) {
  if (!t.ok()) return false;
  char tmp[72];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  SD_MMC.remove(tmp);   // stale leftover (fine if absent)
  File f = SD_MMC.open(tmp, FILE_WRITE);
  if (!f) return false;
  const bool ok = writeAll(f, t.data(), t.len());
  f.close();
  if (!ok) {
    SD_MMC.remove(tmp);
    return false;
  }
  if (bakPath) {
    SD_MMC.remove(bakPath);
    SD_MMC.rename(path, bakPath);   // fails harmlessly if there was no old file
  } else {
    SD_MMC.remove(path);
  }
  if (SD_MMC.rename(tmp, path)) return true;
  // couldn't swap the new file in: put the old one back if we moved it away
  if (bakPath && !SD_MMC.exists(path)) SD_MMC.rename(bakPath, path);
  return false;
}

// ========================================================== value parsing ====
bool parseUInt(const char *s, uint32_t &out) {
  if (!s) return false;
  while (isBlank(*s)) s++;
  if (*s == '+') s++;
  if (*s < '0' || *s > '9') return false;
  uint64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (uint64_t)(*s - '0');
    if (v > 0xFFFFFFFFull) v = 0xFFFFFFFFull;
    s++;
  }
  while (isBlank(*s)) s++;
  if (*s) return false;   // trailing junk ("12abc")
  out = (uint32_t)v;
  return true;
}

bool parseBool(const char *s, bool &out) {
  if (!s) return false;
  static const char *const YES[] = {"1", "yes", "y", "true", "on", "enabled"};
  static const char *const NO[] = {"0", "no", "n", "false", "off", "disabled"};
  for (const char *w : YES) {
    if (strcasecmp(s, w) == 0) { out = true; return true; }
  }
  for (const char *w : NO) {
    if (strcasecmp(s, w) == 0) { out = false; return true; }
  }
  return false;
}

int hexVal(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

// "#RRGGBB", "RRGGBB", "0xRRGGBB" (also the short "#RGB") -> 0xRRGGBB
bool parseColor(const char *s, uint32_t &out) {
  if (!s) return false;
  while (isBlank(*s)) s++;
  if (*s == '#') s++;
  else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  uint32_t v = 0;
  int digits = 0;
  while (hexVal(*s) >= 0 && digits < 7) {
    v = (v << 4) | (uint32_t)hexVal(*s);
    s++;
    digits++;
  }
  while (isBlank(*s)) s++;
  if (*s) return false;
  if (digits == 6) {
    out = v;
    return true;
  }
  if (digits == 3) {   // #RGB -> #RRGGBB
    const uint32_t r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
    out = (r * 0x11) << 16 | (g * 0x11) << 8 | (b * 0x11);
    return true;
  }
  return false;
}

// ======================================================= config.ini table ====
// One row per key: drives load(), save() and the template. `lo..hi` clamp the
// value (enums/bools/strings ignore them).
enum class FT : uint8_t { U8, U16, Bool, Reader, Rush, Url };
struct Field {
  const char *section;
  const char *key;
  FT type;
  void *ptr;
  uint16_t lo, hi;
  const char *note;
};

using settings::cfg;

const Field FIELDS[] = {
  // [station]
  {"station", "event_year", FT::U16, &cfg.eventYear, 2000, 2121, "year slot this Halloween writes - bump it every year"},
  {"station", "base_year", FT::U16, &cfg.baseYear, 2000, 2100, "year of tag page 18 - NEVER change once crystals exist"},
  {"station", "chapter", FT::U8, &cfg.chapter, 0, 255, "story chapter number stored with each visit"},
  {"station", "reader", FT::Reader, &cfg.reader, 0, 0, "plinth | rfid2 | none"},
  // No-button mode (DESIGN.md 5.7): with button = no (the default; the 2026 plinth has no big
  // button) a tap on the screen gives the no-crystal blessing, the words nobutton.<key> replace
  // <key> and the word nobutton.voice swaps the button voice lines. yes = the plinth's button.
  {"station", "button", FT::Bool, &cfg.hasButton, 0, 0, "yes = the plinth has a big button; no = kids tap the screen instead"},
  {"station", "group", FT::U16, &cfg.group, 0, 65535, "ESP-NOW station id (must match the plinth)"},
  {"station", "channel", FT::U8, &cfg.channel, 1, 13, "WiFi channel 1..13 for ESP-NOW (must match the plinth)"},
  {"station", "volume", FT::U8, &cfg.volume, 0, 100, "speaker volume 0..100"},
  {"station", "brightness", FT::U8, &cfg.brightness, 5, 100, "screen backlight 5..100"},
  {"station", "ring_brightness", FT::U8, &cfg.ringBrightness, 0, 255, "plinth LED ring brightness cap 0..255"},
  {"station", "pclk_mhz", FT::U8, &cfg.pclkMhz, 8, 16, "screen pixel clock MHz 8..16 (default 12; 16 made the real panel jump)"},
  {"station", "rush", FT::Rush, &cfg.rush, 0, 0, "auto | on | off  (short sequences when it's busy)"},
  {"station", "second_helpings", FT::Bool, &cfg.secondHelpings, 0, 0, "yes = an already-played crystal is sent to the common bowl again"},
  {"station", "allow_fresh", FT::Bool, &cfg.allowFresh, 0, 0, "yes = awaken blank tags that were never bench-provisioned"},
  {"station", "log_visits", FT::Bool, &cfg.logVisits, 0, 0, "yes = anonymous visit + event logs in /oracle/logs"},
  {"station", "show_counter", FT::Bool, &cfg.showCounter, 0, 0, "yes = station-wide crystal counter on the idle screen"},
  {"station", "ndef_url", FT::Url, cfg.ndefUrl, 0, 0, "phone link written to new crystals (https:// implied, max 40 chars)"},
  // [odds]
  {"odds", "trick_pct", FT::U8, &cfg.trickPct, 0, 100, "% of visits that are a TRICK (rest of the non-prizes = treat)"},
  {"odds", "rare_pct", FT::U8, &cfg.rarePct, 0, 100, "% base chance of RARE"},
  {"odds", "legend_pct", FT::U8, &cfg.legendPct, 0, 100, "% chance of LEGENDARY (loyalty doesn't change it)"},
  {"odds", "loyalty_rare_pct", FT::U8, &cfg.loyaltyRarePct, 0, 100, "+% rare for every earlier visit of the crystal ..."},
  {"odds", "loyalty_cap_pct", FT::U8, &cfg.loyaltyCapPct, 0, 100, "... but at most this much"},
  {"odds", "charge_rare_pct", FT::U8, &cfg.chargeRarePct, 0, 100, "+% rare for a full tap/press charge meter (scaled by fill)"},
  {"odds", "pity_after", FT::U8, &cfg.pityAfter, 0, 22, "visits in a row without rare before rare is guaranteed (0 = off)"},
  {"odds", "first_no_trick", FT::Bool, &cfg.firstNoTrick, 0, 0, "yes = a crystal's very first visit is never a trick"},
  // [stock]
  {"stock", "rare", FT::U16, &cfg.rareStock, 0, 65535, "rare prizes for tonight (0 = unlimited) - paced over the night"},
  {"stock", "legend", FT::U16, &cfg.legendStock, 0, 65535, "legendary prizes for tonight (0 = unlimited) - paced"},
  {"stock", "expected_kids", FT::U16, &cfg.expectedKids, 1, 65535, "expected crystal visits tonight (for the pacing)"},
  // [timing]
  {"timing", "attract_call_sec", FT::U16, &cfg.attractCallSec, 5, 3600, "teaser voice line every N s while idle"},
  {"timing", "chapter_every", FT::U16, &cfg.chapterEvery, 0, 1000, "chapter story every N attract loops (0 = never)"},
  {"timing", "charge_ms", FT::U16, &cfg.chargeMs, 500, 10000, "tap/press charge window for returning crystals, ms"},
  {"timing", "write_timeout_ms", FT::U16, &cfg.writeTimeoutMs, 300, 10000, "keep retrying the crystal write this long, ms"},
  {"timing", "milestone_every", FT::U16, &cfg.milestoneEvery, 0, 65535, "celebrate every Nth crystal at this station (0 = never)"},
  {"timing", "rush_gap_sec", FT::U16, &cfg.rushGapSec, 0, 600, "a crystal within N s of the last sequence => rush (0 = off)"},
  {"timing", "rush_quiet_sec", FT::U16, &cfg.rushQuietSec, 5, 3600, "rush ends after N s without sequences"},
  {"timing", "group_gap_sec", FT::U16, &cfg.groupGapSec, 0, 600, "crystals closer than N s are one group; 3 = circle (0 = off)"},
  {"timing", "tap_cooldown_sec", FT::U16, &cfg.tapCooldownSec, 0, 600, "button = no: taps start nothing for N s after a sequence ends"},
  {"timing", "tap_spark_sec", FT::U16, &cfg.tapSparkSec, 0, 3600, "button = no: a tap blessing adds a meter spark at most every N s"},
  // [story]
  {"story", "moon_goal", FT::U16, &cfg.moonGoal, 1, 1000, "meter points (crystal visits + no-crystal blessings) for tonight's finale"},
  {"story", "keeper_one_in", FT::U8, &cfg.keeperOneIn, 0, 100, "top-rank crystals hear the keeper secret about 1 visit in N (0 = never)"},
};

const char *const READER_NAMES[] = {"plinth", "rfid2", "none"};
const char *const RUSH_NAMES[] = {"auto", "on", "off"};

uint16_t clampU(uint32_t v, uint16_t lo, uint16_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return (uint16_t)v;
}

// Enum by name (or number). -1 if unknown.
int enumIndex(const char *s, const char *const *names, int n) {
  for (int i = 0; i < n; i++) {
    if (strcasecmp(s, names[i]) == 0) return i;
  }
  uint32_t v;
  if (parseUInt(s, v) && v < (uint32_t)n) return (int)v;
  return -1;
}

// The URL as stored: no "https://" (the NDEF prefix code 0x04 adds it back).
void setUrl(const char *v) {
  if (strncasecmp(v, "https://", 8) == 0) v += 8;
  const size_t n = strlen(v);
  if (n >= sizeof(cfg.ndefUrl)) {
    vlog::event("settings: ndef_url is %u chars - cut to %u", (unsigned)n, (unsigned)(sizeof(cfg.ndefUrl) - 1));
  }
  strlcpy(cfg.ndefUrl, v, sizeof(cfg.ndefUrl));
  if (strlen(cfg.ndefUrl) > 40) {
    vlog::event("settings: ndef_url longer than 40 chars - it won't fit on a crystal (link not written)");
  }
}

// Apply one config value. false = the value couldn't be understood.
bool applyField(const Field &f, const char *v) {
  uint32_t u;
  bool b;
  int e;
  switch (f.type) {
    case FT::U8:
      if (!parseUInt(v, u)) return false;
      *(uint8_t *)f.ptr = (uint8_t)clampU(u, f.lo, f.hi);
      return true;
    case FT::U16:
      if (!parseUInt(v, u)) return false;
      *(uint16_t *)f.ptr = clampU(u, f.lo, f.hi);
      return true;
    case FT::Bool:
      if (!parseBool(v, b)) return false;
      *(bool *)f.ptr = b;
      return true;
    case FT::Reader:
      e = enumIndex(v, READER_NAMES, 3);
      if (e < 0) return false;
      *(uint8_t *)f.ptr = (uint8_t)e;
      return true;
    case FT::Rush:
      e = enumIndex(v, RUSH_NAMES, 3);
      if (e < 0) return false;
      *(uint8_t *)f.ptr = (uint8_t)e;
      return true;
    case FT::Url:
      setUrl(v);
      return true;
  }
  return false;
}

void formatField(const Field &f, char *out, size_t n) {
  switch (f.type) {
    case FT::U8:     snprintf(out, n, "%u", (unsigned)*(const uint8_t *)f.ptr); break;
    case FT::U16:    snprintf(out, n, "%u", (unsigned)*(const uint16_t *)f.ptr); break;
    case FT::Bool:   snprintf(out, n, "%s", *(const bool *)f.ptr ? "yes" : "no"); break;
    case FT::Reader: snprintf(out, n, "%s", READER_NAMES[*(const uint8_t *)f.ptr < 3 ? *(const uint8_t *)f.ptr : 0]); break;
    case FT::Rush:   snprintf(out, n, "%s", RUSH_NAMES[*(const uint8_t *)f.ptr < 3 ? *(const uint8_t *)f.ptr : 0]); break;
    case FT::Url:    snprintf(out, n, "%s", (const char *)f.ptr); break;
  }
}

// Rules that involve more than one key (applied after every load).
void validate() {
  const uint16_t lastYear = (uint16_t)(cfg.baseYear + token::MAX_YEARS - 1);
  if (cfg.eventYear < cfg.baseYear || cfg.eventYear > lastYear) {
    const uint16_t fixed = cfg.eventYear < cfg.baseYear ? cfg.baseYear : lastYear;
    vlog::event("settings: event_year %u outside %u..%u (base_year + 21) - using %u", cfg.eventYear,
                cfg.baseYear, lastYear, fixed);
    cfg.eventYear = fixed;
  }
  if (cfg.reader > READER_NONE) cfg.reader = READER_PLINTH;
  if (cfg.rush > RUSH_OFF) cfg.rush = RUSH_AUTO;
  if ((uint16_t)cfg.legendPct + cfg.rarePct + cfg.trickPct > 100) {
    vlog::event("settings: legend+rare+trick = %u%% > 100%% - treats will be squeezed out",
                (unsigned)(cfg.legendPct + cfg.rarePct + cfg.trickPct));
  }
}

// =================================================================== state ====
IniStore s_captions;          // never released (text() pointers stay valid)
uint16_t s_cntYear = 0;       // year of the tonight file in use
Counters s_saved;             // what's on the card now (unchanged files aren't rewritten)
bool s_stationOnCard = false;
bool s_tonightOnCard = false;
bool s_dirsOk = false;
bool s_saveWarned = false;

bool sdReady() { return media::sdOk(); }

bool ensureStateDir() {
  if (s_dirsOk) return true;
  s_dirsOk = SD_MMC.mkdir(ORACLE_DIR) && SD_MMC.mkdir(STATE_DIR);   // true if they exist already
  return s_dirsOk;
}

void tonightPath(char *out, size_t n, uint16_t year) {
  snprintf(out, n, "/oracle/state/tonight_%u.ini", (unsigned)year);
}

uint32_t getU32(const IniStore &st, const char *key) {
  uint32_t v = 0;
  const char *s = st.get(key);
  if (s && parseUInt(s, v)) return v;
  return 0;
}
uint16_t getU16(const IniStore &st, const char *key) {
  const uint32_t v = getU32(st, key);
  return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

bool sameTonight(const Counters &a, const Counters &b) {
  return a.visits == b.visits && a.awakened == b.awakened && a.returning == b.returning &&
         a.replays == b.replays && a.noCrystal == b.noCrystal && a.sleepy == b.sleepy &&
         a.foreign == b.foreign && a.tier[0] == b.tier[0] && a.tier[1] == b.tier[1] &&
         a.tier[2] == b.tier[2] && a.tier[3] == b.tier[3] && a.rareGiven == b.rareGiven &&
         a.legendGiven == b.legendGiven && a.writeFails == b.writeFails &&
         a.moonPoints == b.moonPoints && a.moonFull == b.moonFull;
}

// ====================================================== crystal colours ====
// words color.N_hex -> one parsed table, built on first use and again after
// every settings::load() (grammar: settings.h). Lookups are then a few integer
// ops (+ one cosf / HSV step for animated colours): fine every frame.
using settings::CRYSTAL_COLORS_MAX;
using settings::CRYSTAL_STOPS_MAX;
using settings::CRYSTAL_CYCLE_MS;

struct CrystalColor {
  bool     rainbow = false;
  uint8_t  n = 0;                              // stops in use (1..4); 0 only for a rainbow
  uint32_t stop[CRYSTAL_STOPS_MAX] = {};
};

// When a value is missing or unreadable: the firmware's own six (0 = colour 1).
constexpr uint32_t CRYSTAL_FALLBACK[CRYSTAL_COLORS_MAX + 1] = {0x8B5CF6, 0x8B5CF6, 0xF97316, 0x38BDF8,
                                                               0x4ADE80, 0xF472B6, 0xE2E8F0};
// The rainbow: candy-bright but not neon on the RGB565 panel (pure 0/255 primaries
// look harsh and pure blue reads dark in the additive glows).
constexpr float RAINBOW_SAT = 0.70f;
constexpr float RAINBOW_VAL = 1.00f;

CrystalColor s_crystal[CRYSTAL_COLORS_MAX + 1];   // [1..6]; [0] = a copy of [1]
uint8_t      s_colorCount = 0;
bool         s_colorsReady = false;

inline uint8_t unit8(float v) { return v <= 0.0f ? 0 : (v >= 1.0f ? 255 : (uint8_t)(v * 255.0f + 0.5f)); }

uint32_t hsvRgb(float h, float s, float v) {   // h 0..1 (wraps), s / v 0..1
  h = (h - floorf(h)) * 6.0f;
  int sec = (int)h;
  if (sec > 5) sec = 5;
  const float f = h - (float)sec;
  const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
  float r, g, b;
  switch (sec) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return ((uint32_t)unit8(r) << 16) | ((uint32_t)unit8(g) << 8) | unit8(b);
}

uint32_t blendRgb(uint32_t a, uint32_t b, float f) {   // f 0..1
  const uint32_t k = (uint32_t)(f <= 0.0f ? 0.0f : (f >= 1.0f ? 256.0f : f * 256.0f));
  const uint32_t inv = 256u - k;
  const uint32_t r = (((a >> 16) & 0xFF) * inv + ((b >> 16) & 0xFF) * k) >> 8;
  const uint32_t g = (((a >> 8) & 0xFF) * inv + ((b >> 8) & 0xFF) * k) >> 8;
  const uint32_t bl = ((a & 0xFF) * inv + (b & 0xFF) * k) >> 8;
  return (r << 16) | (g << 8) | bl;
}

// True if the value is empty (blanks / quotes only): the colour is not defined.
bool blankValue(const char *v) {
  if (!v) return true;
  for (; *v; v++)
    if (!isBlank(*v) && *v != '"' && *v != '\'') return false;
  return true;
}

// One color.N_hex value -> c. Returns false if nothing in it was a colour (c is
// then the built-in colour for `id`). Bad / surplus stops are skipped and logged.
bool parseCrystal(const char *v, uint8_t id, CrystalColor &c, const char *key) {
  c = CrystalColor();
  while (isBlank(*v) || *v == '"' || *v == '\'') v++;
  size_t len = strlen(v);
  while (len && (isBlank(v[len - 1]) || v[len - 1] == '"' || v[len - 1] == '\'')) len--;
  if (len == 7 && strncasecmp(v, "rainbow", 7) == 0) {
    c.rainbow = true;
    return true;
  }
  const char *p = v;
  const char *end = v + len;
  uint8_t bad = 0, surplus = 0;
  while (p < end) {   // stops separated by commas (blanks and ';' are tolerated too)
    while (p < end && (isBlank(*p) || *p == ',' || *p == ';')) p++;
    if (p >= end) break;
    const char *s = p;
    while (p < end && !isBlank(*p) && *p != ',' && *p != ';') p++;
    const size_t tl = (size_t)(p - s);
    char tok[16];
    uint32_t rgb = 0;
    bool ok = tl < sizeof tok;
    if (ok) {
      memcpy(tok, s, tl);
      tok[tl] = 0;
      ok = parseColor(tok, rgb);
    }
    if (!ok) { bad++; continue; }
    if (c.n >= CRYSTAL_STOPS_MAX) { surplus++; continue; }
    c.stop[c.n++] = rgb;
  }
  if (bad) vlog::event("settings: %s = \"%s\": %u part(s) not a #RRGGBB colour - skipped", key, v, bad);
  if (surplus) vlog::event("settings: %s has more than %u colours - the first %u are used", key,
                           (unsigned)CRYSTAL_STOPS_MAX, (unsigned)CRYSTAL_STOPS_MAX);
  if (c.n) return true;
  c.n = 1;
  c.stop[0] = CRYSTAL_FALLBACK[id <= CRYSTAL_COLORS_MAX ? id : 1];
  return false;
}

void ensureColors() {
  if (s_colorsReady) return;
  s_colorsReady = true;
  s_colorCount = 0;
  bool counting = true;   // defined = contiguous from 1
  char key[20];
  char summary[160];
  size_t o = 0;
  summary[0] = 0;
  for (uint8_t id = 1; id <= CRYSTAL_COLORS_MAX; id++) {
    snprintf(key, sizeof key, "color.%u_hex", (unsigned)id);
    const char *v = settings::text(key, "");
    CrystalColor &c = s_crystal[id];
    if (blankValue(v)) {
      counting = false;
      c = CrystalColor();
      c.n = 1;
      c.stop[0] = CRYSTAL_FALLBACK[id];
      continue;
    }
    if (counting) s_colorCount = id;
    if (!parseCrystal(v, id, c, key))
      vlog::event("settings: %s = \"%s\" is not a colour - using #%06lX", key, v, (unsigned long)c.stop[0]);
    if (!counting || o + 40 >= sizeof summary) continue;
    if (c.rainbow) {
      o += (size_t)snprintf(summary + o, sizeof summary - o, " %u=rainbow", (unsigned)id);
    } else {
      o += (size_t)snprintf(summary + o, sizeof summary - o, " %u=#%06lX", (unsigned)id, (unsigned long)c.stop[0]);
      for (uint8_t k = 1; k < c.n && o + 10 < sizeof summary; k++)
        o += (size_t)snprintf(summary + o, sizeof summary - o, ">#%06lX", (unsigned long)c.stop[k]);
    }
  }
  s_crystal[0] = s_crystal[1];
  vlog::event("settings: %u crystal colour(s):%s", (unsigned)s_colorCount, summary);
}

const CrystalColor &crystalAt(uint8_t id) {
  ensureColors();
  return s_crystal[(id >= 1 && id <= CRYSTAL_COLORS_MAX) ? id : 1];
}

}  // namespace

// ============================================================ settings:: ====
namespace settings {

bool load() {
  cfg = Settings();   // defaults first; config.ini only overrides what it names
  s_colorsReady = false;   // the crystal colours are re-read from the (new) words on next use
  if (!sdReady()) {
    validate();
    return false;
  }

  // captions.ini (kept for the whole run; the old copy is intentionally not freed)
  IniStore caps;
  if (loadIni(CAPTIONS_PATH, nullptr, caps)) {
    s_captions = caps;
    vlog::event("settings: captions.ini %u entries", s_captions.size());
  } else {
    vlog::event("settings: no captions.ini - built-in words");
  }

  IniStore ini;
  if (!loadIni(CONFIG_PATH, CONFIG_BAK, ini)) {
    vlog::event("settings: no config.ini - built-in defaults");
    validate();
    return false;
  }
  uint16_t used = 0, unknown = 0, bad = 0;
  for (uint16_t i = 0; i < ini.size(); i++) {
    const IniEntry &e = ini.at(i);
    bool known = false;
    for (const Field &f : FIELDS) {
      const size_t sl = strlen(f.section);
      if (strncmp(e.key, f.section, sl) == 0 && e.key[sl] == '.' && strcmp(e.key + sl + 1, f.key) == 0) {
        known = true;
        if (applyField(f, e.val)) {
          used++;
        } else {
          bad++;
          vlog::event("settings: %s = \"%s\" not understood - keeping the default", e.key, e.val);
        }
        break;
      }
    }
    if (!known) unknown++;
  }
  ini.release();
  validate();
  vlog::event("settings: config.ini %u keys used, %u unknown, %u bad - year %u, reader %s, button %s", used,
              unknown, bad, cfg.eventYear, READER_NAMES[cfg.reader], cfg.hasButton ? "yes" : "no (taps bless)");
  return true;
}

bool save() {
  if (!sdReady()) return false;
  TextBuf t;
  t.add("; ==========================================================================\n");
  t.add(";  Crystal Oracle - station settings  (/oracle/config.ini on the TF card)\n");
  t.add(";  key = value, one per line. ';' starts a comment. Unknown keys are ignored\n");
  t.add(";  and missing keys use the firmware defaults. yes/no, on/off, 1/0 all work.\n");
  t.add(";  The service menu rewrites this file; the previous one is kept as .bak\n");
  t.add("; ==========================================================================\n");
  const char *sec = nullptr;
  char val[72];
  for (const Field &f : FIELDS) {
    if (!sec || strcmp(sec, f.section) != 0) {
      sec = f.section;
      t.add("\n[%s]\n", sec);
    }
    formatField(f, val, sizeof(val));
    t.add("%-17s = %-8s ; %s\n", f.key, val, f.note);
  }
  if (!t.ok()) return false;
  SD_MMC.mkdir(ORACLE_DIR);
  const bool ok = replaceFile(CONFIG_PATH, t, CONFIG_BAK);
  vlog::event("settings: config.ini %s", ok ? "saved" : "SAVE FAILED");
  return ok;
}

}  // namespace settings

namespace {

// One word: captions.ini, then story_defaults.h. nullptr = neither has the key.
const char *lookupWord(const char *key) {
  const char *v = s_captions.get(key);
  if (v) return v;
  for (const StoryDefault &d : STORY_DEFAULTS) {
    if (strcasecmp(d.key, key) == 0) return d.value;
  }
  return nullptr;
}

// No-button mode ([station] button = no): "nobutton.<key>" when that word exists (an empty
// value counts: it blanks the word), else nullptr. Keys that are themselves "nobutton.*"
// (e.g. nobutton.voice) are never prefixed twice.
const char *noButtonWord(const char *key) {
  if (settings::cfg.hasButton || strncasecmp(key, NOBUTTON_PREFIX, NOBUTTON_PREFIX_LEN) == 0) return nullptr;
  const size_t kl = strlen(key);
  if (kl > MAX_KEY_LEN) return nullptr;
  char full[NOBUTTON_PREFIX_LEN + MAX_KEY_LEN + 1];
  memcpy(full, NOBUTTON_PREFIX, NOBUTTON_PREFIX_LEN);
  memcpy(full + NOBUTTON_PREFIX_LEN, key, kl + 1);
  return lookupWord(full);
}

}  // namespace

namespace settings {

const char *text(const char *key, const char *fallback) {
  if (!fallback) fallback = "";
  if (!key) return fallback;
  const char *v = noButtonWord(key);
  if (!v) v = lookupWord(key);
  return v ? v : fallback;
}

uint32_t color(const char *key, uint32_t fallbackRgb) {
  if (!key) return fallbackRgb;
  uint32_t rgb;
  const char *nb = noButtonWord(key);     // a no-button override that isn't a colour is ignored
  if (nb && parseColor(nb, rgb)) return rgb;
  const char *v = s_captions.get(key);
  if (v && parseColor(v, rgb)) return rgb;
  for (const StoryDefault &d : STORY_DEFAULTS) {
    if (strcasecmp(d.key, key) == 0) {
      if (parseColor(d.value, rgb)) return rgb;
      break;
    }
  }
  return fallbackRgb;
}

const char *meterName() {
  const char *n = text("meter.name", "");
  if (n[0]) return n;
  const char *st = text("meter.style", "");
  while (*st == ' ' || *st == '\t') st++;
  return strncasecmp(st, "lantern", 7) == 0 ? "Lantern" : "Moon";
}

uint8_t colorCount() {
  ensureColors();
  return s_colorCount;
}

uint32_t crystalRgb(uint8_t id, uint32_t tMs) {
  const CrystalColor &c = crystalAt(id);
  const uint32_t ph = tMs % CRYSTAL_CYCLE_MS;
  if (c.rainbow) return hsvRgb((float)ph / (float)CRYSTAL_CYCLE_MS, RAINBOW_SAT, RAINBOW_VAL);
  if (c.n < 2) return c.n ? c.stop[0] : CRYSTAL_FALLBACK[1];
  // n stops round the cycle: stop i -> stop i+1 (the last back to the first),
  // cosine-eased so each stop lingers a moment instead of bouncing off it.
  const uint32_t pos = ph * c.n;                    // 0 .. n * CYCLE - 1
  const uint8_t i = (uint8_t)(pos / CRYSTAL_CYCLE_MS);
  const float f = (float)(pos % CRYSTAL_CYCLE_MS) / (float)CRYSTAL_CYCLE_MS;
  const float e = 0.5f - 0.5f * cosf(f * 3.14159265f);
  return blendRgb(c.stop[i % c.n], c.stop[(i + 1) % c.n], e);
}

bool crystalAnimated(uint8_t id) {
  const CrystalColor &c = crystalAt(id);
  return c.rainbow || c.n >= 2;
}

bool crystalRainbow(uint8_t id) { return crystalAt(id).rainbow; }

uint8_t crystalStops(uint8_t id, uint32_t *out, uint8_t max) {
  const CrystalColor &c = crystalAt(id);
  if (c.rainbow || !out) return 0;
  const uint8_t n = c.n < max ? c.n : max;
  for (uint8_t i = 0; i < n; i++) out[i] = c.stop[i];
  return n;
}

}  // namespace settings

// ============================================================ counters:: ====
namespace counters {

bool load(uint16_t eventYear) {
  c = Counters();
  s_saved = c;
  s_cntYear = eventYear;
  s_stationOnCard = s_tonightOnCard = false;
  s_saveWarned = false;
  if (!sdReady()) return false;

  IniStore st;
  if (loadIni(STATION_PATH, nullptr, st)) {
    c.stationCrystals = getU32(st, "station.crystals");
    c.stationAwakened = getU32(st, "station.awakened");
    s_stationOnCard = true;
  }
  st.release();

  char path[48];
  tonightPath(path, sizeof(path), eventYear);
  IniStore tn;
  if (loadIni(path, nullptr, tn)) {
    c.visits      = getU16(tn, "tonight.visits");
    c.awakened    = getU16(tn, "tonight.awakened");
    c.returning   = getU16(tn, "tonight.returning");
    c.replays     = getU16(tn, "tonight.replays");
    c.noCrystal   = getU16(tn, "tonight.no_crystal");
    c.sleepy      = getU16(tn, "tonight.sleepy");
    c.foreign     = getU16(tn, "tonight.foreign");
    c.tier[0]     = getU16(tn, "tonight.tier_0");
    c.tier[1]     = getU16(tn, "tonight.tier_1");
    c.tier[2]     = getU16(tn, "tonight.tier_2");
    c.tier[3]     = getU16(tn, "tonight.tier_3");
    c.rareGiven   = getU16(tn, "tonight.rare_given");
    c.legendGiven = getU16(tn, "tonight.legend_given");
    c.writeFails  = getU16(tn, "tonight.write_fails");
    c.moonPoints  = getU16(tn, "tonight.moon_points");
    bool full = false;
    const char *mf = tn.get("tonight.moon_full");
    if (mf && parseBool(mf, full)) c.moonFull = full;
    s_tonightOnCard = true;
  }
  tn.release();
  s_saved = c;
  vlog::event("counters: station %lu crystals (%lu awakened), tonight %u visits, moon %u%s",
              (unsigned long)c.stationCrystals, (unsigned long)c.stationAwakened, c.visits, c.moonPoints,
              c.moonFull ? " (full)" : "");
  return true;   // card present (missing files simply mean "start from zero")
}

bool save() {
  if (!sdReady()) return false;
  if (!s_cntYear) s_cntYear = settings::cfg.eventYear;
  if (!ensureStateDir()) {
    if (!s_saveWarned) vlog::event("counters: can't create %s", STATE_DIR);
    s_saveWarned = true;
    return false;
  }
  bool ok = true;

  if (!s_stationOnCard || c.stationCrystals != s_saved.stationCrystals ||
      c.stationAwakened != s_saved.stationAwakened) {
    TextBuf t;
    t.add("; Crystal Oracle - all-time counters of this station (written by the firmware)\n");
    t.add("[station]\n");
    t.add("crystals = %lu ; crystal visits that rolled, every year\n", (unsigned long)c.stationCrystals);
    t.add("awakened = %lu ; first visits (crystals awakened here)\n", (unsigned long)c.stationAwakened);
    if (replaceFile(STATION_PATH, t, nullptr)) {
      s_saved.stationCrystals = c.stationCrystals;
      s_saved.stationAwakened = c.stationAwakened;
      s_stationOnCard = true;
    } else {
      ok = false;
    }
  }

  if (!s_tonightOnCard || !sameTonight(c, s_saved)) {
    char path[48];
    tonightPath(path, sizeof(path), s_cntYear);
    TextBuf t;
    t.add("; Crystal Oracle - counters for Halloween %u (written by the firmware;\n", (unsigned)s_cntYear);
    t.add("; reset from the service menu, or delete this file)\n");
    t.add("[tonight]\n");
    t.add("visits       = %u ; crystal visits that rolled\n", c.visits);
    t.add("awakened     = %u ; first visits\n", c.awakened);
    t.add("returning    = %u ; returning crystals\n", c.returning);
    t.add("replays      = %u ; crystals that had already played tonight\n", c.replays);
    t.add("no_crystal   = %u ; button presses without a crystal\n", c.noCrystal);
    t.add("sleepy       = %u ; unreadable placements\n", c.sleepy);
    t.add("foreign      = %u ; not an oracle crystal\n", c.foreign);
    t.add("tier_0       = %u ; tricks\n", c.tier[0]);
    t.add("tier_1       = %u ; treats\n", c.tier[1]);
    t.add("tier_2       = %u ; rares\n", c.tier[2]);
    t.add("tier_3       = %u ; legendaries\n", c.tier[3]);
    t.add("rare_given   = %u ; rare stock used\n", c.rareGiven);
    t.add("legend_given = %u ; legendary stock used\n", c.legendGiven);
    t.add("write_fails  = %u ; crystal writes that failed\n", c.writeFails);
    t.add("moon_points  = %u ; the communal meter (crystal visits + no-crystal blessings)\n", c.moonPoints);
    t.add("moon_full    = %s ; the finale has played tonight\n", c.moonFull ? "yes" : "no");
    if (replaceFile(path, t, nullptr)) {
      Counters keep = s_saved;   // keep the station half as last written
      s_saved = c;
      s_saved.stationCrystals = keep.stationCrystals;
      s_saved.stationAwakened = keep.stationAwakened;
      s_tonightOnCard = true;
    } else {
      ok = false;
    }
  }

  if (!ok) {
    s_dirsOk = false;   // re-check the folders next time (card swapped?)
    if (!s_saveWarned) vlog::event("counters: save FAILED (TF card?)");
    s_saveWarned = true;
  } else {
    s_saveWarned = false;
  }
  return ok;
}

void resetTonight() {
  const uint32_t sc = c.stationCrystals, sa = c.stationAwakened;
  c = Counters();
  c.stationCrystals = sc;
  c.stationAwakened = sa;
  vlog::event("counters: tonight reset (year %u)", (unsigned)(s_cntYear ? s_cntYear : settings::cfg.eventYear));
  save();
}

}  // namespace counters
