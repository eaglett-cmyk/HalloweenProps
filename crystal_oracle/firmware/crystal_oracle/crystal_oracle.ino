/*
 * ============================================================================
 *  CRYSTAL ORACLE — screen firmware  (Waveshare ESP32-S3-Touch-LCD-7C-BOX)
 * ----------------------------------------------------------------------------
 *  A trick-or-treat storytelling station. Kids place their NFC crystal on the
 *  plinth; the screen tells the story, reveals tonight's prize tier and which
 *  bowl to pick from, and the result is written ONTO THE CRYSTAL (schema 0x03,
 *  compatible with the Skull Oracle + nfc_tag_bench), so a dead or reflashed
 *  board never loses anyone's history.
 *
 *  Board settings (Arduino IDE 2, Espressif "esp32" core 3.3.x):
 *    Board ............ ESP32S3 Dev Module Octal (WROOM2)
 *    Flash Mode ....... OPI 80MHz        Flash Size .... 32MB (256Mb)
 *    PSRAM ............ OPI PSRAM        Partition ..... 32M Flash (4.8MB APP/22MB FATFS)
 *    USB CDC On Boot .. Enabled          USB Mode ...... Hardware CDC and JTAG
 *    CPU .............. 240MHz
 *  arduino-cli FQBN:
 *    esp32:esp32:esp32s3-octal:CDCOnBoot=cdc,FlashMode=opi,FlashSize=32M,PSRAM=opi,PartitionScheme=app5M_fat24M_32MB,CPUFreq=240,USBMode=hwcdc
 *  (If it ever boot-loops, use Waveshare's own setting: board "ESP32S3 Dev
 *   Module" with Flash Mode "QIO 80MHz" — the octal flash is auto-detected.)
 *
 *  Libraries (Library Manager): "GFX Library for Arduino" (1.6.9+), "JPEGDEC"
 *  (1.8.4+), "U8g2", "RFID_MFRC522v2" (only used with reader = rfid2).
 *
 *  Everything you'd want to tweak lives on the TF card: /oracle/config.ini
 *  (year, volume, odds, stock ...) and /oracle/captions.ini (words). See
 *  crystal_oracle/README.md.
 * ============================================================================
 */
#include "board.h"
#include "display.h"
#include "touch.h"
#include "media.h"
#include "audio.h"
#include "settings.h"
#include "reader.h"
#include "vlog.h"
#include "show.h"
#include "esp_heap_caps.h"

#define FW_VERSION_STR "1.0.0"

static char g_line[8][48];
static const char *g_lines[8];
static uint8_t g_nLines = 0;

static void status(const char *fmt, ...) {
  if (g_nLines >= 8) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(g_line[g_nLines], sizeof(g_line[0]), fmt, ap);
  va_end(ap);
  Serial.println(g_line[g_nLines]);
  g_lines[g_nLines] = g_line[g_nLines];
  g_nLines++;
  if (display::ok()) show::bootScreen(g_lines, g_nLines);
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);            // never block when no USB host is attached
  delay(200);
  Serial.println("\nCrystal Oracle " FW_VERSION_STR);

  const bool exp = board::begin();     // Wire on 47/48 FIRST, expander to a safe state
  if (!exp) Serial.println("FATAL: IO expander (0x24) not answering - no display possible");

  display::begin(settings::cfg.brightness);   // default brightness until config.ini is read
  status("Crystal Oracle " FW_VERSION_STR);
  status("Display %s", display::ok() ? "ok" : "FAILED");

  status("Touch %s", touch::begin() ? "ok" : "not found");

  const bool sd = media::mount();
  status("TF card %s", sd ? "ok" : "MISSING - running on built-in visuals");
  if (sd) {
    settings::load();
    if (settings::cfg.pclkMhz * 1000000u != display::pclkHz())        // pclk_mhz from config.ini: next VSYNC
      display::setPclk(settings::cfg.pclkMhz * 1000000u);
    counters::load(settings::cfg.eventYear);
    vlog::begin(settings::cfg.eventYear);
    board::backlight(settings::cfg.brightness);
    status("Year %u  -  %u assets", settings::cfg.eventYear, media::assetCount());
    // Leftovers from a rehearsal show here (Host menu -> Tonight -> Reset tonight).
    // The meter's name comes from the story words (meter.name: "Moon", "Lantern" ...).
    if (counters::c.moonPoints || counters::c.moonFull || counters::c.visits)
      status("Tonight so far: %u visits, %s %u/%u%s", counters::c.visits, settings::meterName(),
             counters::c.moonPoints, settings::cfg.moonGoal, counters::c.moonFull ? " FULL" : "");
  }

  status("Sound %s", audio::begin(settings::cfg.volume) ? "ok" : "FAILED");
  const bool rd = reader::begin();     // separate statement: backendName() is only valid after begin()
  status("Crystal reader: %s%s", reader::backendName(), rd ? "" : " FAILED");

  vlog::event("boot fw=%s sd=%d year=%u reader=%s", FW_VERSION_STR, sd, settings::cfg.eventYear, reader::backendName());
  delay(1200);
  show::begin();
  // The internal-RAM margin left for SD / audio / ESP-NOW (media logs it again at the first clip).
  vlog::event("heap: internal %lu B free, largest DMA block %lu B, psram %lu B free",
              (unsigned long)board::freeInternal(),
              (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
              (unsigned long)board::freePsram());
}

void loop() {
  show::loop();
}
