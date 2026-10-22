/*
 * ============================================================================
 *  display.cpp — 800x480 RGB565 panel (ST7262) on esp_lcd, triple-buffered
 * ----------------------------------------------------------------------------
 *  Implements display.h.
 *
 *  Panel (Waveshare 7C demo values, docs/DESIGN.md §2-3):
 *    PCLK 16 MHz, pulse 4 / back porch 8 / front porch 8 on both axes,
 *    pclk_active_neg = 1  ->  16e6 / (820 * 500) = 39.02 Hz refresh.
 *    data pins D0..D15 = B3..B7, G2..G7, R3..R7.
 *    THREE framebuffers in PSRAM + two 800x10 px bounce buffers in internal
 *    RAM: the DMA streams from the bounce buffers, which the driver's ISR
 *    refills (memcpy) from the current framebuffer. The panel is created from
 *    a short-lived task pinned to CORE 0, so that ISR (and its PSRAM traffic)
 *    lives on core 0 while drawing happens on the Arduino loop task (core 1).
 *
 *  Swap protocol (no tearing, no copies):
 *    shown  = buffer the bounce ISR is reading from
 *    queued = buffer handed to esp_lcd_panel_draw_bitmap() (same pointer as one
 *             of the driver's own buffers -> the driver only switches its
 *             "current" index; the switch happens at the next frame wrap)
 *    on_frame_buf_complete (ISR, at every frame wrap) promotes queued -> shown
 *    and gives a binary semaphore. beginFrame() hands out the buffer that is
 *    neither shown nor queued; endFrame() queues it, waiting (bounded) only if
 *    the previous frame has not started scanning out yet.
 *    Ordering detail: endFrame() calls draw_bitmap BEFORE publishing `queued`,
 *    so a frame wrap in between can only make us think a buffer is still
 *    busy for one extra refresh — never hand out a buffer that is on screen.
 *
 *  Drawing: an Arduino_Canvas subclass whose protected `_framebuffer` (GFX
 *  Library for Arduino 1.6.9) is pointed at the back buffer by beginFrame()
 *  and set to nullptr by endFrame() — every canvas primitive checks
 *  `if (_framebuffer)`, so drawing outside a frame (or with no panel at all)
 *  is a harmless no-op. The destructor nulls it so the canvas never frees a
 *  panel buffer. Text = u8g2 fonts through the canvas (U8g2lib.h is included
 *  before Arduino_GFX_Library.h by display.h).
 *
 *  If begin() fails every function here is a safe no-op (beginFrame() returns
 *  nullptr; callers check display::ok()).
 * ============================================================================
 */
