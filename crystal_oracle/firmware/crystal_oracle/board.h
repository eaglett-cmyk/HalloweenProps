#pragma once
// ============================================================================
//  board.h — Waveshare ESP32-S3-Touch-LCD-7C(-BOX) low-level board support.
//  Owns the ONE I2C bus (GPIO47/48 via Arduino Wire, core 3.3.x i2c-ng driver)
//  and the CH32V006 "IO EXTENSION" expander at 0x24, which gates the LCD power,
//  LCD reset, backlight, touch reset, speaker amp, SD DAT3 and the STA LED.
//
//  Facts (Waveshare repo 73f8b90 + schematic netlist, see docs/DESIGN.md §2):
//    expander regs: 0x02 direction (16-bit, 1 = output) | 0x03 output (16-bit)
//                   0x04 input (read 2 bytes LE)        | 0x05 backlight PWM (1 byte 0..255)
//    writes are [reg, lo, hi]; reads are write [reg] + repeated-start read.
//    The expander is NOT reset by an ESP software reset (shares the EN net only
//    with the RESET key) -> begin() must rewrite direction + outputs every boot.
//    Every write is a whole-port write -> keep a 16-bit shadow.
// ============================================================================
#include <Arduino.h>

namespace board {

// ---- ESP32-S3 GPIO map (every exposed GPIO is used by the board) ----
constexpr int PIN_I2C_SDA  = 47;   // 1.8 V domain, level-shifted to 3.3 V on the bus side
constexpr int PIN_I2C_SCL  = 48;
constexpr int PIN_TP_INT   = 4;    // GT911 INT (also address-select during reset)
constexpr int PIN_SD_CLK   = 12;   // TF card, SD-bus 1-bit mode (D1/D2 not connected)
constexpr int PIN_SD_CMD   = 11;
constexpr int PIN_SD_D0    = 13;
constexpr int PIN_I2S_MCLK = 6;    // ES8389 / ES7210
constexpr int PIN_I2S_BCLK = 44;
constexpr int PIN_I2S_WS   = 16;
constexpr int PIN_I2S_DOUT = 15;   // to ES8389 (speakers)
constexpr int PIN_I2S_DIN  = 43;   // from ES7210 (mics) — also the ROM boot-log TX; never Serial0.begin()

// ---- I2C 7-bit addresses on the shared bus ----
constexpr uint8_t ADDR_EXPANDER = 0x24;   // CH32V006 — SAME as a PN532, which is why the reader lives in the plinth
constexpr uint8_t ADDR_ES8389   = 0x10;
constexpr uint8_t ADDR_ES7210   = 0x40;
constexpr uint8_t ADDR_RTC      = 0x51;   // PCF85063A
constexpr uint8_t ADDR_FUEL     = 0x55;   // BQ27220 (only answers with a battery fitted)
constexpr uint8_t ADDR_GT911    = 0x5D;
constexpr uint8_t ADDR_GT911_B  = 0x14;
constexpr uint8_t ADDR_RFID2    = 0x28;   // optional M5Stack Unit RFID2 on the P1 screw terminal

constexpr uint32_t I2C_HZ = 400000;

// ---- IO expander pins (EXIOn) ----
enum Exio : uint8_t {
  EXIO_LCD_RST = 0,   // panel reset (10k pull-up), active low
  EXIO_TP_RST  = 1,   // GT911 reset, active low
  EXIO_BL_EN   = 2,   // backlight boost enable (SY7200A EN)
  EXIO_PA      = 3,   // speaker amps enable (AND-ed with NOT headphone-detect in hardware)
  EXIO_SD_D3   = 4,   // TF card DAT3/CS: keep HIGH for SD-bus mode
  EXIO_LCD_VDD = 5,   // panel bias supply (AP3012 SHDN), 10k pull-down
  EXIO_LED     = 6,   // STA LED, ACTIVE LOW
  EXIO_P1_9    = 7,   // EXIO7..EXIO10 = user INPUTS on P1 pins 9..12 (3.3 V logic, add 10k pull-up)
  EXIO_P1_10   = 8,
  EXIO_P1_11   = 9,
  EXIO_P1_12   = 10,
  EXIO_P1_13   = 11,  // EXIO11..EXIO14 = user OUTPUTS on P1 pins 13..16 (logic only — drive a MOSFET/relay module)
  EXIO_P1_14   = 12,
  EXIO_P1_15   = 13,
  EXIO_P1_16   = 14,
  EXIO_RTC_INT = 15,  // input (PCF85063A INT, open drain) — never drive
};
constexpr uint16_t EXIO_DIR_MASK = 0x787F;   // 1 = output: EXIO0-6 + EXIO11-14

// ---- lifecycle ----
// Starts Wire on 47/48 @400 kHz (MUST be the first I2C init in the program, so no
// library later calls Wire.begin() with the variant's default pins 8/9 = LCD lines),
// probes the expander (5 tries) and puts every expander output in a SAFE state:
// LCD power off, LCD + touch in reset, backlight off, amp off, SD DAT3 high, LED off.
// Returns false if the expander never answers (no display possible; Serial still works).
bool begin();
bool expanderOk();

// ---- expander I/O ----
bool exioWrite(uint8_t pin, bool level);          // updates the shadow + writes reg 0x03
bool exioWriteMask(uint16_t mask, uint16_t levels);   // several pins in one transaction
bool exioReadInputs(uint16_t &levels);            // reg 0x04 (inputs EXIO7..10, EXIO15)
uint16_t exioShadow();

// ---- conveniences ----
void lcdPower(bool on);       // EXIO5 then EXIO0 with the delays the panel needs
void backlight(uint8_t pct);  // 0 = off (EXIO2 low); 1..100 = on + PWM (clamped to >= 5 %)
uint8_t backlightPct();
void touchReset(bool asserted);
void ampEnable(bool on);
void statusLed(bool on);

// ---- generic I2C helpers (all error-returning; never abort) ----
bool i2cProbe(uint8_t addr);
bool i2cWrite(uint8_t addr, const uint8_t *data, size_t n);                // plain write
bool i2cWriteReg8(uint8_t addr, uint8_t reg, const uint8_t *data, size_t n);
bool i2cReadReg8(uint8_t addr, uint8_t reg, uint8_t *data, size_t n);     // 8-bit register, repeated start
bool i2cReadReg16(uint8_t addr, uint16_t reg, uint8_t *data, size_t n);   // 16-bit register (GT911)
bool i2cWriteReg16(uint8_t addr, uint16_t reg, const uint8_t *data, size_t n);
int  i2cScan(uint8_t *found, int maxFound);       // 0x08..0x77
void i2cRecover();                                // bus clear after repeated NACK/timeouts

// ---- misc ----
uint32_t freeInternal();   // bytes of free internal RAM
uint32_t freePsram();

}  // namespace board
