/*
 * ============================================================================
 *  SKULL ORACLE  —  Halloween prop firmware  (v4: ESP32-S3, WiFi-ready)
 *  Board : Adafruit Feather ESP32-S3 (4MB Flash / 2MB PSRAM, #5477)
 *          (install the "esp32" boards package by Espressif, v3.0+, via
 *           Tools > Board > Boards Manager; select board
 *           "Adafruit Feather ESP32-S3 2MB PSRAM")
 * ----------------------------------------------------------------------------
 *  WHAT IT DOES
 *    Idle    : the eyes "breathe" with a slow glow.
 *    Trigger : a hand in the skull's mouth trips the VCNL4010 proximity sensor.
 *    On trigger, if there's an NFC token in that hand:
 *       - a fresh token gets fully set up: the GitHub link is written (so a
 *         PHONE TAP opens the repo) and this year's visit is recorded
 *       - a returning token has this year added to its history; veterans
 *         (3rd year+) get a bonus roll, so loyalty pays off
 *       - the label prints a welcome tier, the number drawn, the prize, and
 *         the kid's full year-by-year history
 *    No token -> a plain random 1-100 number (the classic behavior).
 *
 *    Everything lives ON THE TOKEN, so a wiped/replaced board never erases a
 *    kid's streak. The phone-tappable link and the prop's private visit data
 *    sit in separate regions of the same tag and never collide.
 *
 *  DATA BUDGET (NTAG203 user memory = pages 4..39 = 144 bytes):
 *       pages  4..15 : NDEF URI record = the GitHub link        48 bytes
 *       page   16    : marker "SK" + schema + visit count         4 bytes
 *       pages 17..39 : one page per year -> [number, prize, ...] 92 bytes
 *    A 5-year veteran uses ~70 bytes; the layout holds 23 years of history
 *    before it fills. Your existing tags are plenty — no upgrade needed.
 *
 *  HARDWARE / WIRING   (GPIO numbers; Feather ESP32-S3 silk label in [brackets].
 *  Handy: on this board the D-labels equal the GPIO numbers — D6=GPIO6, etc.)
 *  --------------------------------------------------------------------------
 *    I2C bus (Wire) — shared by both I2C devices; easiest via the STEMMA QT jack,
 *    else the SDA / SCL pins. (Code powers the QT port automatically.)
 *    VCNL4010 proximity sensor (I2C 0x13):
 *        VIN -> 3.3V   GND -> GND   SDA -> SDA   SCL -> SCL
 *        (If read range feels weak, move VIN to USB/5V for a stronger IR LED.)
 *    PN532 NFC board, set to I2C MODE (I2C 0x24):
 *        VCC -> 3.3V   GND -> GND   SDA -> SDA   SCL -> SCL
 *        IRQ -> GP6 [D6]      RSTO/RST -> GP9 [D9]
 *        (If I2C is ever flaky, set PN532_USE_SPI = 1 and rewire to SPI pins.)
 *    DP-EH400 thermal printer (TTL serial header — NOT the USB port):
 *        Printer RX  <- Feather TX pin [TX]
 *        Printer DTR -> GP11 [D11]   (optional, see USE_DTR)
 *        Printer power: SEPARATE 5-9V supply, 2A+, GROUND SHARED with Feather.
 *    Eyes (your existing LEDs):
 *        GP10 [D10] -> resistor -> LED(s) -> GND
 *    Buttons (each wired between its pin and GND; internal pull-up, pressed = LOW):
 *        Inside the mouth -> GP12 [D12]  : adds +10 to the roll (capped at 100)
 *        Outside the skull -> GP13 [D13] : a 2nd way to trigger a roll/print
 *
 *  LIBRARIES  (Tools > Manage Libraries — install all three):
 *        "Adafruit VCNL4010" | "Adafruit PN532" | "Adafruit Thermal Printer Library"
 *
 *  WIFI: this board has WiFi/BLE but the prop doesn't use them yet. To add it
 *        later (NTP auto-year, visit logging, a leaderboard), #include <WiFi.h>
 *        and connect in setup(). Keep credentials OUT of this file / the repo.
 *
 *  THINGS TO TUNE  (search the file for "TUNE"):
 *        PRINTER_BAUD  - 9600 first; 19200 if it prints garbage
 *        PROX_MARGIN   - hand-vs-empty threshold (watch Serial Monitor @115200)
 *        EVENT_YEAR    - bump each Halloween (drives which history slot is used)
 *        BASE_YEAR     - the year that maps to the first history slot; set once
 *        PROP_WRITES_URL - 1 = prop writes the link to fresh tokens (default);
 *                          0 = you pre-write the link at the bench with NFC
 *                          Tools + the ACR1252 (makes first-time taps instant)
 *        Prize tiers / names, and the veteran bonus, are all editable below.
 *  ============================================================================
 */