#include "display.h"
#include "board.h"
#include <math.h>
#include <string.h>
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_panel_ops.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace display {

namespace {

// ---- panel timing / wiring -------------------------------------------------
constexpr uint32_t H_PULSE = 4, H_BP = 8, H_FP = 8;
constexpr uint32_t V_PULSE = 4, V_BP = 8, V_FP = 8;
constexpr uint32_t H_TOTAL = W + H_PULSE + H_BP + H_FP;   // 820
constexpr uint32_t V_TOTAL = H + V_PULSE + V_BP + V_FP;   // 500
constexpr int PIN_HSYNC = 46, PIN_VSYNC = 3, PIN_DE = 5, PIN_PCLK = 7;
// D0..D15 = B3 B4 B5 B6 B7 | G2 G3 G4 G5 G6 G7 | R3 R4 R5 R6 R7
constexpr int PIN_DATA[16] = {14, 38, 18, 17, 10, 39, 0, 45, 9, 8, 21, 1, 2, 42, 41, 40};

constexpr int      NUM_FB    = 3;
constexpr size_t   FB_PIXELS = (size_t)W * H;
constexpr size_t   FB_BYTES  = FB_PIXELS * sizeof(uint16_t);
constexpr size_t   BOUNCE_PX = (size_t)W * 10;   // 768000 % 16000 == 0 (driver requirement)
constexpr uint32_t PCLK_MIN  = 6000000;
constexpr uint32_t PCLK_MAX  = 24000000;
constexpr uint32_t CREATE_TIMEOUT_MS = 3000;     // panel creation (allocates + clears 2.3 MB PSRAM)
constexpr uint32_t QUEUE_STALL_MS    = 120;      // endFrame gives up waiting for a frame start after this

// 4x4 ordered-dither matrix (0..15) for smooth gradients in RGB565.
constexpr uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// ---- canvas bound to whichever buffer is being drawn -------------------------
class FbCanvas : public Arduino_Canvas {
 public:
  FbCanvas() : Arduino_Canvas(W, H, nullptr) {}
  ~FbCanvas() { _framebuffer = nullptr; }   // never let Arduino_Canvas free a panel buffer
  void target(uint16_t *fb) { _framebuffer = fb; }
};

FbCanvas &canvas() {
  static FbCanvas c;   // constructed on first use (no static-init-order surprises)
  return c;
}

// ---- panel state -------------------------------------------------------------
esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t *s_fb[NUM_FB] = {nullptr, nullptr, nullptr};
bool      s_ok = false;
uint32_t  s_pclk = PCLK_HZ;
int8_t    s_back = -1;               // buffer handed out by beginFrame (loop task only)
bool      s_warnedStall = false;

// Shared with the ISR (core 0).
portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile int8_t   s_shown = 0;       // the driver starts scanning fb[0]
volatile int8_t   s_queued = -1;
volatile uint32_t s_presented = 0;   // frames that actually went on screen
volatile uint32_t s_refreshes = 0;   // frame wraps seen
StaticSemaphore_t s_refreshSemBuf;
SemaphoreHandle_t s_refreshSem = nullptr;   // binary, given at every frame wrap

// Runs in the bounce-buffer ISR (core 0) right after the driver switched its
// read index to the most recently queued buffer for the next frame.
bool IRAM_ATTR onFrameBufComplete(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  BaseType_t woken = pdFALSE;
  portENTER_CRITICAL_ISR(&s_mux);
  if (s_queued >= 0) {
    s_shown = s_queued;
    s_queued = -1;
    s_presented = s_presented + 1;
  }
  s_refreshes = s_refreshes + 1;
  portEXIT_CRITICAL_ISR(&s_mux);
  if (s_refreshSem) xSemaphoreGiveFromISR(s_refreshSem, &woken);
  return woken == pdTRUE;
}

// ---- panel creation on core 0 --------------------------------------------------
// The job lives in static storage so a creator task that finishes after the
// caller gave up (timeout) never writes into a dead stack frame; it then
// deletes the panel itself.
struct CreateJob {
  uint32_t gen = 0;            // which begin() attempt this job belongs to
  esp_err_t err = ESP_FAIL;
  esp_lcd_panel_handle_t panel = nullptr;
  bool finished = false;
  bool abandoned = false;
};
CreateJob s_job;
uint32_t s_jobGen = 0;
portMUX_TYPE s_jobMux = portMUX_INITIALIZER_UNLOCKED;
StaticSemaphore_t s_jobDoneBuf;
SemaphoreHandle_t s_jobDone = nullptr;

esp_err_t createPanel(esp_lcd_panel_handle_t *out) {
  esp_lcd_rgb_panel_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = s_pclk;
  cfg.timings.h_res = W;
  cfg.timings.v_res = H;
  cfg.timings.hsync_pulse_width = H_PULSE;
  cfg.timings.hsync_back_porch = H_BP;
  cfg.timings.hsync_front_porch = H_FP;
  cfg.timings.vsync_pulse_width = V_PULSE;
  cfg.timings.vsync_back_porch = V_BP;
  cfg.timings.vsync_front_porch = V_FP;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = NUM_FB;
  cfg.bounce_buffer_size_px = BOUNCE_PX;
  cfg.dma_burst_size = 64;
  cfg.hsync_gpio_num = PIN_HSYNC;
  cfg.vsync_gpio_num = PIN_VSYNC;
  cfg.de_gpio_num = PIN_DE;
  cfg.pclk_gpio_num = PIN_PCLK;
  cfg.disp_gpio_num = -1;              // DISP / backlight are on the IO expander
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = PIN_DATA[i];
  cfg.flags.fb_in_psram = 1;

  esp_lcd_panel_handle_t panel = nullptr;
  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
  if (err != ESP_OK) return err;
  esp_lcd_rgb_panel_event_callbacks_t cbs;
  memset(&cbs, 0, sizeof(cbs));
  cbs.on_frame_buf_complete = onFrameBufComplete;
  err = esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, nullptr);
  if (err == ESP_OK) err = esp_lcd_panel_reset(panel);
  if (err == ESP_OK) err = esp_lcd_panel_init(panel);   // starts streaming fb[0]
  if (err != ESP_OK) {
    esp_lcd_panel_del(panel);
    return err;
  }
  *out = panel;
  return ESP_OK;
}

void createTask(void *arg) {
  const uint32_t gen = (uint32_t)(uintptr_t)arg;
  esp_lcd_panel_handle_t panel = nullptr;
  const esp_err_t err = createPanel(&panel);
  bool abandoned;
  portENTER_CRITICAL(&s_jobMux);
  // Abandoned = the caller timed out, or a newer begin() attempt replaced this job.
  abandoned = s_job.abandoned || s_job.gen != gen;
  if (!abandoned) {
    s_job.err = err;
    s_job.panel = panel;
    s_job.finished = true;
  }
  portEXIT_CRITICAL(&s_jobMux);
  if (abandoned) {
    if (panel) esp_lcd_panel_del(panel);
  } else {
    xSemaphoreGive(s_jobDone);
  }
  vTaskDelete(nullptr);
}

esp_err_t createPanelOnCore0(esp_lcd_panel_handle_t *out) {
  if (!s_jobDone) s_jobDone = xSemaphoreCreateBinaryStatic(&s_jobDoneBuf);
  xSemaphoreTake(s_jobDone, 0);
  portENTER_CRITICAL(&s_jobMux);
  s_job = CreateJob();
  s_job.gen = ++s_jobGen;
  const uint32_t gen = s_job.gen;
  portEXIT_CRITICAL(&s_jobMux);

  if (xTaskCreatePinnedToCore(createTask, "lcd_create", 6144, (void *)(uintptr_t)gen, 5, nullptr, 0) != pdPASS) {
    Serial.println("[display] could not start the core-0 creator task - creating on this core");
    return createPanel(out);   // degrade: ISR lands on this core, still works
  }
  if (xSemaphoreTake(s_jobDone, pdMS_TO_TICKS(CREATE_TIMEOUT_MS)) != pdTRUE) {
    bool finished;
    portENTER_CRITICAL(&s_jobMux);
    finished = s_job.finished;
    if (!finished) s_job.abandoned = true;   // the task cleans up whenever it ends
    portEXIT_CRITICAL(&s_jobMux);
    if (!finished) return ESP_ERR_TIMEOUT;
  }
  if (s_job.err == ESP_OK) *out = s_job.panel;
  return s_job.err;
}

inline uint16_t *backBuf() { return (s_ok && s_back >= 0) ? s_fb[s_back] : nullptr; }

// ---- glow falloff --------------------------------------------------------------
// glowLut[i] = falloff at squared normalised distance t = i/256 (0 = centre).
// (1 - t)^2.5 looks like a soft gaussian orb with a gentle tail.
uint8_t s_glowLut[257];
bool s_glowLutReady = false;

void initGlowLut() {
  for (int i = 0; i <= 256; i++) {
    const float t = i / 256.0f;
    s_glowLut[i] = (uint8_t)lroundf(255.0f * powf(1.0f - t, 2.5f));
  }
  s_glowLut[256] = 0;
  s_glowLutReady = true;
}

}  // namespace

