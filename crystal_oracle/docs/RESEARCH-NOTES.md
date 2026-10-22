# Critic report: Waveshare ESP32-S3-Touch-LCD-7C-BOX "Crystal Oracle" station

I checked every value below against primary sources: Waveshare repo `waveshareteam/ESP32-S3-Touch-LCD-7C` at `73f8b90`, the schematic netlist, the factory firmware binary in that repo, the installed core 3.3.10, and the library sources at their release tags. I also pulled Waveshare's CH32 expander component from the Espressif registry. My working files are in `C:\Users\Ciancio\AppData\Local\Temp\crystal-oracle-research\critic\`: the two driver versions in `ch32_v2\` and `ch32_v102\`, plus `comp.json`. Nothing was run.

---

## 1. Resolved facts

### 1.1 Module, flash and PSRAM

**The module is ESP32-S3-WROOM-2-N32R16V: 32 MB octal flash, 16 MB octal PSRAM, VDD_SPI at 1.8 V.**
- Schematic: U10 is printed as `ESP32-S3-WROOM-2-N32R16V`.
- Product page: names "ESP32-S3-WROOM-2-N32R16" (https://www.waveshare.com/esp32-s3-touch-lcd-7c-box.htm).
- Module pins 28–30 (IO35/36/37) are on single-pin nets, i.e. not connected.

**Waveshare's factory image was built in OPI mode.** File: `firmware/ESP32-S3-Touch-LCD-7C-Test.bin`, 21.7 MB.
- At offset 0x0 the image header is `e9 03 03 5f`. Mode byte 3 means DOUT, and 0x5f means 32 MB at 80 MHz.
- The app image at 0x200000 has header `e9 06 03 5f`, with the same values.
- The app descriptor reads: project `phone_touch_lcd_4_3c`, built `May 29 2026`, IDF `v5.5.4`.
- DOUT is how an OPI build is encoded. In `boards.txt`, `esp32s3.menu.FlashMode.opi.build.flash_mode=dout`, while `qio` maps to `dio`.

**Arduino settings.** These are verified against `boards.txt` and the precompiled sdkconfigs in 3.3.10.
- Primary: FQBN `esp32:esp32:esp32s3:FlashMode=opi,PSRAM=opi,FlashSize=32M,PartitionScheme=app5M_fat24M_32MB,CDCOnBoot=cdc,USBMode=hwcdc,CPUFreq=240`.
  - This resolves to `memory_type=opi_opi`.
  - That config sets `CONFIG_ESPTOOLPY_OCT_FLASH 1` and `FLASH_SAMPLE_MODE_DTR`.
  - The bootloader used is `bootloader_opi_80m.elf`.
  - `esp32s3-octal` with the same menu values resolves to the identical `opi_opi`.
- Fallback: Waveshare's documented Arduino setting, `FlashMode=qio`.
  - This resolves to `qio_opi` with `CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT 1`, which switches to OPI at boot.
- The partition key `app5M_fat24M_32MB` exists for both boards.

**Things the Arduino precompiled libs do not set, and that cannot be changed from the IDE:**
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA` and `XIP_FROM_PSRAM`.
- `CONFIG_LCD_RGB_ISR_IRAM_SAFE`.

**What they do set:**
- `CONFIG_LCD_RGB_RESTART_IN_VSYNC 1`
- `CONFIG_I2S_ISR_IRAM_SAFE 1`
- `CONFIG_SPIRAM_USE_MALLOC 1`
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096`
- `DATA_CACHE_LINE_32B`
- `CONFIG_ESP_CONSOLE_UART_DEFAULT 1`, with USB-Serial-JTAG as the secondary console.

**Boot log on the I2S line.** The ROM and IDF boot log goes out on GPIO43, which is the I2S DIN line from the ES7210. This is the vendor's design and needs no action. Never call `Serial0.begin()`.

### 1.2 IO expander (CH32V006K8U7, 7-bit address 0x24)

**Registers**, from the vendor source in Arduino and IDF and from ESPHome's `waveshare_io_ch32v003`:

| Register | Function | Access |
|---|---|---|
| `0x02` | Direction | 1 = output |
| `0x03` | Output | write |
| `0x04` | Input | read |
| `0x05` | PWM | 1 byte |
| `0x06` | ADC | read 2 bytes, little-endian |
| `0x07` | RTC_INT | read; 0 = alarm |

**Write formats.**
- 16-bit form: `[reg, lo, hi]`. Read form: write `[reg]`, then repeated-start read of N bytes.
- **The CH32 firmware also accepts the 8-bit form `[reg, val]`.**
  - The factory image uses Waveshare's registry component `custom_io_expander_ch32v003`, an 8-bit driver with `IO_COUNT 8`. The string `custom_io_expander_new_i2c_ch32v003(i2c_handle, ((0x24)), &custom_io_expander)` appears in `./components/esp32_s3_touch_lcd_7c/esp32_s3_touch_lcd_7c.c`, along with `set_dir/set_level(PIN_NUM_4,1)` and `(PIN_NUM_3,1)`.
  - Source: https://components.espressif.com/components/waveshare/custom_io_expander_ch32v003 (v2.0.0 is byte-identical to v1.0.2 in the `.c`).
  - So the "8-bit vs 16-bit revision split" is not a hardware split. Use 16-bit, because it is the only way to reach EXIO8–15.

**PWM direction.**
- Higher values are brighter.
- IDF `io_extension.c`: `// Prevent the screen from completely turning off: if (Value <= 5) Value = 5;` then `data[1] = Value * (255/100.0)`.
- The raw register is 0–255. Waveshare's helper takes a percentage.

**EXIO map, from the schematic netlist (`NLPDx NLEXIOx NL<net>`):**

| EXIO | CH32 pin | Net / function |
|---|---|---|
| 0 | PD0 | LCD_RST → J3 pin 44 (R72 10 k pull-up) |
| 1 | PC5 | CTP_RST → J1 pin 1 |
| 2 | PD2 | DISP → U2 SY7200A EN |
| 3 | PD3 | PA_CTRL → U16 AND gate |
| 4 | PD4 | SDCS → SD pin 2 (CD/D3) |
| 5 | PD5 | LCD_VDD_EN → U3 AP3012 pin 4 SHDN; R25 10 k pulls it **to GND** |
| 6 | PD6 | LED2 cathode (active-low) |
| 7–14 | PC6, PB0, PB1, PB2, PB3, PB4, PB5, PB6 | P1 pins 9–16, wired directly (no optocouplers) |
| 15 | PC7 | RTC_INT (U5 pin 4) |
| EXIO_ADC | PC4 | Single-pin net: connected to nothing |
| EXIO_PWM | PC3 | → R100 → Q8 |

**The CH32 is not reset by ESP software resets.** Its RESET (PA7) is on the same net as ESP EN and the K1 key. `esp_restart()`, a panic, or a USB-JTAG reset leaves the expander outputs latched. Firmware must re-write direction and output registers on every boot.

### 1.3 P1 screw terminal (verified pin by pin from the netlist)

| P1 pin | Signal |
|---|---|
| 1 | DC+ |
| 2 | DC− (through L10) |
| 3 | 5V |
| 4 | GND |
| 5 | 3V3 |
| 6 | GND |
| 7 | SDA (the SDA net includes PIP107) |
| 8 | SCL (the SCL net includes PIP108) |
| 9–16 | EXIO7 … EXIO14 |

### 1.4 I2C bus

- One bus. ESP side: GPIO47 (SDA) and GPIO48 (SCL) at 1.8 V. M1 NDC7002N shifts it to the 3.3 V side.
- The 3.3 V side carries CH32 (PC1/PC2), GT911, ES8389 (CCLK/CDATA), ES7210, PCF85063A, BQ27220 and P1.
- Address straps:
  - ES8389: AD0 and AD1 tied to AGND through 0 Ω (R108/R109) → **0x10**.
  - ES7210: A0 and A1 tied to GND through 0 Ω (R61/R62) → **0x40**.
- Bus speeds:
  - Waveshare uses 400 kHz for the bus, the expander and GT911.
  - Vendored esp_codec_dev adds the codec devices at `DEFAULT_I2C_CLOCK (100000)`, shifting the 8-bit address `>>1`. Speeds are per device, so these don't conflict.

**Core 3.3.10 Wire** (`esp32-hal-i2c-ng.c`, `Wire.cpp`):
- It uses the new `i2c_master` driver: `glitch_ignore_cnt=7`, `enable_internal_pullup=1`, `scl_wait_us=0`.
- It adds one device handle per address, at Wire's frequency.
- `void *i2cBusHandle(uint8_t)` and `bool i2cIsInit(uint8_t)` are exported.
- `begin()` when the bus is already up logs `"Bus already started in Master Mode."` and returns true. It only knows about buses **it** created.
- **So the order must be `Wire.begin(47,48,400000)` first, then hand `i2cBusHandle(0)` to esp_lcd/esp_codec_dev.** If Waveshare's `DEV_I2C_Init()` runs first, the next `i2c_new_master_bus` on port 0 fails, and Waveshare wraps it in `ESP_ERROR_CHECK`, which aborts.
- `beginTransmission` takes the Wire mutex and `endTransmission`/`requestFrom` release it, so Wire is task-safe per transaction.
- Variant defaults are `SDA=8, SCL=9`, which are LCD G6/G5. `LED_BUILTIN = RGB_BUILTIN = 49+48 = 97`, which `digitalWrite`/`pinMode` redirect to **GPIO48, which is SCL**. Never use either.

**PN532 cannot share the bus.** `Adafruit_PN532.h:73 #define PN532_I2C_ADDRESS (0x48 >> 1)` (v1.3.4) is 0x24, the same as the expander. A hard conflict, confirmed.

**bb_captouch 1.3.1:** `init()` probes `TMA445_ADDR 0x24` (cpp L195) before GT911 (L252). Confirmed; do not use it.

### 1.5 LCD

- Timings (Arduino 03_LCD and 10_LVGL_CODEC): `hsync/vsync pulse 4, bp 8, fp 8`, `pclk_active_neg=1`, data width 16. Panel reset and backlight are −1 (handled by the expander).
- PCLK is 16 MHz in 03_LCD and 12 MHz in 10_LVGL_CODEC. The IDF demos use pulse 8 / bp 8 / fp 4. All of these are vendor-used and valid in DE mode.

**Pins** (verified against netlist `NLIOxx NLyy`):

| Signal | GPIO |
|---|---|
| HSYNC | 46 |
| VSYNC | 3 |
| DE | 5 |
| PCLK | 7 |
| data[0..15] | 14, 38, 18, 17, 10 (B3–B7) · 39, 0, 45, 9, 8, 21 (G2–G7) · 1, 2, 42, 41, 40 (R3–R7) |

**Header facts** (IDF 5.5.4 `esp_lcd_panel_rgb.h` in core 3.3.10):
- `num_fbs` max is 3.
- `on_bounce_frame_finish` is deprecated; use `on_frame_buf_complete`.
- `sram_trans_align` and `psram_trans_align` are deprecated; use `dma_burst_size`.
- Runtime controls exist: **`esp_lcd_rgb_panel_set_pclk(panel, hz)`** and `esp_lcd_rgb_panel_restart()`.

**Driver behaviour** (`esp_lcd_panel_rgb.c` v5.5.4):
- If you pass one of the driver's own framebuffers to `draw_bitmap`, it only sets `cur_fb_index`; there is no copy.
- The bounce ISR copies with `memcpy`. At frame wrap it does `bb_fb_index = cur_fb_index` and then calls `on_frame_buf_complete`.
- Creation requires `fb_size % bb_size == 0` (800×10 px is fine).
- The IRAM check on callbacks applies only with `LCD_RGB_ISR_IRAM_SAFE`, which is not set in Arduino.

**Waveshare's init order** (03_LCD): `DEV_I2C_Init` → `IO_EXTENSION_Init` (direction 0xFFFF, no output write) → `esp_lcd_new_rgb_panel` + `init` → `bl_on`. That first output write sends 0xFFFF, which raises LCD_VDD_EN, LCD_RST, DISP, PA and SDCS together.

### 1.6 Touch (GT911)

From `06_TOUCH/src/gt911/gt911.cpp`, verified against the source:
- Reset sequence:
  1. `pinMode(4,OUTPUT)`
  2. EXIO1 = 0, wait 100 ms
  3. `digitalWrite(4,0)`, wait 100 ms
  4. EXIO1 = 1, wait 200 ms
- Address `0x5D`; the backup address is `0x14`.
- The driver then configures INT as an input. No ISR is installed in the demo; it polls with `esp_lcd_touch_read_data`.
- `swap_xy=0, mirror_x=0, mirror_y=0`, `x_max=800, y_max=480`.
- The LVGL demo works with these settings, so coordinates are native 800×480.

### 1.7 TF card

- Wired for SD bus 1-bit mode: CMD = IO11 (R45), CLK = IO12 (R47), D0 = IO13 (R50), D3 = EXIO4 (R43). Pull-ups R35–R38.
- **SD pins 1 (D2) and 8 (D1) are single-pin nets (not connected).** SD pin 9 (CD) is on GND, so **there is no card-detect signal**.
- Core API: `SD_MMC.setPins(int clk,int cmd,int d0)` and `begin(const char* mnt="/sdcard", bool mode1bit=false, bool format=false, int freq=BOARD_MAX_SDMMC_FREQ /*=SDMMC_FREQ_HIGHSPEED*/, uint8_t maxOpenFiles=5)`.

### 1.8 Audio

- I2S: MCLK 6, BCLK 44, LRCK 16, DOUT 15, DIN 43 (through R63 51 Ω).
- **The ES8389 open sequence in the audio report matches the vendored `es8389.c` exactly**, including `F0=0x1A` (no_dac_ref=false) and `48=0xBE`.
- **Confirmed bug:** `es8389_set_mute()` writes `ES8389_ADC_SP_CONTROL_REG0x20`. Mute the output with reg 0x40 bits[1:0], EXIO3 or volume instead.
- `ESP_I2S`:
  - `setPins(int8_t bclk,int8_t ws,int8_t dout,int8_t din=-1,int8_t mclk=-1)`
  - `explicit I2SClass(i2s_port_t port=I2S_NUM_AUTO)`
  - DMA is fixed at `dma_desc_num 6 × dma_frame_num 240`, `mclk_multiple 256`.
- **EarPhone_Detect** (J5 pins 6/7 → Q7/R92) goes **only** to the AND gate. Neither the ESP nor the CH32 can read it. A plug in the jack mutes both NS4150B amplifiers in hardware.

### 1.9 USB

IO19/IO20 → R40/R44 22 Ω → Type-C DN/DP. This is native USB-Serial/JTAG, and it resolves the audio report's "UNVERIFIED" note.

**HWCDC:** default `tx_timeout_ms = 100`. Writes return immediately when no host is attached (`isConnected()`). Optionally call `Serial.setTxTimeoutMs(0)`.

### 1.10 ESP-NOW (core 3.3.10 `esp_now.h`)

- `typedef wifi_tx_info_t esp_now_send_info_t;`
- `typedef void (*esp_now_send_cb_t)(const esp_now_send_info_t*, esp_now_send_status_t);`
- `typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t*, const uint8_t*, int);`
- `ESP_NOW_MAX_DATA_LEN = ESP_NOW_MAX_IE_DATA_LEN` (250 bytes).

### 1.11 Video libraries

- **JPEGDEC 1.8.4** (tag confirmed):
  - `JPEG_openRAM` does `memset(pJPEG,0,…)`, so `setFramebuffer()` **must** be called after `openRAM`.
  - In framebuffer mode, `iPitch = iCropCX` and no draw callback is made. The image must be exactly 800×480; both are multiples of 16, which suits 4:2:0 MCUs.
- **Arduino_GFX v1.6.9:** `Arduino_ESP32RGBPanel` hard-codes `.num_fbs = 1`, `.double_fb = false`, `.hsync_idle_low = (polarity==0)?1:0`. Confirmed.

### 1.12 Token compatibility (`skull_oracle.ino`, `nfc_tag_bench.ino`)

**Layout:**

| Page | Content |
|---|---|
| 16 | Marker `{'S','K',0x03,visits}`, written last (commit) |
| 17 | `[charge,0,0,0]` |
| 18 + (EVENT_YEAR − 2024) | Year slot `{number 1..100, tier 0..3, 0, 0}`. 2026 is page 20 |

**Skull logic:**
- "Played this year" is `slot[0] != 0`. A replay re-uses `slot[0]` and `slot[1]` and does not re-roll.
- `visits` is incremented only on a new year.
- The veteran rule tests prior visits ≥ 2.
- Page access goes through `mifareultralight_ReadPage/WritePage`, which accept pages <64 only. `ntag2xx_WritePage` refuses pages <4 or >225.

**Bench behaviour:**
- `ensureProvisioned` and `handleProvision` write marker `{S,K,3,**0**}`.
- `handleCharge` writes `{v,0,0,0}`.
- `handleLog` writes `{n,p,0,0}` and bumps visits only if the slot was empty.
- `handleFormat` zeroes pages 4–39.

**Hardware status:** per Halloween `CLAUDE.md`, the skull sketch "has never been flashed or bench-tested". The PN532 code path is not hardware-proven on the owner's boards.

### 1.13 OpenAI API dates (verified at https://developers.openai.com/api/docs/deprecations)

| Model / API | Shutdown | Replacement |
|---|---|---|
| Videos API, `sora-2`, `sora-2-pro` | 2026-09-24 | none |
| `gpt-4o-mini-tts`, `tts-1`, `tts-1-hd` | 2027-01-06 | `gpt-realtime-2.1-mini` |
| `gpt-image-1` | 2026-10-23 | `gpt-image-2.5-sunburst` / `-flare` |
| `gpt-image-1.5` | 2026-12-01 | same |
| `dall-e-3` | 2026-05-12 | — |

---

## 2. Still unverified: safest assumption and runtime handling

| # | Unknown | Code against | Runtime detection / adaptation |
|---|---|---|---|
| U1 | CH32 power-on direction and output state | Assume outputs come up **low**: LCD bias off, backlight off, PA off, touch in reset. The V003 driver states "Default register value on power-up: DIR 0xFF, OUT 0x00". Also assume a previous boot may have left anything latched (§1.2) | Boot: `i2c_master_probe` / `Wire.beginTransmission(0x24)` ACK, retrying 5× at 10 ms. Then write OUT `[0x03,0x50,0x00]` (EXIO4=1 for SD D3, EXIO6=1 so LED off), then DIR `[0x02,0x7F,0x78]` (0x787F), then OUT again. If 0x24 never ACKs there is no display at all; log over USB and blink nothing |
| U2 | Whether reg 0x04 reflects output pins; EXIO7–14 pull-ups and drive current | Don't read outputs back. Use an external 10 kΩ pull-up to P1 3V3 on every input. Outputs only drive logic-level MOSFET or relay modules | Poll inputs every 10–20 ms with 3-sample debounce. Keep EXIO15 an input (DIR bit 15 = 0) so it never fights the open-drain RTC INT |
| U3 | LCD power-up sequencing (the vendor raises VDD_EN, RST and DISP together) | EXIO5=1 → 20 ms → EXIO0=1 → create panel → fill all framebuffers black → wait 2 frames → PWM `[0x05,val]` → EXIO2=1 | Black-first avoids a garbage flash. If the panel stays blank, fall back to the vendor's single 0xFFFF write |
| U4 | PSRAM bandwidth with 3 framebuffers, JPEG decode, SD and audio (and Wi-Fi) | Start at 16 MHz PCLK with bounce 800×10 px. The vendor itself dropped to 12 MHz in its LVGL+SD+audio demo | `esp_lcd_rgb_panel_set_pclk(panel, 12000000)` from a service setting. Measure the `on_frame_buf_complete` rate. Make clip fps = refresh ÷ k: 16 MHz gives 39.02 Hz → 13 fps; 12 MHz gives 29.27 Hz → 14.6 fps. Store fps in the clip header |
| U5 | Flash/NVS/FFat writes vs the RGB scan-out (no XIP-PSRAM, no IRAM-safe ISR in Arduino) | No Preferences, FFat or OTA writes while showing. Log and write state to SD only | — |
| U6 | GT911 config resolution | Assume 800×480 (vendor uses no transform) | Read 0x8048–0x804B (x_max and y_max, little-endian) and 0x8140 ("911") at boot. Scale if they differ. Probe 0x5D, then 0x14 |
| U7 | Speakers inside the BOX, their impedance and power | The product page lists only "Dual MX1.25 speaker headers" and the package as "ESP32-S3-Touch-LCD-7C-BOX x1". Inspect the unit. A powered external speaker on the 3.5 mm jack works and automatically mutes the onboard amps | No software detection is possible (EarPhone_Detect isn't routed). Add a service-menu tone test |
| U8 | ES8389 chip ID values | Don't gate on the ID | Probe 0x10/0x40 ACK and log `0xFD`/`0xFE` |
| U9 | BQ27220 (0x55) present without a battery | Expected scan is {0x10, 0x24, 0x40, 0x51, 0x5D} plus 0x55 when a battery is fitted. The gauge is powered from BAT, so this is inferred | Treat 0x55 as optional |
| U10 | SD 1-bit throughput at 40 MHz | Use 32 KB reads into `MALLOC_CAP_DMA` internal buffers; the S3 has no PSRAM-DMA for SDMMC | Benchmark at boot in service mode. If mount fails at `SDMMC_FREQ_HIGHSPEED`, retry at `SDMMC_FREQ_DEFAULT` |
| U11 | Current budget on the P1 5 V and 3V3 terminals | ≤0.5 A external on 5 V from VIN; ≤150 mA on 3V3. Never back-feed 3V3 | Power the plinth MCU or reader from its own supply when possible |
| U12 | Reader choice for the 23 mm NTAG203 inlays in the crystals (planned as resin then; 3D-printed PLA since 2026-10-22) | Primary: a separate plinth ESP32 with PN532 linked over ESP-NOW. The owner's notes say "Use the 23 mm circles … (better range)". Fallback: M5 Unit RFID2 (WS1850S, 0x28) on P1 5V/SDA/SCL, range <20 mm | Bench-test read range through the finished crystal. Re-verify the UID before every write. Keep the commit-last order. Guard writes to `4 ≤ page ≤ 39`. Skip the CC write if page 3 byte 0 is already 0xE1 |
| U13 | Whether a spare ESP32 exists. The bench tool's pin comments mention the thermocouple project, so its board may be shared | Plan for a dedicated board | — |
| U14 | RTC coin cell present or set | Use the compile-time `EVENT_YEAR` for slot selection. Use the RTC only for auto-dim, and only if its year is plausible (2026–2045) | — |
| U15 | ESP-NOW latency and coexistence with the RGB panel | Use a fixed channel (1). Bounce buffer stays on | Heartbeat every 1 s. The display shows "crystal reader asleep" on 3 missed beats |
| U16 | Whether ChatGPT can still generate video in-app | **Assume no.** Secondary sources disagree (https://en.wikipedia.org/wiki/Sora_(text-to-video_model), https://zilliz.com/ai-faq/what-is-the-sora-shutdown-timeline); the Sora API is verified dead (§1.13). Plan on ChatGPT stills animated locally with ffmpeg and on the device | Owner checks the ChatGPT UI |

**Error handling for unattended running.** Replace every Waveshare `ESP_ERROR_CHECK` I2C helper with error-returning wrappers: 3 retries, then `i2c_master_bus_reset(i2cBusHandle(0))`. The vendor version aborts on a single NACK.

---

## 3. Contradictions settled

1. **Module and flash type.** The video report said "docs don't name the module; octal UNVERIFIED"; the board report said WROOM-2-N32R16V octal. **The board report is correct**: schematic U10, the product page, and the factory image header (OPI build, 32 MB).
2. **Flash Mode QIO vs OPI.** The video report recommended QIO; the board and audio reports used OPI. **Both boot.** Use OPI (`opi_opi`), since the factory image is an OPI build and IDF warns against relying on auto-detect. Waveshare's QIO is the fallback.
3. **EXIO5.** Waveshare's header comment says "0 for USB, 1 for CAN" (audio report flagged it). The netlist says **LCD_VDD_EN** → AP3012 SHDN with a 10 k pull-down; the comment is stale. Waveshare's 04 demo sets EXIO0 and EXIO5 as **inputs** while using the LCD. Don't copy that; keep EXIO0–6 as outputs.
4. **"Isolated IO" EXIO7–14** (task context, experience report). They are raw CH32 GPIO at 3.3 V on P1 pins 9–16. The demo name `04_ISOLATION_IO` is misleading. They are digital only, with no PWM and no NeoPixel timing.
5. **Expander protocol, 8-bit vs 16-bit "revision split".** The firmware accepts both (factory image uses 8-bit; current demos use 16-bit). Use 16-bit `[reg,lo,hi]`.
6. **Direction mask 0xFFFF (video/audio) vs 0x787F (board).** Use 0x787F, adjusting the EXIO7–14 bits per use. EXIO15 must be an input.
7. **Backlight PWM direction** (old Arduino clamp ≥95 vs current clamp ≤5). Higher is brighter, per the IDF comment. The raw register is 0–255.
8. **I2C speed** (400 k everywhere vs codec at 100 k). Both are true: per-device speeds, with esp_codec_dev using 100 kHz.
9. **GT911 reset timing** (video report's 10/1/60 ms vs Waveshare's 100/100/200 ms). Use Waveshare's; it is boot-only and proven on this board. Poll the touch controller.
10. **TF card interface** (context said SPI). It is SD_MMC 1-bit; D1/D2 are not connected and there is no card detect.
11. **P1 pinout "UNVERIFIED pin-by-pin"** (NFC report). Verified from the netlist (§1.3).
12. **EXIO_ADC as battery voltage** (docs). PC4 connects to nothing. Use the BQ27220.
13. **"Proven" PN532 code** (NFC report). The skull has never been flashed; only the architecture is proven, not the hardware.
14. **Audio sample rate** (assets report 22.05 kHz vs audio report 48 kHz). **48 kHz mono s16 PCM.** The audio engine checks `sr == 48000` and runs I2S at a fixed 48 k with MCLK at 256·fs.
15. **Clip fps and quality** (assets: 15 fps, q:v 4–6; video: 13 fps, q:v 8).
    - Make fps = panel refresh ÷ k (13 fps at 16 MHz PCLK).
    - Use q:v 6–8 for motion and q:v 3–5 for stills.
    - The packer records `max_frame_bytes`; firmware rejects a clip whose max exceeds the buffer. The `MjpegClass` overflow only matters if Arduino_GFX's `MjpegClass` is used, which the chosen design doesn't.
16. **LVGL** (assets report: LVGL `.bin` sprites and LVGL fonts) vs no LVGL (video report). For the show path, use esp_lcd directly with JPEGDEC and an Arduino_GFX canvas. Sprites are raw RGB565 plus an 8-bit alpha plane; fonts are U8g2 or Adafruit GFXfont. Don't convert anything to LVGL `.bin`.
17. **"ChatGPT makes the animations"** (experience and video reports) vs "no video" (assets report). The assets report is correct for the API (verified). Request stills, layers and pose variants from ChatGPT; voice comes from TTS before 2027-01-06 or from ElevenLabs.
18. **Awakening trigger** (experience report: "tag without the SK marker"). Bench-provisioned crystals **do** carry the SK marker, with `visits=0`. Use `!ours || visits==0`.
19. **Element colour in page 17 byte 1, and badges in slot bytes 2–3** (experience report). The bench's `handleCharge` writes `{v,0,0,0}` and `handleLog` writes `{n,p,0,0}`, so both would be wiped. Either change the bench to read-modify-write, or derive the colour from a hash of the UID, which needs no tag write. The skull only writes zeros to empty slots, so it is compatible. The new station must still write a non-zero `slot[0]` (1–100) and a tier of 0–3.
20. **Touch controller "likely GT911, UNVERIFIED"** (experience report). Verified as GT911 at 0x5D (vendor source).
21. **USB routing UNVERIFIED** (audio report). Native USB on IO19/20 (§1.9).
22. **"Docs don't name the module"** (video report). The product page names WROOM-2-N32R16 and the schematic names the N32R16V.

---

## 4. Additional gotchas not raised in any report

- **No user button on the board.** BOOT is on GPIO0, which is LCD G3; pressing it during operation corrupts the green bit and should not be used as input. All buttons go on EXIO inputs or on the plinth MCU.
- **The RGB panel can convert YUV to RGB in hardware** (`esp_lcd_rgb_panel_set_yuv_conversion` is in the 3.3.10 headers). It might take colour conversion off the CPU, but the JPEGDEC output path for it has not been explored.

## 5. Sources

- Waveshare repo and schematic: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7C (commit `73f8b90`), `schematic/ESP32-S3-Touch-LCD-7C-schematic.pdf` (netlist), `firmware/ESP32-S3-Touch-LCD-7C-Test.bin`.
- Waveshare docs: https://docs.waveshare.com/ESP32-S3-Touch-LCD-7C-BOX/FAQ (only two entries: BOOT recovery and the US/CN jack resistors); product page https://www.waveshare.com/esp32-s3-touch-lcd-7c-box.htm.
- CH32 expander driver: https://components.espressif.com/components/waveshare/custom_io_expander_ch32v003; ESPHome https://api-docs.esphome.io/waveshare__io__ch32v003_8cpp_source.
- OpenAI deprecations: https://developers.openai.com/api/docs/deprecations.
- Local, core 3.3.10: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\3.3.10\{boards.txt, cores/esp32/esp32-hal-i2c-ng.c, esp32-hal-gpio.c, HWCDC.cpp, libraries/Wire, ESP_I2S, SD_MMC, variants/esp32s3/pins_arduino.h}` and `tools\esp32s3-libs\3.3.10\{opi_opi,qio_opi}\include\sdkconfig.h`, `include/esp_lcd/rgb/include/esp_lcd_panel_rgb.h`, `include/esp_wifi/include/esp_now.h`.
- Local, research clones: `C:\Users\Ciancio\AppData\Local\Temp\crystal-oracle-research\video-playback\{JPEGDEC@1.8.4, Arduino_GFX@v1.6.9, bb_captouch@1.3.1, idf-src\esp_lcd_panel_rgb.c}`.
- Local, owner's project (read only): `C:\Users\Ciancio\Desktop\Claude projects\Halloween\skull_oracle\skull_oracle.ino`, `C:\Users\Ciancio\Desktop\Claude projects\Halloween\nfc_tag_bench\nfc_tag_bench.ino`.