/*
 * ============================================================================
 *  board.cpp — Waveshare ESP32-S3-Touch-LCD-7C(-BOX) low-level board support
 * ----------------------------------------------------------------------------
 *  Implements board.h: the ONE I2C bus (GPIO47 SDA / GPIO48 SCL, 400 kHz) and
 *  the CH32V006 "IO EXTENSION" expander at 0x24 that gates LCD power + reset,
 *  backlight, touch reset, speaker amps, SD DAT3 and the STA LED.
 *
 *  I2C design (core 3.3.x "i2c-ng" HAL on top of the IDF i2c_master driver):
 *    * Wire.begin(47, 48, 400 kHz) is done here FIRST, so every library that
 *      later calls Wire.begin() with no pins (Adafruit BusIO, MFRC522v2 ...)
 *      gets "bus already started" instead of grabbing the variant's default
 *      pins 8/9 (which are LCD data lines on this board).
 *    * Our helpers talk to the bus through the HAL calls Wire itself uses
 *      (::i2cWrite / ::i2cWriteReadNonStop) with OUR OWN buffers. Each HAL call
 *      is one atomic, mutex-protected bus transaction, so the helpers are safe
 *      from any task/core and never touch Wire's shared rx buffer (which other
 *      Wire users on core 0 — the rfid2 reader — could overwrite between a
 *      requestFrom() and the read() calls).
 *    * A recursive board mutex additionally serialises our helpers, the
 *      expander's 16-bit output shadow (read-modify-write) and bus recovery.
 *    * Every transfer: 3 attempts; after a bus-level failure (timeout = SCL/SDA
 *      held, hung slave) the bus is cleared with i2c_master_bus_reset() (rate
 *      limited). Nothing here ever aborts or waits forever.
 *
 *  Expander protocol (Waveshare repo 73f8b90 + schematic, docs/DESIGN.md §2):
 *    reg 0x02 direction (16 bit, 1 = output), 0x03 output, 0x04 input,
 *    0x05 backlight PWM (1 byte, higher = brighter). Writes are [reg, lo, hi];
 *    reads are write [reg] + repeated-start read. The CH32 is NOT reset by an
 *    ESP software reset, so begin() rewrites outputs + direction every boot.
 * ============================================================================
 */
#include "board.h"
#include <Wire.h>
#include "esp32-hal-i2c.h"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace board {

namespace {

constexpr uint8_t  I2C_PORT        = 0;      // Wire == I2C port 0
constexpr uint32_t XFER_TIMEOUT_MS = 50;     // per transaction (same as Wire's default)
constexpr uint32_t PROBE_TIMEOUT_MS = 20;
constexpr int      TRIES           = 3;
constexpr TickType_t LOCK_WAIT     = pdMS_TO_TICKS(300);   // never block forever on our mutex
constexpr uint32_t RECOVER_MIN_GAP_MS = 500;              // automatic bus resets at most this often

// CH32V006 registers
constexpr uint8_t REG_DIR = 0x02;
constexpr uint8_t REG_OUT = 0x03;
constexpr uint8_t REG_IN  = 0x04;
constexpr uint8_t REG_PWM = 0x05;

// Safe output state: EXIO4 (SD D3) high, EXIO6 (LED, active low) high = off,
// everything else low (LCD bias off, LCD + touch held in reset, backlight off, amp off).
constexpr uint16_t SAFE_OUT = (1u << EXIO_SD_D3) | (1u << EXIO_LED);   // 0x0050

StaticSemaphore_t s_lockBuf;
SemaphoreHandle_t s_lock = nullptr;     // recursive: exio helpers call the i2c helpers
bool     s_i2cOk = false;               // Wire/HAL bus is up
bool     s_expOk = false;               // expander answered at boot
uint16_t s_shadow = SAFE_OUT;           // last value written (or wanted) on reg 0x03
uint8_t  s_blPct = 0;
uint32_t s_lastRecoverMs = 0;
uint32_t s_recoveries = 0;

// RAII guard for the board mutex (timed take; ok() == false -> caller gives up).
class Guard {
 public:
  Guard() : _ok(s_lock != nullptr && xSemaphoreTakeRecursive(s_lock, LOCK_WAIT) == pdTRUE) {}
  ~Guard() {
    if (_ok) xSemaphoreGiveRecursive(s_lock);
  }
  bool ok() const { return _ok; }