#include <Wire.h>
#include <SPI.h>
#include <string.h>
#include "Adafruit_VCNL4010.h"
#include <Adafruit_PN532.h>
#include "Adafruit_Thermal.h"

// ------------------------------- configuration -----------------------------
#define DEBUG          1          // 1 = print proximity + NFC info to USB Serial

// --- printer ---
#define PRINTER_BAUD   9600       // TUNE: 9600, else 19200 if output is garbled
#define USE_DTR        0          // TUNE: 1 only if the printer DTR wire is wired
#define DTR_PIN        11         // GP11 [D11]

// --- proximity trigger ---
#define PROX_MARGIN    600        // TUNE: counts above empty baseline = trigger
#define PROX_REARM     200        // must fall back under baseline+this to re-arm
#define COOLDOWN_MS    4000UL     // ignore new triggers for this long after a print

// --- NFC interface select ---
#define PN532_USE_SPI  0          // 0 = I2C (default) | 1 = SPI
#define PN532_IRQ      6          // GP6  [D6]   (I2C)
#define PN532_RESET    9          // GP9  [D9]   (I2C)
#define PN532_SCK     SCK         // board SCK pin   (SPI)
#define PN532_MISO    MISO        // board MI pin    (SPI)
#define PN532_MOSI    MOSI        // board MO pin    (SPI)
#define PN532_SS       5          // D5 [GPIO5]      (SPI chip-select)

// --- eyes ---
#define EYES_PIN       10         // GP10 [D10]

// --- buttons (wire each between its pin and GND; internal pull-up, pressed = LOW) ---
#define MOUTH_BUTTON_PIN 12       // GP12 [D12] - inside the mouth: +10 to the roll (cap 100)
#define EXTRA_BUTTON_PIN 13       // GP13 [D13] - outside the skull: 2nd trigger (roll/print)
#define MOUTH_BONUS     10        // how much the in-mouth button adds
#define NUMBER_MAX      100       // hard ceiling on the printed number

// --- calendar ---
#define EVENT_YEAR     2026       // TUNE: update each year
#define BASE_YEAR      2024       // TUNE: year mapped to the first history slot

// --- the GitHub repo (folded in) ---
//   Stored WITHOUT the "https://" — the NDEF prefix code 0x04 adds it back, so
//   a phone tap opens https://github.com/eaglett-cmyk/HalloweenProps
#define REPO_PATH      "github.com/eaglett-cmyk/HalloweenProps"
#define PROP_WRITES_URL 1         // see header notes

// --- behavior knobs ---
#define VETERAN_YEARS  2          // prior years needed before the bonus roll kicks in
#define PRINT_HISTORY  1          // 1 = print the kid's year-by-year history

// --- token memory layout (NTAG203 user pages 4..39) ---
#define NDEF_START_PAGE 4         // pages 4..15 hold the link
#define MARKER_PAGE     16        // "SK" + schema + visit count
#define YEAR_BASE_PAGE  17        // slot for BASE_YEAR; +1 per year after
#define MAX_YEARS       23        // pages 17..39
#define MAGIC0          0x53      // 'S'
#define MAGIC1          0x4B      // 'K'
#define SCHEMA_VER      0x02

// --- prize tiers by drawn number (TUNE freely) ---
//   returns a tier 0..3 that gets stored on the tag and printed
uint8_t prizeTier(uint8_t n) {
  if (n == 100)      return 3;    // jackpot
  if (n >= 90)       return 2;    // big win
  if (n >= 40)       return 1;    // treat
  return 0;                       // trick / consolation
}
const char* prizeName(uint8_t tier) {
  switch (tier) {
    case 3:  return "*** JACKPOT ***";
    case 2:  return "BIG WIN";
    case 1:  return "A TREAT";
    default: return "a trick...";
  }
}

// --------------------------------- objects ---------------------------------
Adafruit_VCNL4010 vcnl;