// ============================================================================
//  lifecycle
// ============================================================================
bool begin(uint8_t backlightPct) {
  FbCanvas &cv = canvas();
  cv.setTextWrap(false);       // a long caption must never wrap onto a second line
  cv.setUTF8Print(false);      // fonts are _tr (ASCII); captions.ini text is printed byte-wise
  cv.setTextSize(1);

  if (s_ok) {                  // already running: just apply the brightness
    board::backlight(backlightPct);
    return true;
  }
  if (!s_refreshSem) s_refreshSem = xSemaphoreCreateBinaryStatic(&s_refreshSemBuf);

  if (!board::expanderOk()) Serial.println("[display] IO expander missing - panel will stay unpowered/dark");
  board::lcdPower(true);       // EXIO5 bias on, 20 ms, EXIO0 reset released

  const uint32_t t0 = millis();
  esp_lcd_panel_handle_t panel = nullptr;
  const esp_err_t err = createPanelOnCore0(&panel);
  if (err != ESP_OK || !panel) {
    Serial.printf("[display] RGB panel creation FAILED: %s (free PSRAM %lu, internal %lu)\n", esp_err_to_name(err),
                  (unsigned long)board::freePsram(), (unsigned long)board::freeInternal());
    board::lcdPower(false);
    return false;
  }

  void *a = nullptr, *b = nullptr, *c = nullptr;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel, NUM_FB, &a, &b, &c) != ESP_OK || !a || !b || !c) {
    Serial.println("[display] could not get the 3 frame buffers");
    esp_lcd_panel_del(panel);
    board::lcdPower(false);
    return false;
  }
  s_fb[0] = (uint16_t *)a;
  s_fb[1] = (uint16_t *)b;
  s_fb[2] = (uint16_t *)c;
  for (int i = 0; i < NUM_FB; i++) memset(s_fb[i], 0, FB_BYTES);   // black (the driver calloc'd them too)

  portENTER_CRITICAL(&s_mux);
  s_shown = 0;                 // esp_lcd_panel_init() starts on fb[0]
  s_queued = -1;
  portEXIT_CRITICAL(&s_mux);
  s_back = -1;
  s_panel = panel;
  s_ok = true;

  // Bind once so begin() sees a buffer and does NOT allocate its own 768 KB.
  cv.target(s_fb[0]);
  cv.begin(GFX_SKIP_OUTPUT_BEGIN);
  cv.target(nullptr);

  // Present one (black) frame through the normal path, wait for 2 refreshes,
  // THEN light the backlight -> no garbage flash at power-up.
  beginFrame();
  endFrame();
  const uint32_t r0 = s_refreshes;
  waitRefresh(100);
  waitRefresh(100);
  if (s_refreshes == r0) Serial.println("[display] WARNING: no frame interrupts yet (panel DMA not running?)");

  board::backlight(backlightPct);
  Serial.printf("[display] panel ok in %lu ms: 3 FB @%p %p %p, %.2f Hz, backlight %u%%\n",
                (unsigned long)(millis() - t0), a, b, c, (double)refreshHz(), (unsigned)board::backlightPct());
  return true;
}

