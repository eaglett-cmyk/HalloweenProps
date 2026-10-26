#pragma once
// ============================================================================
//  token.h — the crystal's on-tag data. Schema 0x03, byte-compatible with
//  skull_oracle/skull_oracle.ino and nfc_tag_bench/nfc_tag_bench.ino:
//
//   page  3      CC  E1 10 12 00   (written only if byte 0 != 0xE1 — CC is OTP)
//   pages 4..15  NDEF URI record (phone-tappable link) + 0xFE terminator:
//                https://<ndef_url>/c/<8-hex anonId> (the crystal's own page on the
//                site; the bare <ndef_url> if that would pass 40 characters)
//   page  16     'S' 'K' 0x03 visits          <- written LAST (= commit)
//   page  17     charge, colour, 0, 0         (colour = Crystal Oracle extension, set at the bench; 0 = unset;
//                                              1..settings::colorCount() = the words' color.N)
//   pages 18..29 one page per year (slot = 18 + year - BASE_YEAR, 12 years):
//                number(1..100), tier(0..3), flags, chapter
//                (flags/chapter = Crystal Oracle extensions; skull + bench write 0)
//   pages 30..39 RESERVED for future use (never read or written, Format zeroes them)
//
//  "Played this year" = slot byte 0 != 0 (same rule as the skull), so a crystal
//  rolls ONCE per year across every station. Tiers keep the skull's meaning:
//  0 = trick, 1 = treat, 2 = rare (skull "big win"), 3 = legendary (jackpot).
//  Numbers stay inside the skull's bands so prizeTier(number) still agrees:
//  trick 1-39, treat 40-89, rare 90-99, legendary 100.
//
//  Safety: never write page 2 (lock bytes) or pages >= 40 (NTAG213 config /
//  NTAG203 counter). Blank tags are only "awakened" if their NDEF area is empty
//  or already our link — someone's random NFC sticker is never overwritten.
// ============================================================================
#include <Arduino.h>
#include "reader.h"