#if PN532_USE_SPI
Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
#else
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);     // I2C mode, uses Wire
#endif

#if USE_DTR
Adafruit_Thermal printer(&Serial1, DTR_PIN);
#else
Adafruit_Thermal printer(&Serial1);
#endif

// --------------------------------- state -----------------------------------
uint16_t      proxBaseline = 0;
bool          armed        = true;
unsigned long lastFire     = 0;
bool          seeded       = false;

// --------------------------------- helpers ---------------------------------
void    handleTag(bool mouthBonus);
uint8_t drawNumber(uint8_t priorYears);
uint8_t applyBonus(uint8_t n, bool bonus);
void    writeNdefUrl();
bool    readPage(uint8_t page, uint8_t *buf);
bool    writePage(uint8_t page, uint8_t *buf);
void    printReward(uint8_t visits, uint8_t number, uint8_t tier);
void    printNoToken(uint8_t number);
void    eyesBreathe();
void    eyesFlare(uint16_t ms);
void    failBlink(const char *msg);

// ===========================================================================
void setup() {
  pinMode(EYES_PIN, OUTPUT);
  pinMode(MOUTH_BUTTON_PIN, INPUT_PULLUP);   // pressed = LOW
  pinMode(EXTRA_BUTTON_PIN, INPUT_PULLUP);   // pressed = LOW

  if (DEBUG) { Serial.begin(115200); delay(300); Serial.println(F("Skull Oracle v4 boot")); }

  // ESP32-S3 Feather: the STEMMA QT / I2C bus has a power gate — turn it on
  // before talking to the sensors. (Usually automatic, but make it explicit.)
#if defined(PIN_I2C_POWER)
  pinMode(PIN_I2C_POWER, OUTPUT);  digitalWrite(PIN_I2C_POWER, HIGH);  delay(10);
#elif defined(TFT_I2C_POWER)
  pinMode(TFT_I2C_POWER, OUTPUT);  digitalWrite(TFT_I2C_POWER, HIGH);  delay(10);
#endif
  Wire.begin();

  // thermal printer on Serial1, mapped to the Feather's RX/TX pins
  Serial1.begin(PRINTER_BAUD, SERIAL_8N1, RX, TX);
  printer.begin();

  if (!vcnl.begin()) failBlink("VCNL4010 not found - check I2C wiring");
  if (DEBUG) Serial.println(F("VCNL4010 OK"));

  nfc.begin();
  if (!nfc.getFirmwareVersion()) failBlink("PN532 not found - check mode/wiring");
  nfc.SAMConfig();
  if (DEBUG) Serial.println(F("PN532 OK"));

  uint32_t sum = 0;                 // KEEP THE MOUTH CLEAR AT POWER-ON
  for (int i = 0; i < 16; i++) { sum += vcnl.readProximity(); delay(20); }
  proxBaseline = sum / 16;
  if (DEBUG) { Serial.print(F("Proximity baseline: ")); Serial.println(proxBaseline); }
}

// ===========================================================================
void loop() {
  eyesBreathe();

  uint16_t prox = vcnl.readProximity();
  if (DEBUG) {
    static unsigned long t = 0;
    if (millis() - t > 500) { t = millis(); Serial.print(F("prox=")); Serial.println(prox); }
  }

  if (!armed && prox < proxBaseline + PROX_REARM
            && digitalRead(EXTRA_BUTTON_PIN) == HIGH      // button released
            && millis() - lastFire > COOLDOWN_MS) {
    armed = true;                   // re-arm once the hand clears / button is released
  }

  // Trigger = hand in the mouth (proximity) OR a press of the outside-skull button.
  bool triggered = (prox > proxBaseline + PROX_MARGIN)
                || (digitalRead(EXTRA_BUTTON_PIN) == LOW);

  if (armed && triggered) {
    armed    = false;
    lastFire = millis();
    if (DEBUG) { Serial.print(F("trigger: "));
                 Serial.println(digitalRead(EXTRA_BUTTON_PIN) == LOW ? "button" : "hand"); }
    if (!seeded) { randomSeed(micros()); seeded = true; }
    eyesFlare(150);

    // ~700ms window: give the kid a moment to press the in-mouth +10 button,
    // and look for an NFC token in that same hand at the same time.
    bool    mouthBonus = false, tagFound = false;
    uint8_t uid[7], uidLen;
    unsigned long t0 = millis();
    while (millis() - t0 < 700) {
      if (digitalRead(MOUTH_BUTTON_PIN) == LOW) mouthBonus = true;          // latched
      if (!tagFound)
        tagFound = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 100);
    }
    if (DEBUG && mouthBonus) Serial.println(F("mouth button: +10"));

    if (tagFound) {
      handleTag(mouthBonus);
    } else {
      printNoToken(applyBonus(random(1, 101), mouthBonus));   // classic behavior + bonus
      if (DEBUG) Serial.println(F("no token"));
    }
    eyesFlare(150);
  }
}