 private:
  bool _ok;
};

#if !CONFIG_DISABLE_HAL_LOCKS
// Wire's own transaction mutex is a protected member. A pointer-to-member formed
// through a derived class is the standard-conforming way to reach it, so bus
// recovery can also keep Wire-based libraries (MFRC522v2 on core 0) off the bus
// while it resets the controller. The class is never instantiated.
struct WirePeek : public TwoWire {
  static SemaphoreHandle_t TwoWire::*lockMember() { return &WirePeek::lock; }
};
SemaphoreHandle_t wireLock() { return Wire.*(WirePeek::lockMember()); }
#endif

// ---- raw single attempts (HAL, own buffers) ----
esp_err_t rawWrite(uint8_t addr, const uint8_t *d, size_t n) {
  return ::i2cWrite(I2C_PORT, addr, d, n, XFER_TIMEOUT_MS);
}
esp_err_t rawWriteRead(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
  size_t got = 0;
  esp_err_t e = ::i2cWriteReadNonStop(I2C_PORT, addr, w, wn, r, rn, XFER_TIMEOUT_MS, &got);
  if (e == ESP_OK && got != rn) e = ESP_FAIL;
  return e;
}
esp_err_t rawProbe(uint8_t addr) {
  return ::i2cWrite(I2C_PORT, addr, nullptr, 0, PROBE_TIMEOUT_MS);   // size 0 = i2c_master_probe
}

void recoverIfDue() {
  if (millis() - s_lastRecoverMs >= RECOVER_MIN_GAP_MS || s_recoveries == 0) i2cRecover();
}

// Runs `op` up to TRIES times. A timeout means the bus itself is in trouble
// (a slave holding SDA, SCL stretched forever): clear the bus once and try
// again. `alwaysRecover` does that for ANY failure (used for the expander,
// which is always present — a failure to reach it is never a plain NACK).
template <typename Op>
bool withRetries(Op op, bool alwaysRecover) {
  if (!s_i2cOk) return false;
  Guard g;
  if (!g.ok()) return false;
  esp_err_t e = ESP_FAIL;
  for (int i = 0; i < TRIES; i++) {
    e = op();
    if (e == ESP_OK) return true;
    if (e == ESP_ERR_INVALID_ARG) return false;   // caller error: repeating won't help
    delayMicroseconds(400);
  }
  if (e == ESP_ERR_TIMEOUT || alwaysRecover) {
    recoverIfDue();
    if (op() == ESP_OK) return true;
  }
  return false;
}

bool expWrite16(uint8_t reg, uint16_t v) {
  const uint8_t d[3] = {reg, (uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
  return withRetries([&]() { return rawWrite(ADDR_EXPANDER, d, sizeof(d)); }, true);
}

// Builds [reg bytes..., data...] in a stack buffer (heap for big writes) and sends it.
bool writeWithPrefix(uint8_t addr, const uint8_t *prefix, size_t np, const uint8_t *data, size_t n) {
  if (n && !data) return false;
  uint8_t stackBuf[66];
  uint8_t *buf = stackBuf;
  const size_t total = np + n;
  if (total > sizeof(stackBuf)) {
    buf = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) return false;
  }
  memcpy(buf, prefix, np);
  if (n) memcpy(buf + np, data, n);
  const bool ok = withRetries([&]() { return rawWrite(addr, buf, total); }, false);
  if (buf != stackBuf) heap_caps_free(buf);
  return ok;
}

}  // namespace

// ============================================================================
//  lifecycle
// ============================================================================
bool begin() {
  if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutexStatic(&s_lockBuf);

  // Pins first (so a later pin-less Wire.begin() from any library is a no-op
  // on THIS bus), then the bus itself.
  Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);
  s_i2cOk = Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_HZ);
  if (!s_i2cOk) {
    Serial.println("[board] Wire.begin(47,48) FAILED - no I2C bus");
    s_expOk = false;
    return false;
  }

  // Probe the expander: 5 tries, 10 ms apart (it may still be coming out of power-on).
  s_expOk = false;
  for (int i = 0; i < 5 && !s_expOk; i++) {
    if (i) delay(10);
    Guard g;
    s_expOk = g.ok() && rawProbe(ADDR_EXPANDER) == ESP_OK;
  }
  if (!s_expOk) {
    // One bus clear and a last try: a previous boot may have left a slave mid-byte.
    i2cRecover();
    Guard g;
    s_expOk = g.ok() && rawProbe(ADDR_EXPANDER) == ESP_OK;
  }
  if (!s_expOk) {
    Serial.println("[board] IO expander 0x24 not answering");
    return false;
  }

  // Safe state: outputs first (so pins that become outputs come up in the safe
  // level), then the direction mask, then outputs again.
  bool ok;
  {
    Guard g;
    s_shadow = SAFE_OUT;
    ok = expWrite16(REG_OUT, s_shadow);
    ok = expWrite16(REG_DIR, EXIO_DIR_MASK) && ok;
    ok = expWrite16(REG_OUT, s_shadow) && ok;
    s_blPct = 0;
  }
  Serial.printf("[board] expander ok, safe state %s (out=0x%04X dir=0x%04X)\n",
                ok ? "written" : "WRITE FAILED", s_shadow, EXIO_DIR_MASK);
  return true;
}