bool ok() { return s_ok; }

// ============================================================================
//  frame cycle
// ============================================================================
uint16_t *beginFrame() {
  if (!s_ok) return nullptr;
  if (s_back < 0) {
    portENTER_CRITICAL(&s_mux);
    const int8_t q = s_queued, sh = s_shown;
    portEXIT_CRITICAL(&s_mux);
    for (int8_t i = 0; i < NUM_FB; i++) {
      if (i != q && i != sh) {
        s_back = i;
        break;
      }
    }
    if (s_back < 0) s_back = (int8_t)((sh + 1) % NUM_FB);   // unreachable with 3 buffers
  }
  canvas().target(s_fb[s_back]);
  return s_fb[s_back];
}

void endFrame() {
  if (!s_ok || s_back < 0) return;
  // The previous frame may still be waiting for its scan-out to begin: wait for
  // the next frame wrap (bounded — a stalled panel must never hang the show).
  const uint32_t t0 = millis();
  for (;;) {
    portENTER_CRITICAL(&s_mux);
    const bool busy = s_queued >= 0;
    portEXIT_CRITICAL(&s_mux);
    if (!busy) break;
    if (millis() - t0 >= QUEUE_STALL_MS) {
      if (!s_warnedStall) {
        Serial.println("[display] WARNING: frame interrupt stalled - replacing the queued frame");
        s_warnedStall = true;
      }
      break;
    }
    xSemaphoreTake(s_refreshSem, pdMS_TO_TICKS(10));
  }
  canvas().target(nullptr);   // stray drawing after endFrame() goes nowhere
  // Same pointer as a driver buffer -> no copy, just switches the driver's index.
  esp_lcd_panel_draw_bitmap(s_panel, 0, 0, W, H, s_fb[s_back]);
  portENTER_CRITICAL(&s_mux);
  s_queued = s_back;          // published AFTER the switch (see header comment)
  portEXIT_CRITICAL(&s_mux);
  s_back = -1;
}

