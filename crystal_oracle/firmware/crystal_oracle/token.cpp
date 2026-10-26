/*
 * ============================================================================
 *  token.cpp — decode a crystal's page image and build its visit write list
 * ----------------------------------------------------------------------------
 *  Implements token.h (schema 0x03, byte-compatible with skull_oracle.ino and
 *  nfc_tag_bench.ino — see token.h for the page map and docs/DESIGN.md §6).
 *
 *  Classification (parse):
 *    Unreadable  the reader couldn't read every page 3..39, or no UID
 *    Ours        page 16 starts with 'S','K' (any schema byte, like the skull)
 *    Fresh       no marker AND pages 4..15 are "empty" or already hold our link:
 *                  * all zero, or
 *                  * TLVs: any NULL TLVs (00), Lock-Control (01 03 xx xx xx) and
 *                    Memory-Control (02 03 xx xx xx) TLVs, then an EMPTY NDEF
 *                    message (03 00) or straight away the Terminator (FE).
 *                    This covers the factory images
 *                      NTAG203  01 03 A0 10 44 03 00 FE
 *                      NTAG213  03 00 FE 00   (and 01 03 A0 0C 34 03 00 FE)
 *                    and the bench's "Format" (all zero).
 *                  * our link: 03 len D1 01 plen 55 04 <url> FE, where <url> is
 *                    the crystal's own link (crystalUrl) or the bare ndefUrl
 *                  * either link cut short by an interrupted first write
 *                    (pages 4..k ours, the rest still blank)
 *    Foreign     anything else (someone's sticker, a phone-written link ...):
 *                NEVER written.
 *  For Fresh crystals the visit pages are ignored: the marker (written last) is
 *  the commit, so a half-written fresh crystal simply rolls again.
 *
 *  Write list (buildVisitWrites) — the plinth executes it in order:
 *    Fresh: [CC E1 10 12 00 if page 3 byte 0 != E1] [NDEF 4..15 unless it's
 *           already its own link] [page 17 = 0 0 0 0] slot  marker
 *    Ours:  [CC] [NDEF 4..15 unless it's already its own link] slot  marker
 *           (the link upgrade: crystals written before the /c/<id> links;
 *            never touches charge+colour)
 *  Only pages 3..29 are ever emitted (30..39 reserved); the marker is ALWAYS last.
 *
 *  Crystal prep (the host menu's Crystals page; token.h has the rules):
 *    buildProvisionWrites   Fresh -> [CC] [NDEF] page 17 {0,colour,0,0}  marker(0)
 *                           Ours  -> [CC] [NDEF] only (the link upgrade)
 *    buildColorWrite        Ours: [CC] [NDEF] page 17 {charge, colour, b2, b3} (read-modify-write);
 *                           Fresh: the provisioning list with that colour
 *    buildClearYearWrites   this year's slot -> 0  [marker visits-1 if it was filled]
 *    buildFormatWrites      every non-zero page of 4..39 -> 0, marker (16) last
 *  All refuse a partial image (readOk false); only Format touches a foreign tag.
 * ============================================================================
 */
#include "token.h"
#include "settings.h"   // settings::cfg.ndefUrl (our link) for hasOurLink; colorCount()
#include <string.h>
#include <strings.h>