bool expanderOk() { return s_expOk; }

// ============================================================================
//  expander I/O
// ============================================================================
bool exioWrite(uint8_t pin, bool level) {
  if (pin > 15) return false;
  return exioWriteMask((uint16_t)(1u << pin), level ? 0xFFFF : 0x0000);
}

bool exioWriteMask(uint16_t mask, uint16_t levels) {
  if (!s_expOk) return false;
  Guard g;
  if (!g.ok()) return false;
  // The shadow keeps the WANTED state even if this write fails, so the next
  // successful write of any pin carries it.
  s_shadow = (uint16_t)((s_shadow & ~mask) | (levels & mask));
  return expWrite16(REG_OUT, s_shadow);
}

bool exioReadInputs(uint16_t &levels) {
  if (!s_expOk) return false;
  uint8_t d[2] = {0, 0};
  const uint8_t reg = REG_IN;
  if (!withRetries([&]() { return rawWriteRead(ADDR_EXPANDER, &reg, 1, d, 2); }, true)) return false;
  levels = (uint16_t)(d[0] | (d[1] << 8));
  return true;
}

uint16_t exioShadow() { return s_shadow; }

// ============================================================================
//  conveniences
// ============================================================================
void lcdPower(bool on) {
  if (on) {
    exioWrite(EXIO_LCD_VDD, true);    // AP3012 bias supply on
    delay(20);
    exioWrite(EXIO_LCD_RST, true);    // release panel reset
  } else {
    backlight(0);                     // a lit, unpowered panel is just a white glow
    exioWrite(EXIO_LCD_RST, false);
    delay(20);
    exioWrite(EXIO_LCD_VDD, false);
  }
}

void backlight(uint8_t pct) {
  Guard g;   // keep PWM + enable together if two tasks race
  if (pct == 0) {
    exioWrite(EXIO_BL_EN, false);
    s_blPct = 0;
    return;
  }
  if (pct < 5) pct = 5;               // Waveshare: never fully dark via PWM
  if (pct > 100) pct = 100;
  if (s_expOk) {
    const uint8_t d[2] = {REG_PWM, (uint8_t)((uint16_t)pct * 255 / 100)};
    withRetries([&]() { return rawWrite(ADDR_EXPANDER, d, sizeof(d)); }, true);
  }
  exioWrite(EXIO_BL_EN, true);
  s_blPct = pct;
}