// ---------------------- the brains: one token interaction ------------------
void handleTag(bool mouthBonus) {
  uint8_t marker[4];
  if (!readPage(MARKER_PAGE, marker)) {       // tag unreadable -> classic behavior
    printNoToken(applyBonus(random(1, 101), mouthBonus));
    return;
  }

  bool    ours    = (marker[0] == MAGIC0 && marker[1] == MAGIC1);
  uint8_t visits  = ours ? marker[3] : 0;     // years attended so far
  int     yearIdx = (int)EVENT_YEAR - (int)BASE_YEAR;
  uint8_t slotPage = (uint8_t)(YEAR_BASE_PAGE + yearIdx);
  bool    slotValid = (yearIdx >= 0 && yearIdx < MAX_YEARS);

  // already played this year? (idempotent — reprint, don't double-count)
  uint8_t slot[4] = {0,0,0,0};
  if (slotValid) readPage(slotPage, slot);
  bool playedThisYear = (slot[0] != 0);

  uint8_t number, tier;
  if (playedThisYear) {
    number = slot[0];
    tier   = slot[1];
  } else {
    number = applyBonus(drawNumber(visits), mouthBonus);   // veteran roll, then +10 if pressed
    tier   = prizeTier(number);

    if (!ours) {                              // FRESH TOKEN -> provision it
#if PROP_WRITES_URL
      writeNdefUrl();                         // pages 3..15 (link is now phone-tappable)
#endif
      visits = 0;
    }

    if (slotValid) {                          // record this year's result
      uint8_t newSlot[4] = { number, tier, 0, 0 };
      writePage(slotPage, newSlot);
    }
    if (visits < 255) visits++;               // count this year

    uint8_t newMarker[4] = { MAGIC0, MAGIC1, SCHEMA_VER, visits };
    writePage(MARKER_PAGE, newMarker);        // COMMIT LAST (safe if interrupted)
  }

  if (DEBUG) {
    Serial.print(F("token: visits=")); Serial.print(visits);
    Serial.print(F(" number=")); Serial.print(number);
    Serial.print(F(" tier="));   Serial.println(tier);
  }
  printReward(visits, number, tier);
}

// veterans (>= VETERAN_YEARS prior years) get the better of two rolls
uint8_t drawNumber(uint8_t priorYears) {
  uint8_t n = random(1, 101);
  if (priorYears >= VETERAN_YEARS) {
    uint8_t n2 = random(1, 101);
    if (n2 > n) n = n2;
  }
  return n;
}

// in-mouth button adds MOUTH_BONUS, clamped to NUMBER_MAX
uint8_t applyBonus(uint8_t n, bool bonus) {
  int v = (int)n + (bonus ? MOUTH_BONUS : 0);
  if (v > NUMBER_MAX) v = NUMBER_MAX;
  return (uint8_t)v;
}

// ------------------------- NDEF link writer (fresh tags) -------------------
//  Writes the Capability Container + a single NDEF URI record for REPO_PATH
//  into pages 3..15. The prop never touches these pages again, so the
//  phone-tappable link can't be corrupted by visit-data writes.
void writeNdefUrl() {
  uint8_t cc[4] = { 0xE1, 0x10, 0x12, 0x00 };   // NTAG203 CC: NDEF, 144 bytes
  writePage(3, cc);

  uint8_t buf[48];
  memset(buf, 0, sizeof(buf));
  uint8_t urlLen     = strlen(REPO_PATH);        // 38
  uint8_t payloadLen = urlLen + 1;               // + URI prefix byte
  uint8_t i = 0;
  buf[i++] = 0x03;                 // NDEF Message TLV
  buf[i++] = payloadLen + 4;       // TLV length (record header 4 + payload)
  buf[i++] = 0xD1;                 // record header: MB|ME|SR, TNF=well-known
  buf[i++] = 0x01;                 // type length
  buf[i++] = payloadLen;           // payload length
  buf[i++] = 0x55;                 // type 'U' (URI)
  buf[i++] = 0x04;                 // URI prefix code: https://
  memcpy(&buf[i], REPO_PATH, urlLen);
  i += urlLen;
  buf[i++] = 0xFE;                 // Terminator TLV (phones stop reading here)

  for (uint8_t p = 0; p < 12; p++) writePage(NDEF_START_PAGE + p, &buf[p * 4]);
}