Arduino_GFX &gfx() { return canvas(); }

uint16_t *shownBuffer() {
  if (!s_ok) return nullptr;
  portENTER_CRITICAL(&s_mux);
  const int8_t sh = s_shown;
  portEXIT_CRITICAL(&s_mux);
  return s_fb[sh];
}

void waitRefresh(uint32_t timeoutMs) {
  if (!s_ok || !s_refreshSem) {
    // No panel: still pace the caller like a ~39 Hz display instead of spinning.
    delay(timeoutMs < 26 ? timeoutMs : 26);
    return;
  }
  xSemaphoreTake(s_refreshSem, 0);   // drop a token from an earlier refresh
  xSemaphoreTake(s_refreshSem, pdMS_TO_TICKS(timeoutMs));
}

float refreshHz() { return (float)s_pclk / (float)(H_TOTAL * V_TOTAL); }

uint32_t pclkHz() { return s_pclk; }

void setPclk(uint32_t hz) {
  if (hz < PCLK_MIN) hz = PCLK_MIN;
  if (hz > PCLK_MAX) hz = PCLK_MAX;
  if (s_ok && s_panel) {
    const esp_err_t e = esp_lcd_rgb_panel_set_pclk(s_panel, hz);   // applied by the driver at the next VSYNC
    if (e != ESP_OK) {
      Serial.printf("[display] set_pclk(%lu) failed: %s\n", (unsigned long)hz, esp_err_to_name(e));
      return;
    }
  }
  s_pclk = hz;
  Serial.printf("[display] PCLK %lu Hz -> %.2f Hz refresh\n", (unsigned long)hz, (double)refreshHz());
}

uint32_t presentedFrames() { return s_presented; }

// ============================================================================
//  colour helpers
// ============================================================================
uint32_t scaleRgb(uint32_t rgb888, uint8_t level) {
  const uint32_t r = (((rgb888 >> 16) & 0xFF) * level + 127) / 255;
  const uint32_t g = (((rgb888 >> 8) & 0xFF) * level + 127) / 255;
  const uint32_t b = ((rgb888 & 0xFF) * level + 127) / 255;
  return (r << 16) | (g << 8) | b;
}

// 5-bit alpha blend with the fields spread out so all three channels are
// mixed by ONE multiply each: g in bits 21..26, r in 11..15, b in 0..4.
// f*a + b*(32-a) <= 32*field_max, which still fits each field's gap.
uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t alpha) {
  const uint32_t a = ((uint32_t)alpha + 4) >> 3;   // 0..32
  if (a == 0) return bg;
  if (a >= 32) return fg;
  const uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
  const uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
  const uint32_t r = ((f * a + b * (32 - a)) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | (r >> 16));
}

// ============================================================================
//  raw-buffer operations on the current back buffer
// ============================================================================
void fill(uint16_t c) {
  uint16_t *fb = backBuf();
  if (!fb) return;
  if ((c >> 8) == (c & 0xFF)) {          // black, white, ... -> one memset
    memset(fb, c & 0xFF, FB_BYTES);
    return;
  }
  for (int x = 0; x < W; x++) fb[x] = c; // first row, then replicate it (row 0 stays cache-hot)
  for (int y = 1; y < H; y++) memcpy(fb + (size_t)y * W, fb, W * sizeof(uint16_t));
}