uint8_t backlightPct() { return s_blPct; }

void touchReset(bool asserted) { exioWrite(EXIO_TP_RST, !asserted); }   // active low
void ampEnable(bool on) { exioWrite(EXIO_PA, on); }
void statusLed(bool on) { exioWrite(EXIO_LED, !on); }                  // active low

// ============================================================================
//  generic I2C helpers
// ============================================================================
bool i2cProbe(uint8_t addr) {
  if (!s_i2cOk || addr > 0x7F) return false;
  Guard g;
  if (!g.ok()) return false;
  // A clean NACK is a definite "not there"; only a bus timeout is worth retrying.
  for (int i = 0; i < TRIES; i++) {
    const esp_err_t e = rawProbe(addr);
    if (e == ESP_OK) return true;
    if (e != ESP_ERR_TIMEOUT) return false;
  }
  recoverIfDue();
  return rawProbe(addr) == ESP_OK;
}

bool i2cWrite(uint8_t addr, const uint8_t *data, size_t n) {
  if (!data || n == 0 || addr > 0x7F) return false;
  return withRetries([&]() { return rawWrite(addr, data, n); }, false);
}

bool i2cWriteReg8(uint8_t addr, uint8_t reg, const uint8_t *data, size_t n) {
  if (addr > 0x7F) return false;
  return writeWithPrefix(addr, &reg, 1, data, n);
}

bool i2cWriteReg16(uint8_t addr, uint16_t reg, const uint8_t *data, size_t n) {
  if (addr > 0x7F) return false;
  const uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};   // big-endian (GT911)
  return writeWithPrefix(addr, r, 2, data, n);
}

bool i2cReadReg8(uint8_t addr, uint8_t reg, uint8_t *data, size_t n) {
  if (!data || n == 0 || addr > 0x7F) return false;
  return withRetries([&]() { return rawWriteRead(addr, &reg, 1, data, n); }, false);
}

bool i2cReadReg16(uint8_t addr, uint16_t reg, uint8_t *data, size_t n) {
  if (!data || n == 0 || addr > 0x7F) return false;
  const uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
  return withRetries([&]() { return rawWriteRead(addr, r, 2, data, n); }, false);
}

int i2cScan(uint8_t *found, int maxFound) {
  if (!s_i2cOk) return 0;
  int n = 0;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    bool hit;
    {
      Guard g;   // per address, so other tasks can interleave during a scan
      hit = g.ok() && rawProbe(a) == ESP_OK;
    }
    if (hit) {
      if (found && n < maxFound) found[n] = a;
      n++;
    }
  }
  return n;
}

void i2cRecover() {
  if (!s_i2cOk) return;
  Guard g;   // our own helpers are held off (even if the take timed out we still try)
  bool wireHeld = false;
#if !CONFIG_DISABLE_HAL_LOCKS
  SemaphoreHandle_t wl = wireLock();
  if (wl) wireHeld = xSemaphoreTake(wl, pdMS_TO_TICKS(200)) == pdTRUE;
#endif
  i2c_master_bus_handle_t bus = (i2c_master_bus_handle_t)i2cBusHandle(I2C_PORT);
  const esp_err_t e = bus ? i2c_master_bus_reset(bus) : ESP_ERR_INVALID_STATE;
#if !CONFIG_DISABLE_HAL_LOCKS
  if (wireHeld) xSemaphoreGive(wl);
#endif
  s_lastRecoverMs = millis();
  s_recoveries++;
  Serial.printf("[board] I2C bus reset #%lu: %s\n", (unsigned long)s_recoveries, esp_err_to_name(e));
}

// ============================================================================
//  misc
// ============================================================================
uint32_t freeInternal() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
uint32_t freePsram() { return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

}  // namespace board