namespace token {

namespace {

constexpr uint8_t NDEF_BYTES = NDEF_PAGES * 4;   // 48 bytes in pages 4..15
// 03 len | D1 01 plen 55 | 04 <url> | FE  ->  8 bytes of framing around the URL
constexpr uint8_t URL_MAX = NDEF_BYTES - 8;      // 40 characters

constexpr uint8_t TLV_NULL = 0x00, TLV_LOCK = 0x01, TLV_MEM = 0x02, TLV_NDEF = 0x03, TLV_TERM = 0xFE;
constexpr uint8_t URI_PREFIX_HTTPS = 0x04;

constexpr uint8_t CC_NTAG203[4] = {0xE1, 0x10, 0x12, 0x00};   // NDEF v1.0, 144 bytes, read/write

// Salts keep the two UID hashes independent (and the log id meaningless elsewhere).
constexpr char ANON_SALT[] = "crystal-oracle/anon/v1:";
constexpr char COLOR_SALT[] = "crystal-oracle/colour/v1:";

inline const uint8_t *page(const TagImage &img, uint8_t p) { return img.pages[p - TAG_FIRST_PAGE]; }

uint32_t fnv1a(const char *salt, const uint8_t *uid, uint8_t n) {
  uint32_t h = 0x811C9DC5u;
  for (const char *s = salt; *s; s++) {
    h ^= (uint8_t)*s;
    h *= 0x01000193u;
  }
  for (uint8_t i = 0; i < n; i++) {
    h ^= uid[i];
    h *= 0x01000193u;
  }
  return h;
}

// The URL as stored on the tag: without "https://" (prefix code 0x04 adds it).
const char *urlBody(const char *url) {
  if (!url) return "";
  if (strncasecmp(url, "https://", 8) == 0) return url + 8;
  return url;
}

// Walks the TLVs at the start of the NDEF area, skipping NULL, Lock-Control and
// Memory-Control TLVs (NFC Forum Type 2: both control TLVs are exactly 3 value
// bytes). Returns the offset of the first other TLV's type byte, or NDEF_BYTES
// if only NULL TLVs follow. -1 if a control TLV is malformed (so random data
// that happens to start with 01/02 is never mistaken for a blank tag).
int firstContentTlv(const uint8_t *a) {
  int i = 0;
  while (i < NDEF_BYTES) {
    const uint8_t t = a[i];
    if (t == TLV_NULL) {
      i++;
      continue;
    }
    if (t != TLV_LOCK && t != TLV_MEM) return i;
    if (i + 1 >= NDEF_BYTES || a[i + 1] != 3) return -1;
    i += 2 + 3;
    if (i > NDEF_BYTES) return -1;
  }
  return NDEF_BYTES;
}

// True if pages 4..15 hold no NDEF content (see the header comment).
bool ndefEmpty(const uint8_t *a) {
  bool allZero = true;
  for (uint8_t i = 0; i < NDEF_BYTES; i++) {
    if (a[i]) {
      allZero = false;
      break;
    }
  }
  if (allZero) return true;
  const int i = firstContentTlv(a);
  if (i < 0) return false;
  if (i >= NDEF_BYTES) return true;              // only NULL / control TLVs
  if (a[i] == TLV_TERM) return true;             // terminator before any message
  if (a[i] == TLV_NDEF && i + 1 < NDEF_BYTES && a[i + 1] == 0x00) return true;   // empty NDEF message
  return false;
}

// True if the first NDEF message is exactly our single URI record.
bool linkMatches(const uint8_t *a, const char *url) {
  const size_t urlLen = strlen(url);
  if (!urlLen || urlLen > URL_MAX) return false;
  const int i = firstContentTlv(a);
  if (i < 0 || i + 2 >= NDEF_BYTES || a[i] != TLV_NDEF) return false;
  const uint8_t tlvLen = a[i + 1];
  const int r = i + 2;   // record header
  if (tlvLen == 0xFF || r + tlvLen > NDEF_BYTES || tlvLen < 5) return false;
  // D1 = MB|ME|SR, TNF 1 (well-known); type length 1; type 'U'; prefix https://
  if (a[r] != 0xD1 || a[r + 1] != 0x01 || a[r + 3] != 0x55 || a[r + 4] != URI_PREFIX_HTTPS) return false;
  const uint8_t payloadLen = a[r + 2];
  if (payloadLen != urlLen + 1 || (size_t)tlvLen != 4u + payloadLen) return false;
  return memcmp(&a[r + 5], url, urlLen) == 0;
}

// The 48 bytes we write into pages 4..15 (exactly like the skull's writeNdefUrl).
// false if the URL is empty or longer than 40 characters.
bool buildNdef(const char *url, uint8_t *buf) {
  const size_t urlLen = strlen(url);
  if (!urlLen || urlLen > URL_MAX) return false;
  memset(buf, 0, NDEF_BYTES);
  const uint8_t payloadLen = (uint8_t)(urlLen + 1);   // + URI prefix byte
  uint8_t i = 0;
  buf[i++] = TLV_NDEF;                 // NDEF Message TLV
  buf[i++] = (uint8_t)(payloadLen + 4);   // record header (4) + payload
  buf[i++] = 0xD1;                     // MB|ME|SR, TNF = well-known
  buf[i++] = 0x01;                     // type length
  buf[i++] = payloadLen;               // payload length
  buf[i++] = 0x55;                     // type 'U' (URI)
  buf[i++] = URI_PREFIX_HTTPS;         // "https://"
  memcpy(&buf[i], url, urlLen);
  i += (uint8_t)urlLen;
  buf[i++] = TLV_TERM;                 // phones stop reading here
  return true;
}

// Our link cut short by an interrupted provisioning (the plinth writes pages
// 4 -> 15 in order): the first k pages are ours, the rest still hold what a
// blank tag had there (zeros, or the NTAG213/203 factory "xx 03 00 FE" in page 5).
bool partialOurLink(const uint8_t *a, const char *url) {
  uint8_t want[NDEF_BYTES];
  if (!buildNdef(url, want)) return false;
  uint8_t k = 0;
  while (k < NDEF_PAGES && memcmp(&a[k * 4], &want[k * 4], 4) == 0) k++;
  if (k == 0 || k == NDEF_PAGES) return false;   // not started / complete (linkMatches covers it)
  for (uint8_t j = k; j < NDEF_PAGES; j++) {
    const uint8_t *p = &a[j * 4];
    const bool zero = !(p[0] | p[1] | p[2] | p[3]);
    const bool factoryTail = (j == 1 && p[1] == 0x03 && p[2] == 0x00 && p[3] == 0xFE);
    if (!zero && !factoryTail) return false;
  }
  return true;
}

inline void put(PageWrite *out, uint8_t &n, uint8_t pg, const uint8_t *d) {
  out[n].page = pg;
  memcpy(out[n].data, d, 4);
  n++;
}

// [CC if missing] + pages 4..15 = the crystal's own link, unless it's already there.
// (No / too long a URL: nothing — the crystal is still recorded; settings.cpp logs it.)
void putLink(const Crystal &c, const char *ndefUrl, PageWrite *out, uint8_t &n) {
  char url[64];
  uint8_t ndef[NDEF_BYTES];
  const bool writeLink = !c.hasOurLink && buildNdef(crystalUrl(ndefUrl, c.anonId, url, sizeof url), ndef);
  if ((writeLink || c.hasOurLink) && !c.hasCC) put(out, n, PAGE_CC, CC_NTAG203);
  if (writeLink) {
    for (uint8_t p = 0; p < NDEF_PAGES; p++) put(out, n, (uint8_t)(PAGE_NDEF + p), &ndef[p * 4]);
  }
}

}  // namespace

const char *crystalUrl(const char *ndefUrl, uint32_t anonId, char *buf, size_t size) {
  if (!buf || !size) return "";
  const char *base = urlBody(ndefUrl);
  size_t n = strlen(base);
  while (n && base[n - 1] == '/') n--;   // "site/" -> "site/c/<id>"
  if (n + 11 <= URL_MAX && n + 12 <= size) {
    snprintf(buf, size, "%.*s/c/%08lx", (int)n, base, (unsigned long)anonId);
  } else {
    snprintf(buf, size, "%.*s", (int)n, base);
  }
  if (!n) buf[0] = 0;
  return buf;
}

uint8_t tierForNumber(uint8_t n) {   // the skull's prizeTier()
  if (n >= 100) return 3;            // legendary (jackpot)
  if (n >= 90) return 2;             // rare (big win)
  if (n >= 40) return 1;             // treat
  return 0;                          // trick
}

const char *kindName(Kind k) {
  switch (k) {
    case Kind::Unreadable: return "unreadable";
    case Kind::Foreign:    return "foreign";
    case Kind::Fresh:      return "fresh";
    case Kind::Ours:       return "ours";
  }
  return "?";
}

void parse(const TagImage &img, uint16_t eventYear, uint16_t baseYear, Crystal &out) {
  out = Crystal();
  const uint8_t n = img.uidLen > TAG_UID_MAX ? TAG_UID_MAX : img.uidLen;
  memcpy(out.uid, img.uid, n);
  out.uidLen = n;
  if (n) out.anonId = fnv1a(ANON_SALT, out.uid, n);
  if (!img.readOk || n == 0) {
    out.kind = Kind::Unreadable;
    return;
  }

  out.hasCC = page(img, PAGE_CC)[0] == 0xE1;
  uint8_t ndef[NDEF_BYTES];
  for (uint8_t p = 0; p < NDEF_PAGES; p++) memcpy(&ndef[p * 4], page(img, PAGE_NDEF + p), 4);
  char own[64];
  const char *base = urlBody(settings::cfg.ndefUrl);
  crystalUrl(base, out.anonId, own, sizeof own);
  out.hasOurLink = linkMatches(ndef, own);

  const uint8_t *m = page(img, PAGE_MARKER);
  if (!(m[0] == MAGIC0 && m[1] == MAGIC1)) {
    // Not provisioned. Only blank tags (or ones already carrying our link - its own
    // or the bare site link, whole or cut short by an interrupted first write) may
    // be awakened. Anything but its own whole link leaves hasOurLink false, so the
    // next write rewrites all of pages 4..15.
    out.kind = (out.hasOurLink || ndefEmpty(ndef) || linkMatches(ndef, base) || partialOurLink(ndef, own) ||
                partialOurLink(ndef, base))
                   ? Kind::Fresh
                   : Kind::Foreign;
    return;
  }

  // ---- ours: marker, charge/colour, year history ----
  // (any schema byte is accepted, like the skull; 0x03 is the only one ever written)
  out.kind = Kind::Ours;
  out.visits = m[3];
  const uint8_t *chg = page(img, PAGE_CHARGE);
  out.charge = chg[0];
  out.color = chg[1];

  const int yIdx = (int)eventYear - (int)baseYear;   // this year's slot index (may be out of range)
  for (uint8_t y = 0; y < MAX_YEARS; y++) {
    const uint8_t *s = page(img, PAGE_YEAR0 + y);
    if (s[0] == 0) continue;                        // empty slot = didn't play that year
    YearEntry e;
    e.year = (uint16_t)(baseYear + y);
    e.number = s[0] > 100 ? 100 : s[0];
    e.tier = s[1] <= 3 ? s[1] : tierForNumber(e.number);
    e.flags = s[2];
    e.chapter = s[3];
    out.history[out.historyCount++] = e;            // ascending slots = oldest first
    if ((int)y == yIdx) {
      out.playedThisYear = true;
      out.thisYear = e;
    }
  }
  // Every writer keeps visits == filled slots; fewer visits than slots means a
  // write stopped between the slot and the marker. Count the slot (the next
  // visit then writes a corrected marker). Only ever raises it.
  if (out.historyCount > out.visits) out.visits = out.historyCount;

  // Prior years, most recent first: last result + the "no rare" streak (missed
  // years are skipped — they aren't visits).
  bool haveLast = false;
  for (int i = (int)out.historyCount - 1; i >= 0; i--) {
    const YearEntry &e = out.history[i];
    if ((int)e.year >= (int)eventYear) continue;   // this year (or a slot from the future)
    if (!haveLast) {
      out.lastTier = (int8_t)e.tier;
      out.lastYear = e.year;
      haveLast = true;
    }
    if (e.tier <= 1) {
      out.noRareStreak++;
    } else {
      break;
    }
  }
}

uint8_t buildVisitWrites(const Crystal &c, uint16_t eventYear, uint16_t baseYear, uint8_t number,
                         uint8_t tier, uint8_t flags, uint8_t chapter, const char *ndefUrl, PageWrite *out,
                         uint8_t max) {
  if (!out) return 0;
  if (c.kind != Kind::Fresh && c.kind != Kind::Ours) return 0;   // never write foreign / unreadable tags
  if (c.playedThisYear) return 0;                                 // once per year: replay instead
  const int yIdx = (int)eventYear - (int)baseYear;
  if (yIdx < 0 || yIdx >= MAX_YEARS) return 0;
  if (number == 0) return 0;                                      // 0 would read back as "not played"
  if (number > 100) number = 100;
  if (tier > 3) tier = tierForNumber(number);

  PageWrite tmp[LINK_MAX_WRITES];
  uint8_t n = 0;

  // CC (only if missing — OTP bits) and the crystal's own phone link (unless it's
  // already there): provisions a Fresh crystal, upgrades an older one of ours.
  putLink(c, ndefUrl, tmp, n);
  if (c.kind == Kind::Fresh) {
    const uint8_t zero[4] = {0, 0, 0, 0};
    put(tmp, n, PAGE_CHARGE, zero);   // a clean charge/colour page
  }

  const uint8_t slot[4] = {number, tier, (uint8_t)(flags | SF_ORACLE), chapter};
  put(tmp, n, (uint8_t)(PAGE_YEAR0 + yIdx), slot);

  const uint8_t prior = (c.kind == Kind::Ours) ? c.visits : 0;
  const uint8_t marker[4] = {MAGIC0, MAGIC1, SCHEMA, (uint8_t)(prior < 255 ? prior + 1 : 255)};
  put(tmp, n, PAGE_MARKER, marker);   // COMMIT: always last

  if (n > max) return 0;
  for (uint8_t i = 0; i < n; i++) {   // belt and braces: never page 2 (lock bytes) or the reserved 30..
    if (tmp[i].page < PAGE_CC || tmp[i].page >= PAGE_RESERVED0) return 0;
  }
  memcpy(out, tmp, sizeof(PageWrite) * n);
  return n;
}

// ============================================================ crystal prep
namespace {

inline void setWhy(const char **why, const char *s) {
  if (why) *why = s;
}

inline bool nonZero(const uint8_t *p) { return (p[0] | p[1] | p[2] | p[3]) != 0; }

// Parses the image for a prep builder. false (and *why) if it can't be used.
bool prepCrystal(const TagImage &img, Crystal &c, const char **why) {
  if (!img.readOk || img.uidLen == 0) {
    setWhy(why, "read failed - lift it and place it again");
    return false;
  }
  parse(img, settings::cfg.eventYear, settings::cfg.baseYear, c);
  if (c.kind == Kind::Unreadable) {
    setWhy(why, "read failed - lift it and place it again");
    return false;
  }
  return true;
}

// The last guard for every prep list (belt and braces, like buildVisitWrites): pages 3..39
// only, page 3 only while its byte 0 isn't 0xE1, the marker page only as the LAST entry, and
// the list fits `max`. Copies tmp -> out; 0 if any rule is broken.
uint8_t finishPrep(const TagImage &img, const PageWrite *tmp, uint8_t n, PageWrite *out, uint8_t max,
                   const char **why) {
  if (!out || n == 0) return 0;
  if (n > max) {
    setWhy(why, "too many pages for one write");
    return 0;
  }
  for (uint8_t i = 0; i < n; i++) {
    const uint8_t pg = tmp[i].page;
    const bool unsafe = pg < PAGE_CC || pg > TAG_LAST_PAGE || (pg == PAGE_CC && page(img, PAGE_CC)[0] == 0xE1) ||
                        (pg == PAGE_MARKER && i + 1 != n);
    if (unsafe) {
      setWhy(why, "refused: unsafe page list");
      return 0;
    }
  }
  memcpy(out, tmp, sizeof(PageWrite) * n);
  return n;
}

// A Fresh crystal's provisioning: [CC] [NDEF 4..15] page 17 = {0, colour, 0, 0}, marker visits 0.
uint8_t provisionList(const Crystal &c, uint8_t colour, const char *ndefUrl, PageWrite *tmp) {
  uint8_t n = 0;
  putLink(c, ndefUrl, tmp, n);   // (no / too long a URL: provisioned without the phone link)
  const uint8_t chg[4] = {0, colour, 0, 0};
  put(tmp, n, PAGE_CHARGE, chg);
  const uint8_t marker[4] = {MAGIC0, MAGIC1, SCHEMA, 0};
  put(tmp, n, PAGE_MARKER, marker);   // COMMIT: always last
  return n;
}

}  // namespace

uint8_t buildProvisionWrites(const TagImage &img, uint8_t colour, const char *ndefUrl, PageWrite *out, uint8_t max,
                             const char **why) {
  Crystal c;
  if (!prepCrystal(img, c, why)) return 0;
  if (c.kind == Kind::Foreign) {
    setWhy(why, "not blank - Format it first");
    return 0;
  }
  PageWrite tmp[PREP_MAX_WRITES];
  if (c.kind == Kind::Ours) {   // its visits stay; only an older link is brought up to date
    uint8_t n = 0;
    putLink(c, ndefUrl ? ndefUrl : "", tmp, n);
    if (!n) {
      setWhy(why, "already provisioned (its visits stay)");
      return 0;
    }
    return finishPrep(img, tmp, n, out, max, why);
  }
  if (colour > settings::CRYSTAL_COLORS_MAX) {
    setWhy(why, "no such colour");
    return 0;
  }
  const uint8_t n = provisionList(c, colour, ndefUrl ? ndefUrl : "", tmp);
  return finishPrep(img, tmp, n, out, max, why);
}

uint8_t buildColorWrite(const TagImage &img, uint8_t colour, const char *ndefUrl, PageWrite *out, uint8_t max,
                        const char **why) {
  Crystal c;
  if (!prepCrystal(img, c, why)) return 0;
  if (colour > settings::CRYSTAL_COLORS_MAX) {
    setWhy(why, "no such colour");
    return 0;
  }
  if (c.kind == Kind::Foreign) {
    setWhy(why, "not one of ours - Format it first");
    return 0;
  }
  if (c.kind == Kind::Fresh) return buildProvisionWrites(img, colour, ndefUrl, out, max, why);   // provision first
  // Ours: read-modify-write of page 17 - charge (byte 0) and bytes 2..3 stay exactly as they are
  // (an older link is brought up to date on the way)
  const uint8_t *cur = page(img, PAGE_CHARGE);
  if (cur[1] == colour) {
    setWhy(why, "it already has that colour");
    return 0;
  }
  PageWrite tmp[NDEF_PAGES + 2];
  uint8_t n = 0;
  putLink(c, ndefUrl ? ndefUrl : "", tmp, n);
  const uint8_t d[4] = {cur[0], colour, cur[2], cur[3]};
  put(tmp, n, PAGE_CHARGE, d);
  return finishPrep(img, tmp, n, out, max, why);
}

uint8_t buildClearYearWrites(const TagImage &img, uint16_t eventYear, uint16_t baseYear, PageWrite *out, uint8_t max,
                             const char **why) {
  Crystal c;
  if (!prepCrystal(img, c, why)) return 0;
  if (c.kind == Kind::Foreign) {
    setWhy(why, "not one of ours - nothing to clear");
    return 0;
  }
  const int yIdx = (int)eventYear - (int)baseYear;
  if (yIdx < 0 || yIdx >= MAX_YEARS) {
    setWhy(why, "event_year has no slot on the crystal");
    return 0;
  }
  const uint8_t slotPg = (uint8_t)(PAGE_YEAR0 + yIdx);
  const uint8_t *s = page(img, slotPg);
  PageWrite tmp[2];
  uint8_t n = 0;
  if (!nonZero(s)) {
    // A clear that stopped between the slot and the marker (crystal lifted) left the slot
    // empty but the marker still counting it. Every writer keeps visits == filled slots, so
    // finish that clear (marker only) instead of "nothing to clear" - else the crystal would
    // stay a "returning" one for good.
    const uint8_t *m = page(img, PAGE_MARKER);
    if (c.kind == Kind::Ours && m[3] > c.historyCount) {
      const uint8_t marker[4] = {MAGIC0, MAGIC1, m[2], c.historyCount};
      put(tmp, n, PAGE_MARKER, marker);
      return finishPrep(img, tmp, n, out, max, why);
    }
    setWhy(why, "nothing to clear - not played this year");
    return 0;
  }
  const uint8_t zero[4] = {0, 0, 0, 0};
  put(tmp, n, slotPg, zero);
  // Only a FILLED slot counted as a visit (and only once the marker exists): one visit less.
  // (parse() already raised visits to the filled-slot count if a write stopped half-way.)
  if (c.kind == Kind::Ours && s[0] != 0) {
    const uint8_t *m = page(img, PAGE_MARKER);
    const uint8_t marker[4] = {MAGIC0, MAGIC1, m[2], (uint8_t)(c.visits ? c.visits - 1 : 0)};
    put(tmp, n, PAGE_MARKER, marker);   // COMMIT: last
  }
  return finishPrep(img, tmp, n, out, max, why);
}

uint8_t buildFormatWrites(const TagImage &img, PageWrite *out, uint8_t max, const char **why) {
  if (!img.readOk || img.uidLen == 0) {
    setWhy(why, "read failed - lift it and place it again");
    return 0;
  }
  PageWrite tmp[PREP_MAX_WRITES];
  uint8_t n = 0;
  const uint8_t zero[4] = {0, 0, 0, 0};
  for (uint8_t p = PAGE_NDEF; p <= TAG_LAST_PAGE && n < PREP_MAX_WRITES - 1; p++) {
    if (p != PAGE_MARKER && nonZero(page(img, p))) put(tmp, n, p, zero);   // pages already zero are skipped
  }
  if (nonZero(page(img, PAGE_MARKER))) put(tmp, n, PAGE_MARKER, zero);    // the marker last
  if (!n) {
    setWhy(why, "it is already blank");
    return 0;
  }
  return finishPrep(img, tmp, n, out, max, why);
}

uint8_t benchColor(const Crystal &c) {
  return (c.color >= 1 && c.color <= settings::colorCount()) ? c.color : 0;
}

uint8_t displayColor(const Crystal &c) {
  const uint8_t n = settings::colorCount();   // the words' defined colours (0..6)
  if (!n) return 1;
  if (c.color >= 1 && c.color <= n) return c.color;   // set at the bench
  if (!c.uidLen) return 1;
  return (uint8_t)(1 + fnv1a(COLOR_SALT, c.uid, c.uidLen) % n);
}

}  // namespace token