void copyFrom(const uint16_t *src) {
  uint16_t *fb = backBuf();
  if (!fb || !src || src == fb) return;
  memcpy(fb, src, FB_BYTES);
}

void darkenRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t keep) {
  uint16_t *fb = backBuf();
  if (!fb || w <= 0 || h <= 0) return;
  const int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
  const int32_t x1 = (int32_t)x + w > W ? W : (int32_t)x + w;
  const int32_t y1 = (int32_t)y + h > H ? H : (int32_t)y + h;
  if (x0 >= x1 || y0 >= y1) return;
  const uint32_t k = ((uint32_t)keep + 4) >> 3;   // 0..32
  if (k >= 32) return;
  for (int32_t yy = y0; yy < y1; yy++) {
    uint16_t *p = fb + (size_t)yy * W + x0;
    const int32_t n = x1 - x0;
    if (k == 0) {
      memset(p, 0, (size_t)n * sizeof(uint16_t));
      continue;
    }
    for (int32_t i = 0; i < n; i++) {
      const uint32_t c = p[i];
      uint32_t v = (c | (c << 16)) & 0x07E0F81Fu;
      v = ((v * k) >> 5) & 0x07E0F81Fu;
      p[i] = (uint16_t)(v | (v >> 16));
    }
  }
}

void addGlow(int16_t cx, int16_t cy, int16_t radius, uint32_t rgb888, uint8_t intensity) {
  uint16_t *fb = backBuf();
  if (!fb || radius <= 0 || intensity == 0 || (rgb888 & 0xFFFFFF) == 0) return;
  if (!s_glowLutReady) initGlowLut();
  const int32_t r = radius > 1200 ? 1200 : radius;
  const int32_t r2 = r * r;
  // Bounding box, clipped.
  const int32_t y0 = cy - r < 0 ? 0 : cy - r;
  const int32_t y1 = cy + r > H - 1 ? H - 1 : cy + r;
  if (y0 > y1 || cx + r < 0 || cx - r > W - 1) return;

  // Per-call colour ramps in 8-bit units: add[i] = channel * falloff(i) * intensity.
  uint8_t addR[257], addG[257], addB[257];
  const uint32_t R = (rgb888 >> 16) & 0xFF, G = (rgb888 >> 8) & 0xFF, B = rgb888 & 0xFF;
  for (int i = 0; i <= 256; i++) {
    const uint32_t f = (uint32_t)s_glowLut[i] * intensity;   // 0..65025
    addR[i] = (uint8_t)((R * f) / 65025);
    addG[i] = (uint8_t)((G * f) / 65025);
    addB[i] = (uint8_t)((B * f) / 65025);
  }
  // d^2 -> LUT index 0..256 with one multiply: idx = d2 * (256 / r2) in 16.16.
  const uint32_t inv = (256u << 16) / (uint32_t)r2;

  for (int32_t y = y0; y <= y1; y++) {
    const int32_t dy = y - cy;
    const int32_t dy2 = dy * dy;
    if (dy2 >= r2) continue;
    const int32_t span = (int32_t)sqrtf((float)(r2 - dy2));
    int32_t xa = cx - span, xb = cx + span;
    if (xa < 0) xa = 0;
    if (xb > W - 1) xb = W - 1;
    if (xa > xb) continue;
    uint16_t *row = fb + (size_t)y * W;
    const uint8_t *bay = BAYER4[y & 3];
    for (int32_t x = xa; x <= xb; x++) {
      const int32_t dx = x - cx;
      uint32_t idx = ((uint32_t)(dx * dx + dy2) * inv) >> 16;
      if (idx > 256) idx = 256;
      const uint32_t d = bay[x & 3];                         // ordered dither 0..15
      const uint32_t ar = (addR[idx] + (d >> 1)) >> 3;       // 8-bit -> 5-bit
      const uint32_t ag = (addG[idx] + (d >> 2)) >> 2;       // 8-bit -> 6-bit
      const uint32_t ab = (addB[idx] + (d >> 1)) >> 3;
      if ((ar | ag | ab) == 0) continue;
      const uint32_t p = row[x];
      uint32_t pr = (p >> 11) + ar;
      uint32_t pg = ((p >> 5) & 0x3F) + ag;
      uint32_t pb = (p & 0x1F) + ab;
      if (pr > 31) pr = 31;
      if (pg > 63) pg = 63;
      if (pb > 31) pb = 31;
      row[x] = (uint16_t)((pr << 11) | (pg << 5) | pb);
    }
  }
}