// thin wrappers so the page calls read cleanly above
bool readPage(uint8_t page, uint8_t *buf)  { return nfc.mifareultralight_ReadPage(page, buf); }
bool writePage(uint8_t page, uint8_t *buf) { return nfc.mifareultralight_WritePage(page, buf); }

// ------------------------------- the printout ------------------------------
//  Everything between CUSTOMIZE markers is yours to reword.
void printReward(uint8_t visits, uint8_t number, uint8_t tier) {
  printer.wake();
  printer.justify('C');

  // ---- CUSTOMIZE: greeting by how many years they've returned ----
  printer.setSize('S');
  printer.boldOn();
  if (visits <= 1)      printer.println(F("WELCOME, BRAVE SOUL"));
  else if (visits == 2) printer.println(F("WELCOME BACK"));
  else if (visits == 3) printer.println(F("THE SKULL REMEMBERS"));
  else                  printer.println(F("MASTER OF THE SKULL"));
  printer.boldOff();
  if (visits >= 2) {
    printer.print(F("Year "));
    printer.print(visits);
    printer.println(F(" returning"));
  }
  // ---- end CUSTOMIZE ----

  printer.feed(1);                 // the number — classic core
  printer.setSize('L');
  printer.boldOn();
  printer.print(F("No. "));
  printer.println(number);
  printer.boldOff();

  printer.setSize('S');            // the prize
  printer.print(F("Prize: "));
  printer.println(prizeName(tier));

#if PRINT_HISTORY
  // walk the year slots and print every year they attended
  printer.feed(1);
  printer.boldOn();
  printer.println(F("-- YOUR HISTORY --"));
  printer.boldOff();
  int upTo = (int)EVENT_YEAR - (int)BASE_YEAR;
  if (upTo >= MAX_YEARS) upTo = MAX_YEARS - 1;
  for (int y = 0; y <= upTo; y++) {
    uint8_t s[4];
    if (readPage(YEAR_BASE_PAGE + y, s) && s[0] != 0) {
      printer.print(BASE_YEAR + y);
      printer.print(F("   No. "));
      printer.print(s[0]);
      printer.print(F("   "));
      printer.println(prizeName(s[1]));
    }
  }
#endif

  printer.feed(1);
  printer.print(F("Halloween "));
  printer.println(EVENT_YEAR);
  printer.feed(3);
  printer.setDefault();
  printer.sleep();
}

void printNoToken(uint8_t number) {
  printer.wake();
  printer.justify('C');
  printer.setSize('S');
  printer.boldOn();
  printer.println(F("THE SKULL HAS SPOKEN"));
  printer.boldOff();
  printer.println(F("Keep your token and"));
  printer.println(F("return next Halloween..."));
  printer.feed(1);
  printer.setSize('L');
  printer.boldOn();
  printer.print(F("No. "));
  printer.println(number);
  printer.boldOff();
  printer.setSize('S');
  printer.print(F("Prize: "));
  printer.println(prizeName(prizeTier(number)));
  printer.feed(1);
  printer.print(F("Halloween "));
  printer.println(EVENT_YEAR);
  printer.feed(3);
  printer.setDefault();
  printer.sleep();
}

// --------------------------------- eyes ------------------------------------
void eyesBreathe() {
  float phase = (millis() % 4000) / 4000.0f * 6.2832f;
  int   level = (int)(80 + 60 * (sin(phase) * 0.5f + 0.5f));
  analogWrite(EYES_PIN, level);
}
void eyesFlare(uint16_t ms) { analogWrite(EYES_PIN, 255); delay(ms); }

void failBlink(const char *msg) {
  if (DEBUG) { Serial.print(F("FATAL: ")); Serial.println(msg); }
  while (true) {
    analogWrite(EYES_PIN, 255); delay(150);
    analogWrite(EYES_PIN, 0);   delay(850);
  }
}