namespace token {

constexpr uint8_t PAGE_CC      = 3;
constexpr uint8_t PAGE_NDEF    = 4;
constexpr uint8_t NDEF_PAGES   = 12;   // 4..15
constexpr uint8_t PAGE_MARKER  = 16;
constexpr uint8_t PAGE_CHARGE  = 17;
constexpr uint8_t PAGE_YEAR0   = 18;
constexpr uint8_t MAX_YEARS    = 12;   // 18..29
constexpr uint8_t PAGE_RESERVED0 = 30;  // 30..39 reserved (see the header)
constexpr uint8_t MAGIC0 = 'S', MAGIC1 = 'K', SCHEMA = 0x03;

enum SlotFlags : uint8_t {
  SF_FIRST     = 0x01,   // this was the crystal's awakening (first ever visit)
  SF_CHARGED   = 0x02,   // the optional charge meter was filled
  SF_PITY      = 0x04,   // bad-luck protection forced rare
  SF_MILESTONE = 0x08,   // this visit hit a station milestone (100th, 200th ...)
  SF_CIRCLE    = 0x10,   // part of a 3+ crystal group
  SF_BRAVE     = 0x20,   // reserved (brave mode)
  SF_ORACLE    = 0x80,   // written by the Crystal Oracle (vs skull / bench = 0)
};

// Tier bands for the stored number (skull-compatible)
constexpr uint8_t BAND_LO[4] = {1, 40, 90, 100};
constexpr uint8_t BAND_HI[4] = {39, 89, 99, 100};
uint8_t tierForNumber(uint8_t n);      // the skull's prizeTier()

enum class Kind : uint8_t {
  Unreadable = 0,   // read failed / partial — ask to place it again
  Foreign,          // not ours and not blank (someone's NFC sticker, a bus pass ...) — never write
  Fresh,            // blank NTAG (no SK marker, empty NDEF or our link) — awaken it
  Ours,             // SK marker present
};

struct YearEntry { uint16_t year; uint8_t number, tier, flags, chapter; };

struct Crystal {
  Kind     kind = Kind::Unreadable;
  uint8_t  uid[TAG_UID_MAX] = {0};
  uint8_t  uidLen = 0;
  uint8_t  visits = 0;           // marker byte 3 (0 for fresh / bench-provisioned); raised to the
                                 // filled-slot count if an interrupted write left the marker behind
  uint8_t  charge = 0;           // page 17 byte 0
  uint8_t  color = 0;            // page 17 byte 1 (0 = unset)
  bool     hasCC = false;        // page 3 byte 0 == 0xE1
  bool     hasOurLink = false;   // pages 4.. hold this crystal's own link (crystalUrl)
  bool     playedThisYear = false;
  YearEntry thisYear = {0, 0, 0, 0, 0};
  uint8_t  historyCount = 0;     // filled slots, oldest first (includes this year if played)
  YearEntry history[MAX_YEARS];
  int8_t   lastTier = -1;        // most recent PRIOR year's tier (-1 = none)
  uint16_t lastYear = 0;         // that year
  uint8_t  noRareStreak = 0;     // consecutive most-recent prior visits with tier <= 1
  uint32_t anonId = 0;           // salted hash of the UID (the logs' crystal id + the link's /c/<id>)
  bool     firstVisit() const { return kind == Kind::Fresh || (kind == Kind::Ours && visits == 0); }
};

// The crystal's own link as stored on the tag (no "https://"): "<ndefUrl>/c/<anonId as 8 hex>",
// or the bare ndefUrl if that would pass 40 characters; "" if ndefUrl is empty. buf >= 64 bytes.
const char *crystalUrl(const char *ndefUrl, uint32_t anonId, char *buf, size_t size);

// Classify + decode a page image. Never fails: unreadable images come back as Kind::Unreadable.
void parse(const TagImage &img, uint16_t eventYear, uint16_t baseYear, Crystal &out);

// This year's visit as an ORDERED write list (marker page LAST). For first visits
// of Fresh crystals it also provisions: CC (only if missing), the NDEF link
// (pages 4..15) and page 17 = {0,0,0,0}. A crystal of ours whose link isn't its
// own yet (the bare site link, the old repo link) gets the link rewritten first. Returns the count, 0 on error (year out
// of range, or the crystal already played this year).
uint8_t buildVisitWrites(const Crystal &c, uint16_t eventYear, uint16_t baseYear,
                         uint8_t number, uint8_t tier, uint8_t flags, uint8_t chapter,
                         const char *ndefUrl, PageWrite *out, uint8_t max);

// ---- crystal prep (the host menu's "Crystals" page, service.cpp) -------------------------
// Ordered write lists for the tag whose page image is `img`, built like the bench
// (nfc_tag_bench) does it. Every builder honours the same guards:
//   * nothing (0) unless img.readOk - a list is never built from a partial image;
//   * only pages 3..39; page 3 (the OTP capability container) only when its byte 0 isn't 0xE1;
//   * the marker page 16 LAST (the commit), whenever it is written;
//   * Foreign tags are never touched, except by Format (that is how a sticker is reclaimed).
// They return the number of entries (0 = nothing to write; *why, if given, then says why
// in a few words for the screen). Lists longer than LINK_MAX_WRITES (Format: up to 36)
// are sent in order, LINK_MAX_WRITES at a time, each part verified before the next.
constexpr uint8_t PREP_MAX_WRITES = 40;
// A blank (Fresh) crystal -> provisioned: CC (only if missing, and only with a link), our
// NDEF link (pages 4..15, unless it is already there), page 17 = {0, colour, 0, 0}, marker
// {S, K, 03, 0} last. Refuses foreign tags. A crystal that is already ours keeps its visits:
// only its link is rewritten, if it isn't the crystal's own link yet (else "already provisioned").
uint8_t buildProvisionWrites(const TagImage &img, uint8_t colour, const char *ndefUrl, PageWrite *out,
                             uint8_t max, const char **why = nullptr);
// Set the bench colour (0 = none, 1..settings::CRYSTAL_COLORS_MAX). Ours: a read-modify-write
// of page 17 that keeps byte 0 (charge) and bytes 2..3 (+ the link rewrite, if it isn't its own yet). Fresh (no SK marker): provisions it
// first with that colour (= the bench's ensureProvisioned + colour). Foreign: refused.
uint8_t buildColorWrite(const TagImage &img, uint8_t colour, const char *ndefUrl, PageWrite *out, uint8_t max,
                        const char **why = nullptr);
// "Clear tonight": zero this year's slot (18 + year - baseYear) and, only if it was filled
// (byte 0 != 0) on a crystal that is ours, write the marker with visits - 1 (last). Lets a
// rehearsal crystal play again. An already-zero slot = nothing to write, unless an earlier
// clear stopped before its marker (an ours crystal whose marker counts more visits than it has
// filled slots): then only the corrected marker is written.
uint8_t buildClearYearWrites(const TagImage &img, uint16_t eventYear, uint16_t baseYear, PageWrite *out,
                             uint8_t max, const char **why = nullptr);
// "Format": zero every non-zero page of 4..39 (reserved pages too; the marker 16 last), like the bench's Erase.
// The crystal then reads as a blank (Fresh) one. CC page 3 is never touched (OTP).
uint8_t buildFormatWrites(const TagImage &img, PageWrite *out, uint8_t max, const char **why = nullptr);

// The colour the show uses, 1..settings::colorCount(): the bench-set colour if it is
// one of the defined colours, else a stable hash of the UID (1 if no colour is defined).
uint8_t displayColor(const Crystal &c);
// The bench-set colour if it is one of the defined colours (1..settings::colorCount()),
// else 0 = unset (a colour id the words don't define, e.g. a future filament).
uint8_t benchColor(const Crystal &c);
const char *kindName(Kind k);

}  // namespace token