void blitRgb565(const uint16_t *src, int16_t x, int16_t y, int16_t w, int16_t h) {
  uint16_t *fb = backBuf();
  if (!fb || !src || w <= 0 || h <= 0) return;
  const int32_t sx = x < 0 ? -x : 0, sy = y < 0 ? -y : 0;
  const int32_t dx = x + sx, dy = y + sy;
  int32_t cw = w - sx, ch = h - sy;
  if (dx + cw > W) cw = W - dx;
  if (dy + ch > H) ch = H - dy;
  if (cw <= 0 || ch <= 0) return;
  for (int32_t r = 0; r < ch; r++) {
    memcpy(fb + (size_t)(dy + r) * W + dx, src + (size_t)(sy + r) * w + sx, (size_t)cw * sizeof(uint16_t));
  }
}

void blitRgb565A8(const uint16_t *src, const uint8_t *alpha, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!alpha) {
    blitRgb565(src, x, y, w, h);
    return;
  }
  uint16_t *fb = backBuf();
  if (!fb || !src || w <= 0 || h <= 0) return;
  const int32_t sx = x < 0 ? -x : 0, sy = y < 0 ? -y : 0;
  const int32_t dx = x + sx, dy = y + sy;
  int32_t cw = w - sx, ch = h - sy;
  if (dx + cw > W) cw = W - dx;
  if (dy + ch > H) ch = H - dy;
  if (cw <= 0 || ch <= 0) return;
  for (int32_t r = 0; r < ch; r++) {
    uint16_t *d = fb + (size_t)(dy + r) * W + dx;
    const size_t so = (size_t)(sy + r) * w + sx;
    const uint16_t *s = src + so;
    const uint8_t *a = alpha + so;
    for (int32_t i = 0; i < cw; i++) {
      const uint8_t al = a[i];
      if (al == 0) continue;
      d[i] = (al == 255) ? s[i] : blend565(d[i], s[i], al);
    }
  }
}

// ============================================================================
//  text
// ============================================================================
int16_t textCentered(const char *s, int16_t cx, int16_t baseline, const uint8_t *u8g2Font,
                     uint16_t fill, uint16_t outline, uint8_t outlinePx) {
  if (!s || !*s || !u8g2Font) return 0;
  FbCanvas &cv = canvas();
  cv.setFont(u8g2Font);
  cv.setTextSize(1);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  cv.getTextBounds(s, 0, baseline, &x1, &y1, &w, &h);
  const int16_t x = (int16_t)(cx - (int16_t)(w / 2) - x1);   // x1 = ink offset from the cursor

  if (outlinePx > 0) {
    // 8-direction stamp of the text in the outline colour; diagonals at ~0.75x
    // so the outline is octagonal (rounder) instead of a square halo.
    const int o = outlinePx;
    int d = (outlinePx * 3 + 2) / 4;
    if (d < 1) d = 1;
    const int off[8][2] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}, {-d, -d}, {d, -d}, {-d, d}, {d, d}};
    cv.setTextColor(outline);           // fg == bg -> transparent background
    for (const auto &k : off) {
      cv.setCursor((int16_t)(x + k[0]), (int16_t)(baseline + k[1]));
      cv.print(s);
    }
  }
  cv.setTextColor(fill);
  cv.setCursor(x, baseline);
  cv.print(s);
  return (int16_t)(w + 2 * outlinePx);
}

int16_t textWidth(const char *s, const uint8_t *u8g2Font) {
  if (!s || !*s || !u8g2Font) return 0;
  FbCanvas &cv = canvas();
  cv.setFont(u8g2Font);
  cv.setTextSize(1);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  cv.getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return (int16_t)w;
}

}  // namespace display
