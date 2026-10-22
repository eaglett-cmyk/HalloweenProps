/*
 * ============================================================================
 *  audio.cpp — Crystal Oracle sound engine (playback only, no microphone)
 * ----------------------------------------------------------------------------
 *  Implements audio.h on the Waveshare ESP32-S3-Touch-LCD-7C(-BOX):
 *    ES8389 codec (I2C 0x10, slave) on an ESP32 I2S port at a FIXED 48 kHz,
 *    16-bit stereo, MCLK = 256*fs = 12.288 MHz on GPIO6, BCLK 44, WS 16,
 *    DOUT 15. Speakers: 2x NS4150B behind expander EXIO3 (board::ampEnable),
 *    muted in hardware while a 3.5 mm plug is inserted.
 *
 *  Tasks (both pinned to core 0; the show/drawing owns core 1):
 *    mixer    (prio 18) — every 5 ms block (240 frames) it sums, in int32:
 *               * MUSIC  bed  from a PSRAM stream buffer, looped by the streamer,
 *                             ducked -10 dB (smoothed over ~50 ms) while a voice
 *                             line is active
 *               * VOICE  line from a second PSRAM stream buffer
 *               * 4 SFX  voices playing preloaded PSRAM clips
 *               * 2 TONE voices synthesising the chimes on the fly
 *             soft-clips to int16, duplicates to L/R and i2s.write()s the
 *             block — the I2S DMA (6 x 240 frames) paces the task.
 *    streamer (prio 6)  — the ONLY task that reads streamed audio from the TF
 *             card: 8 KB reads into an internal DMA-capable buffer, copied into
 *             the stream buffers (music ~1 s, voice ~0.5 s). Music loops by
 *             seeking back to the start of the data chunk.
 *
 *  Who does what with the SD card:
 *    voice()/music() run on the caller's task (the show, loop task): they open
 *    the file and walk the WAV header right there (a few ms) so voice() can
 *    return the exact duration and reject bad files synchronously, then hand
 *    the OPEN file to the streamer through a one-slot "latest wins" mailbox.
 *    sfxLoad() reads whole clips on the caller's task (show::begin, once).
 *    The mixer never touches the SD card, the I2C bus or the logger.
 *
 *  Stopping / replacing a stream without races:
 *    FreeRTOS stream buffers are single-writer/single-reader; resetting one
 *    while the other side is mid-copy corrupts it. So a stream is only reset
 *    by the streamer, and only after the mixer has published that it is no
 *    longer reading it:
 *      caller   : epoch++, on = false, post request           (under s_mux)
 *      mixer    : sees on == false -> fades the stream out over 1 (voice) or
 *                 4 (music) blocks, then clears `mixerReading` (under s_mux,
 *                 after its last receive)
 *      streamer : waits for mixerReading == false (bounded), resets the
 *                 buffer, queues the first 8 KB of the new file, then sets
 *                 on = true if the epoch is still current      (under s_mux)
 *    The mixer only ever sets mixerReading = true under the same lock in which
 *    it sees on == true, so it cannot sneak a read in after the check.
 *
 *  Voice timing (the director waits on it): voice() returns the duration from
 *  the header; voiceBusy()/voiceRemainingMs() come from frames actually mixed
 *  (an SD underrun simply delays the end). A line that is still "busy" well
 *  past its length (dead SD card, stalled I2S) is force-stopped, so the
 *  director can never wait forever.
 *
 *  Hard rules honoured here: nothing aborts (no ESP_ERROR_CHECK / assert), every
 *  wait is bounded, no flash/NVS writes, SD reads land in internal DMA RAM,
 *  big buffers live in PSRAM, and every public call is a safe no-op when the
 *  codec is missing (ok() == false).
 *
 *  Not yet verified on hardware: the ES8389 register sequence at 48 kHz (it is
 *  Waveshare's vendored esp_codec_dev es8389_open() + set_bias_on() verbatim,
 *  which they run at 16 kHz with the same 256x MCLK ratio), the pop-free amp
 *  timing, and the chime voicings/levels on the BOX's small speakers.
 * ============================================================================
 */
#include "audio.h"
#include "board.h"
#include "media.h"
#include "vlog.h"
#include <SD_MMC.h>
#include <ESP_I2S.h>
#include <atomic>
#include <new>
#include <math.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/stream_buffer.h"

namespace audio {

namespace {

#define ALOG(fmt, ...) Serial.printf("[audio] " fmt "\n", ##__VA_ARGS__)

// ============================================================================
//  tuning
// ============================================================================
constexpr int      BLOCK            = 240;          // frames per mixer block = 5 ms @ 48 kHz
constexpr size_t   CHUNK            = 8192;         // SD read size (bytes) through the DMA bounce buffer
constexpr size_t   MUSIC_SB_BYTES   = 96000;        // 1.0 s of mono s16
constexpr size_t   VOICE_SB_BYTES   = 48000;        // 0.5 s
constexpr int      NUM_SFX          = 4;
constexpr int      NUM_TONES        = 2;            // a tap blip doesn't cut a reveal chime
constexpr int      MAX_CLIPS        = 24;
constexpr uint32_t SFX_MAX_BYTES    = 8u * SAMPLE_RATE * 2;   // 8 s per preloaded clip (longer = truncated)
constexpr int      CMD_QUEUE_LEN    = 16;

constexpr float    DUCK_GAIN        = 0.3162f;      // -10 dB under a voice line
constexpr float    DUCK_STEP        = (1.0f - DUCK_GAIN) / 10.0f;   // full duck in 10 blocks = 50 ms
constexpr float    MUSIC_RAMP_STEP  = 0.25f;        // gain change per block: 0 -> 1 in 20 ms
constexpr float    VOICE_RAMP_STEP  = 1.0f;         // voice fades in over one block (5 ms)
constexpr uint8_t  MUSIC_FADE_BLOCKS = 4;           // fade-out on stop/replace: 20 ms
constexpr uint8_t  VOICE_FADE_BLOCKS = 1;           // 5 ms

constexpr uint32_t I2S_WRITE_TIMEOUT_MS = 50;       // a stalled DMA can't hang the mixer
constexpr uint32_t MIXER_IDLE_WAIT_MS   = 250;      // streamer's bound on the reset handshake
constexpr uint32_t AMP_ON_DELAY_MS      = 40;       // codec settles, DAC outputs silence, then amp on

constexpr int      MIXER_STACK      = 5120;         // no printf in the mixer; headroom for the lazy FPU save area
constexpr int      STREAMER_STACK   = 6144;         // FatFs + VFS + sdmmc + printf
constexpr UBaseType_t MIXER_PRIO    = 18;
constexpr UBaseType_t STREAMER_PRIO = 6;
constexpr BaseType_t AUDIO_CORE     = 0;

// ============================================================================
//  ES8389 (Waveshare's vendored esp_codec_dev es8389.c, slave mode, MCLK from
//  the pin, I2S Philips, 16-bit, no_dac_ref = false — checked line by line)
// ============================================================================
constexpr uint8_t ES_REG_DAC_FMT  = 0x40;   // bits[1:0] = DAC mute (the driver's set_mute() wrongly writes 0x20)
constexpr uint8_t ES_REG_DAC_VOLL = 0x46;   // 0.5 dB/step, 0x00 = -95.5 dB, 0xBF = 0 dB (never go above)
constexpr uint8_t ES_REG_DAC_VOLR = 0x47;
constexpr uint8_t ES_VOL_0DB      = 0xBF;

// es8389_open(): reset + analog setup, clock manager for a 256*fs MCLK, ADC/DAC
// formats (0x20/0x40 = 0x60: S16_LE, I2S), start, volumes 0 dB; then the
// open() tail for slave / MCLK pin / no inversion / internal DAC reference
// (0x23 |= 0x80, 0xF0 = 0x1A). es8389_set_fs() only re-applies 16-bit + I2S
// (already set here) and clears 0x0C[7:5] — done with a read-modify-write below.
const uint8_t ES8389_OPEN_SEQ[][2] = {
  {0xF3, 0x00}, {0x00, 0x7E}, {0xF3, 0x38}, {0x24, 0x64}, {0x25, 0x04}, {0x45, 0x03}, {0x60, 0x2A}, {0x61, 0xC9},
  {0x62, 0x4F}, {0x63, 0x06}, {0x6B, 0x00}, {0x6D, 0x16}, {0x6E, 0xAA}, {0x6F, 0x66}, {0x70, 0x99}, {0x23, 0x00},
  {0x72, 0x10}, {0x73, 0x10}, {0x10, 0xC4}, {0x01, 0x08}, {0xF1, 0x00}, {0x12, 0x01}, {0x13, 0x01}, {0x14, 0x01},
  {0x15, 0x01}, {0x16, 0x35}, {0x17, 0x09}, {0x18, 0x91}, {0x19, 0x28}, {0x1A, 0x01}, {0x1B, 0x01}, {0x1C, 0x11},
  {0x2A, 0x00}, {0x20, 0x60}, {0x40, 0x60}, {0xF0, 0x01}, {0x02, 0x00}, {0x04, 0x00}, {0x05, 0x10}, {0x06, 0x00},
  {0x07, 0xC0}, {0x08, 0x00}, {0x09, 0xC0}, {0x0A, 0x80}, {0x0B, 0x04}, {0x0C, 0x01}, {0x0D, 0x00}, {0x0F, 0x10},
  {0x21, 0x1F}, {0x22, 0x7F}, {0x2F, 0xC0}, {0x30, 0xF4}, {0x31, 0x00}, {0x44, 0x00}, {0x41, 0x7F}, {0x42, 0x7F},
  {0x43, 0x10}, {0x49, 0x0F}, {0x4C, 0xC0}, {0x00, 0x00}, {0x03, 0xC1}, {0x00, 0x01}, {0x4D, 0x00}, {0x26, 0xBF},
  {0x27, 0xBF}, {0x28, 0xBF}, {0x46, 0xBF}, {0x47, 0xBF}, {0x48, 0xBE},
  // open() tail: slave (0x01 bit0 = 0), MCLK from pin, no MCLK/SCLK inversion (0x02 = 0), internal DAC reference
  {0x01, 0x08}, {0x02, 0x00}, {0x23, 0x80}, {0xF0, 0x1A},
};

// BENCH FALLBACK (default off). esp_codec_dev (vendored AND upstream 1.6.2) leaves
// the clock manager at open()'s defaults when MCLK comes from the pin; Waveshare
// runs that at 16 kHz. If the first 48 kHz test sounds wrong (wrong pitch,
// crackle, silence), set this to true: it applies the driver table's explicit
// 256x / 12.288 MHz / 48 kHz row ({reg, mask, value}), as the Linux driver does
// in hw_params().
constexpr bool ES8389_EXPLICIT_48K_CLOCKS = false;
const uint8_t ES8389_48K_COEFFS[][3] = {
  {0x04, 0xFF, 0x01}, {0x05, 0xFF, 0x41}, {0x06, 0xFF, 0x04}, {0x07, 0xFF, 0xD0}, {0x08, 0xFF, 0x10},
  {0x09, 0xFF, 0xD1}, {0x0A, 0xFF, 0x80}, {0x0F, 0xC0, 0x40}, {0x11, 0xFF, 0x00}, {0x21, 0xFF, 0x1F},
  {0x22, 0xFF, 0x7F}, {0x26, 0xFF, 0xBF}, {0x30, 0xC0, 0xC0}, {0x41, 0xFF, 0x7F}, {0x42, 0xFF, 0x7F},
  {0x43, 0x81, 0x00}, {0xF0, 0x73, 0x12}, {0xF1, 0xFF, 0x00}, {0x16, 0xFF, 0x35}, {0x18, 0xFF, 0x91},
  {0x19, 0xFF, 0x28},
};

// ============================================================================
//  chime synthesis tables
// ============================================================================
enum Timbre : uint8_t { T_SINE = 0, T_BELL, T_GLASS, T_REED, T_SOFT };
constexpr uint8_t NF_WAH = 0x01;   // harmonics swell open and closed over the note ("wah")

struct NoteSpec {
  uint16_t startMs;    // offset from the chime start
  uint16_t durMs;      // whole note including the release
  uint16_t f0, f1;     // Hz at the start / end (glide); f1 = 0 -> steady
  uint8_t  level;      // 0..255 amplitude
  uint8_t  timbre;     // Timbre
  uint8_t  attackMs;
  uint16_t decayMs;    // exponential time constant after the attack (0 = sustain)
  uint16_t releaseMs;  // linear fade at the end of the note
  uint8_t  vibCents;   // delayed vibrato depth (0 = none)
  uint8_t  flags;      // NF_*
};

// CHIME_ABSORB — rising two-note shimmer (E5 -> B5, airy octave on top)
const NoteSpec CH_ABSORB[] = {
  {  0, 420,  659, 0, 125, T_BELL,   3, 180,  60,  0, 0},
  { 90, 650,  988, 0, 125, T_BELL,   3, 260,  90,  8, 0},
  { 90, 650, 1976, 0,  40, T_SINE,  10, 220,  90, 12, 0},
  {160, 500, 2637, 0,  25, T_SINE,  20, 150,  90,  0, 0},
};
// CHIME_REVEAL — bright C-major arpeggio up, glassy sparkle on top
const NoteSpec CH_REVEAL[] = {
  {  0, 700,  523, 0, 110, T_BELL,   2, 300,  80,  0, 0},
  { 70, 700,  659, 0, 105, T_BELL,   2, 300,  80,  0, 0},
  {140, 700,  784, 0, 100, T_BELL,   2, 320,  80,  0, 0},
  {210, 900, 1047, 0, 110, T_BELL,   2, 420, 120,  6, 0},
  {280, 600, 2093, 0,  40, T_GLASS,  2, 200, 100,  0, 0},
};
// CHIME_RARE — three-note sparkle (G6 B6 D7) over a soft G5 bell
const NoteSpec CH_RARE[] = {
  {  0, 800,  784, 0,  80, T_BELL,   3, 380, 120,  0, 0},
  {  0, 380, 1568, 0, 100, T_GLASS,  1, 140,  60,  0, 0},
  { 60, 380, 1976, 0,  95, T_GLASS,  1, 140,  60,  0, 0},
  {120, 600, 2349, 0, 100, T_GLASS,  1, 220, 120, 10, 0},
};
// CHIME_LEGEND — brassy fanfare arpeggio G4 C5 E5 -> held G5 with a C-major
// bell chord and a falling-star sparkle run
const NoteSpec CH_LEGEND[] = {
  {  0,  200,  392, 0, 120, T_REED,   8,   0,  40,  0, 0},
  {150,  200,  523, 0, 120, T_REED,   8,   0,  40,  0, 0},
  {300,  200,  659, 0, 120, T_REED,   8,   0,  40,  0, 0},
  {450, 1400,  784, 0, 110, T_REED,  10,   0, 350, 15, 0},
  {450, 1400, 1047, 0,  90, T_BELL,   2, 700, 300,  0, 0},
  {450, 1400, 1319, 0,  70, T_BELL,   2, 700, 300,  0, 0},
  {650,  500, 3136, 0,  40, T_GLASS,  1, 180,  80,  0, 0},
  {750,  500, 2637, 0,  40, T_GLASS,  1, 180,  80,  0, 0},
  {850,  500, 2093, 0,  40, T_GLASS,  1, 180,  80,  0, 0},
};
// CHIME_TRICK — the silly sad-trombone "wah wah wah wahhh" (G4 F#4 F4 E4)
const NoteSpec CH_TRICK[] = {
  {   0, 320, 392, 384, 150, T_REED, 40, 0,  60,  0, NF_WAH},
  { 340, 320, 370, 362, 150, T_REED, 40, 0,  60,  0, NF_WAH},
  { 680, 320, 349, 341, 150, T_REED, 40, 0,  60,  0, NF_WAH},
  {1020, 900, 330, 318, 160, T_REED, 40, 0, 250, 35, NF_WAH},
};
// CHIME_TAP — tiny downward blip
const NoteSpec CH_TAP[] = {
  {0, 45, 1900, 1200, 130, T_SINE, 1, 15, 10, 0, 0},
};
// CHIME_ERROR — soft low double "bloop"
const NoteSpec CH_ERROR[] = {
  {  0, 150, 300, 200, 160, T_SOFT, 6, 0, 50, 0, 0},
  {200, 200, 270, 170, 160, T_SOFT, 6, 0, 70, 0, 0},
};
// CHIME_TEST — 440 Hz for 0.5 s (service menu speaker test)
const NoteSpec CH_TEST[] = {
  {0, 500, 440, 0, 255, T_SINE, 5, 0, 5, 0, 0},
};

struct ChimeDef {
  const NoteSpec *notes;
  uint8_t count;
};
#define CHIME_ENTRY(a) {a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
const ChimeDef CHIMES[] = {   // indexed by audio::Chime
  CHIME_ENTRY(CH_ABSORB), CHIME_ENTRY(CH_REVEAL), CHIME_ENTRY(CH_RARE), CHIME_ENTRY(CH_LEGEND),
  CHIME_ENTRY(CH_TRICK),  CHIME_ENTRY(CH_TAP),    CHIME_ENTRY(CH_ERROR), CHIME_ENTRY(CH_TEST),
};
#undef CHIME_ENTRY
constexpr int NUM_CHIMES = sizeof(CHIMES) / sizeof(CHIMES[0]);
constexpr int MAX_NOTES = 9;   // longest table above (CH_LEGEND)
static_assert(NUM_CHIMES == CHIME_TEST + 1, "CHIMES[] must match the audio::Chime enum");

// ============================================================================
//  shared state between the caller (show, loop task), the streamer and the mixer
// ============================================================================
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;   // guards every StreamShared field

struct StreamShared {
  StreamBufferHandle_t sb = nullptr;   // PSRAM; written by the streamer only, read by the mixer only
  uint32_t epoch = 0;                  // bumped by the caller on every start/stop
  bool     on = false;                 // the mixer may read sb (set by the streamer after reset + prefill)
  bool     active = false;             // logically playing: pending start, playing or draining
  bool     mixerReading = false;       // the mixer is (or is about to be) receiving from sb
  bool     eof = false;                // voice: the whole line is queued in sb
  float    gain = 1.0f;                // target gain 0..1
  uint32_t totalFrames = 0;            // voice: frames in the line (header; corrected at eof)
  uint32_t consumedFrames = 0;         // voice: frames the mixer has played
  // one-slot request mailbox, latest wins (file == nullptr -> stop)
  bool      reqPending = false;
  fs::File *reqFile = nullptr;
  uint32_t  reqDataStart = 0, reqDataLen = 0, reqEpoch = 0;
};
StreamShared s_music, s_voice;

struct Clip {
  char path[80];
  int16_t *pcm;
  uint32_t frames;
};
Clip s_clips[MAX_CLIPS];
std::atomic<int> s_clipCount{0};

enum CmdType : uint8_t { CMD_SFX, CMD_SFX_STOP, CMD_CHIME, CMD_TONE };
struct Cmd {
  uint8_t type;
  uint8_t chime;
  float gain;
  const int16_t *pcm;
  uint32_t frames;
  NoteSpec note;   // CMD_TONE
};

I2SClass s_i2s;                       // first free I2S port (the pins are what matter)
QueueHandle_t s_cmdQ = nullptr;
TaskHandle_t s_mixerTask = nullptr;
TaskHandle_t s_streamTask = nullptr;
uint8_t *s_bounce = nullptr;          // streamer's internal DMA-capable read buffer
bool s_begun = false;
volatile bool s_ok = false;
volatile uint8_t s_volume = 75;
uint8_t s_reg40 = 0x60;               // shadow of ES8389 reg 0x40 (S16_LE | I2S, unmuted)
std::atomic<uint8_t> s_level{0};
std::atomic<uint32_t> s_musicUnderruns{0}, s_voiceUnderruns{0}, s_i2sErrors{0};

// caller-side only (loop task)
char s_musicPath[96] = {0};
uint32_t s_voiceDeadline = 0;

int16_t s_sinTab[1025];               // one sine period + guard point, for the chime oscillators

// ============================================================================
//  small helpers
// ============================================================================
inline uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline float pctGain(uint8_t pct) { return (pct >= 100 ? 100 : pct) / 100.0f; }
inline uint32_t msToFrames(uint32_t ms) { return ms * (SAMPLE_RATE / 1000); }

inline float approach(float v, float target, float step) {
  if (v < target) return (target - v <= step) ? target : v + step;
  if (v > target) return (v - target <= step) ? target : v - step;
  return v;
}

// FNV-1a: lets us log "bad WAV" once per path instead of on every call.
uint32_t pathHash(const char *s) {
  uint32_t h = 2166136261u;
  while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}
bool firstComplaint(const char *path) {
  static uint32_t seen[16];
  static uint8_t n = 0;
  const uint32_t h = pathHash(path);
  for (uint8_t i = 0; i < n && i < 16; i++)
    if (seen[i] == h) return false;
  seen[n % 16] = h;
  n++;
  return true;
}

// ============================================================================
//  ES8389 register access (board helpers: mutex-protected, retrying, never abort)
// ============================================================================
bool codecWrite(uint8_t reg, uint8_t val) { return board::i2cWriteReg8(board::ADDR_ES8389, reg, &val, 1); }
bool codecRead(uint8_t reg, uint8_t &val) { return board::i2cReadReg8(board::ADDR_ES8389, reg, &val, 1); }
// Read-modify-write. If the read fails we don't write blind.
bool codecUpdate(uint8_t reg, uint8_t mask, uint8_t val) {
  uint8_t v;
  if (!codecRead(reg, v)) return false;
  return codecWrite(reg, (uint8_t)((v & ~mask) | (val & mask)));
}

// Returns the number of failed register accesses (0 = success).
int codecInit() {
  int fails = 0;
  for (const auto &rv : ES8389_OPEN_SEQ)
    if (!codecWrite(rv[0], rv[1])) fails++;
  if (ES8389_EXPLICIT_48K_CLOCKS) {
    for (const auto &c : ES8389_48K_COEFFS) {
      const bool ok = (c[1] == 0xFF) ? codecWrite(c[0], c[2]) : codecUpdate(c[0], c[1], c[2]);
      if (!ok) fails++;
    }
  }
  if (!codecUpdate(0x0C, 0xE0, 0x00)) fails++;   // es8389_config_fmt(I2S normal)
  // es8389_set_bias_on() (what esp_codec_dev's enable() does)
  if (!codecWrite(0x4D, 0x00)) fails++;
  if (!codecUpdate(0x69, 0x20, 0x20)) fails++;
  if (!codecWrite(0x61, 0xD9)) fails++;
  if (!codecWrite(0x64, 0x8F)) fails++;
  if (!codecWrite(0x10, 0xE4)) fails++;
  if (!codecWrite(0x00, 0x01)) fails++;
  if (!codecWrite(0x03, 0xC3)) fails++;
  if (!codecWrite(0x24, 0x6A)) fails++;
  if (!codecWrite(0x25, 0x0A)) fails++;
  s_reg40 = 0x60;
  return fails;
}

void dacMute(bool mute) {
  uint8_t v;
  if (codecRead(ES_REG_DAC_FMT, v)) s_reg40 = v;   // else trust the shadow
  s_reg40 = (uint8_t)((s_reg40 & ~0x03) | (mute ? 0x03 : 0x00));
  codecWrite(ES_REG_DAC_FMT, s_reg40);
}

// 0 -> mute; 1..100 -> -50 dB .. 0 dB, linear in dB (0.5 dB register steps).
uint8_t volumeReg(uint8_t pct) {
  if (pct == 0) return 0x00;
  if (pct > 100) pct = 100;
  const int steps = ((100 - pct) * 100 + 49) / 99;   // 0..100 half-dB steps below 0 dB
  return (uint8_t)(ES_VOL_0DB - steps);              // never above 0xBF
}

void applyVolume(uint8_t pct) {
  const uint8_t r = volumeReg(pct);
  const bool okL = codecWrite(ES_REG_DAC_VOLL, r);
  const bool okR = codecWrite(ES_REG_DAC_VOLR, r);
  dacMute(pct == 0);
  if (!okL || !okR) ALOG("volume write failed (I2C)");
}

// ============================================================================
//  WAV header (chunk walker) — caller side
// ============================================================================
struct WavInfo {
  uint32_t dataStart = 0;   // file offset of the first sample
  uint32_t dataLen = 0;     // bytes of sample data (even, clamped to the file)
};

void reject(const char *path, const char *why) {
  ALOG("%s rejected: %s", path, why);
  if (firstComplaint(path)) vlog::event("audio: %s rejected: %s", path, why);
}

bool parseWav(fs::File &f, const char *path, WavInfo &wi) {
  uint8_t h[12];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) {
    reject(path, "not a RIFF/WAVE file");
    return false;
  }
  const uint32_t fileSize = (uint32_t)f.size();
  uint32_t pos = 12;
  bool haveFmt = false;
  uint16_t fmt = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  for (int guard = 0; guard < 64; guard++) {   // bounded walk (corrupt files can't spin us)
    uint8_t c[8];
    if (f.read(c, 8) != 8) break;
    pos += 8;
    const uint32_t len = le32(c + 4);
    uint32_t skip = len + (len & 1);   // chunks are word-aligned
    if (memcmp(c, "fmt ", 4) == 0) {
      uint8_t fm[40] = {0};
      const uint32_t want = len < sizeof(fm) ? len : (uint32_t)sizeof(fm);
      if (len < 16 || f.read(fm, want) != want) {
        reject(path, "bad fmt chunk");
        return false;
      }
      pos += want;
      skip -= want;
      fmt = le16(fm);
      channels = le16(fm + 2);
      rate = le32(fm + 4);
      bits = le16(fm + 14);
      if (fmt == 0xFFFE && len >= 26) fmt = le16(fm + 24);   // WAVE_FORMAT_EXTENSIBLE: GUID starts with the tag
      haveFmt = true;
    } else if (memcmp(c, "data", 4) == 0) {
      if (!haveFmt) { reject(path, "data chunk before fmt"); return false; }
      if (fmt != 1) { reject(path, "not PCM"); return false; }
      if (channels != 1) { reject(path, "not mono"); return false; }
      if (rate != SAMPLE_RATE) { reject(path, "not 48000 Hz"); return false; }
      if (bits != 16) { reject(path, "not 16-bit"); return false; }
      uint32_t n = len;
      const uint32_t avail = fileSize > pos ? fileSize - pos : 0;
      if (n > avail) n = avail;   // truncated file, or a streaming header (0xFFFFFFFF)
      n &= ~1u;
      if (n == 0) { reject(path, "no samples"); return false; }
      wi.dataStart = pos;
      wi.dataLen = n;
      return true;   // file position is now at the first sample
    }
    if (skip) {
      pos += skip;
      if (pos > fileSize || !f.seek(pos)) break;
    }
  }
  reject(path, haveFmt ? "no data chunk" : "no fmt chunk");
  return false;
}

// Opens + validates on the caller's task. On success `out` owns a heap File
// positioned at the first sample.
bool openWav(const char *path, fs::File *&out, WavInfo &wi) {
  out = nullptr;
  if (!path || !*path) return false;
  if (!media::sdOk()) return false;
  fs::File f = SD_MMC.open(path, FILE_READ);
  if (!f) {
    ALOG("%s missing", path);
    return false;
  }
  if (!parseWav(f, path, wi)) {
    f.close();
    return false;
  }
  out = new (std::nothrow) fs::File(f);   // shares the open file; `f` just drops its reference
  if (!out) {
    ALOG("out of memory opening %s", path);
    f.close();
    return false;
  }
  return true;
}

// Caller side: replace whatever stream S is doing with `file` (nullptr = stop).
void replaceStream(StreamShared &S, fs::File *file, const WavInfo *wi, float gain) {
  fs::File *stale = nullptr;
  bool post = true;
  portENTER_CRITICAL(&s_mux);
  if (!file && !S.active && !S.reqPending && !S.on) {
    post = false;   // already silent: nothing to stop
  } else {
    S.epoch++;
    S.on = false;   // the mixer stops reading at its next block (with a short fade)
    S.active = (file != nullptr);
    S.eof = false;
    S.gain = gain;
    S.totalFrames = wi ? wi->dataLen / 2 : 0;
    S.consumedFrames = 0;
    if (S.reqPending) stale = S.reqFile;   // superseded before the streamer took it
    S.reqPending = true;
    S.reqFile = file;
    S.reqDataStart = wi ? wi->dataStart : 0;
    S.reqDataLen = wi ? wi->dataLen : 0;
    S.reqEpoch = S.epoch;
  }
  portEXIT_CRITICAL(&s_mux);
  if (stale) {
    stale->close();
    delete stale;
  }
  if (post && s_streamTask) xTaskNotifyGive(s_streamTask);
}

// ============================================================================
//  streamer task (core 0, prio 6): the only reader of streamed audio
// ============================================================================
struct Feed {
  fs::File *file = nullptr;
  uint32_t dataStart = 0, dataLen = 0, left = 0, epoch = 0, queuedFrames = 0;
  uint8_t errors = 0;
};

void closeFeed(Feed &F) {
  if (F.file) {
    F.file->close();
    delete F.file;
    F.file = nullptr;
  }
  F.left = 0;
}

// Bounded wait until the mixer has finished its last receive on S.
void waitMixerIdle(StreamShared &S, const char *name) {
  const uint32_t t0 = millis();
  for (;;) {
    bool reading;
    portENTER_CRITICAL(&s_mux);
    reading = S.mixerReading;
    portEXIT_CRITICAL(&s_mux);
    if (!reading) return;
    if (millis() - t0 > MIXER_IDLE_WAIT_MS) {
      ALOG("%s: mixer did not release the stream in %lu ms (mixer stalled?)", name, (unsigned long)MIXER_IDLE_WAIT_MS);
      return;   // a stalled mixer is not inside a receive; resetting is the lesser evil
    }
    vTaskDelay(1);
  }
}

// Voice data fully queued (or the file ended early / failed).
void finishVoiceFeed(Feed &F) {
  closeFeed(F);
  portENTER_CRITICAL(&s_mux);
  if (s_voice.epoch == F.epoch) {
    s_voice.eof = true;
    s_voice.totalFrames = F.queuedFrames;
    if (F.queuedFrames == 0) {   // nothing playable at all
      s_voice.active = false;
      s_voice.on = false;
    }
  }
  portEXIT_CRITICAL(&s_mux);
}

void failMusic(Feed &F, const char *why) {
  ALOG("music stopped: %s", why);
  closeFeed(F);
  portENTER_CRITICAL(&s_mux);
  if (s_music.epoch == F.epoch) {
    s_music.active = false;
    s_music.on = false;   // the mixer fades out what is already buffered
  }
  portEXIT_CRITICAL(&s_mux);
}

// Moves one chunk from the file into S.sb if there is room.
// Returns true if it did something (the caller keeps looping).
bool fillChunk(StreamShared &S, Feed &F, bool isVoice) {
  if (!F.file) return false;
  if (xStreamBufferSpacesAvailable(S.sb) < CHUNK) return false;
  if (F.left == 0) {
    if (isVoice) {
      finishVoiceFeed(F);
      return true;
    }
    if (!F.file->seek(F.dataStart)) {   // music: loop back to the first sample
      failMusic(F, "seek failed");
      return true;
    }
    F.left = F.dataLen;
  }
  const size_t want = (F.left < CHUNK ? F.left : CHUNK) & ~(size_t)1;
  size_t n = F.file->read(s_bounce, want);
  n &= ~(size_t)1;
  if (n > 0) {
    const size_t sent = xStreamBufferSend(S.sb, s_bounce, n, 0);   // room was checked; we are the only writer
    if (sent != n) ALOG("stream buffer short write (%u of %u)", (unsigned)sent, (unsigned)n);
    F.queuedFrames += (uint32_t)(sent / 2);
    F.left -= (uint32_t)n;
  }
  if (n != want) {   // read error or the card went away
    if (isVoice) {
      ALOG("voice read error after %lu frames", (unsigned long)F.queuedFrames);
      finishVoiceFeed(F);
    } else if (++F.errors >= 3) {
      failMusic(F, "read errors");
    } else {
      F.left = 0;   // retry from the top of the loop
    }
    return true;
  }
  F.errors = 0;
  if (isVoice && F.left == 0) finishVoiceFeed(F);
  return true;
}

// Takes a pending request for S (if any): closes the old file, waits until the
// mixer is off the buffer, resets it, primes the new file and switches it on.
void serviceRequest(StreamShared &S, Feed &F, bool isVoice) {
  fs::File *file = nullptr;
  uint32_t ds = 0, dl = 0, ep = 0;
  bool pending;
  portENTER_CRITICAL(&s_mux);
  pending = S.reqPending;
  if (pending) {
    file = S.reqFile;
    ds = S.reqDataStart;
    dl = S.reqDataLen;
    ep = S.reqEpoch;
    S.reqPending = false;
    S.reqFile = nullptr;
  }
  portEXIT_CRITICAL(&s_mux);
  if (!pending) return;

  closeFeed(F);
  waitMixerIdle(S, isVoice ? "voice" : "music");
  if (xStreamBufferReset(S.sb) != pdPASS) ALOG("%s: stream buffer reset refused", isVoice ? "voice" : "music");
  if (!file) return;   // a stop request: done

  F.file = file;
  F.dataStart = ds;
  F.dataLen = dl;
  F.left = dl;
  F.epoch = ep;
  F.queuedFrames = 0;
  F.errors = 0;
  fillChunk(S, F, isVoice);   // prime ~85 ms so the mixer starts with data

  portENTER_CRITICAL(&s_mux);
  if (S.epoch == ep && S.active) S.on = true;
  portEXIT_CRITICAL(&s_mux);
}

void streamerTask(void *) {
  Feed musicFeed, voiceFeed;
  uint32_t lastReport = 0, repMusic = 0, repVoice = 0, repI2s = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));   // woken by requests; 10 ms top-up cadence otherwise
    // Voice first: it is the shorter buffer and the director waits on it.
    for (int guard = 0; guard < 48; guard++) {
      serviceRequest(s_voice, voiceFeed, true);
      serviceRequest(s_music, musicFeed, false);
      if (fillChunk(s_voice, voiceFeed, true)) continue;
      if (fillChunk(s_music, musicFeed, false)) continue;
      break;
    }
    // Occasional health report (never from the mixer itself).
    const uint32_t now = millis();
    if (now - lastReport >= 10000) {
      lastReport = now;
      const uint32_t um = s_musicUnderruns.load(), uv = s_voiceUnderruns.load(), ue = s_i2sErrors.load();
      if (um != repMusic || uv != repVoice || ue != repI2s) {
        ALOG("underruns music %lu voice %lu, i2s write errors %lu", (unsigned long)um, (unsigned long)uv, (unsigned long)ue);
        repMusic = um;
        repVoice = uv;
        repI2s = ue;
      }
    }
  }
}

// ============================================================================
//  mixer (core 0, prio 18)
// ============================================================================
enum : uint8_t { ST_IDLE = 0, ST_PLAY, ST_FADE };
struct MixStream {
  uint8_t st = ST_IDLE;
  uint8_t fadeLeft = 0;
  float g = 0.0f;          // current gain
  float fadeStep = 0.0f;
  uint32_t epoch = 0;      // epoch this PLAY belongs to
};

struct SfxSlot {
  const int16_t *pcm = nullptr;
  uint32_t frames = 0, pos = 0, seq = 0;
  float gain = 0.0f;
  bool active = false, fading = false;
};

struct NoteRun {
  uint32_t ph = 0, ph2 = 0;
  float dec = 1.0f;
};
struct ToneSlot {
  bool active = false;
  const NoteSpec *notes = nullptr;
  uint8_t count = 0;
  NoteSpec custom{};       // tone()
  uint32_t t = 0;          // frames since start
  uint32_t endFrames = 0;
  uint32_t seq = 0;
  float gain = 0.0f;
  NoteRun run[MAX_NOTES];
};

// Mixer-owned (only the mixer task touches these after begin()).
SfxSlot s_sfx[NUM_SFX];
ToneSlot s_tone[NUM_TONES];
uint32_t s_seq = 0;

inline float sine(uint32_t ph) {
  const uint32_t i = ph >> 22;   // 1024-entry table
  const float frac = (float)(ph & 0x3FFFFF) * (1.0f / 4194304.0f);
  const float a = s_sinTab[i], b = s_sinTab[i + 1];
  return (a + (b - a) * frac) * (1.0f / 32767.0f);
}

inline float wave(uint8_t timbre, uint32_t ph, uint32_t ph2, float w) {
  const float s1 = sine(ph);
  switch (timbre) {
    case T_BELL:  return (s1 + 0.30f * sine(ph2)) * (1.0f / 1.30f);   // inharmonic 2.756x partial
    case T_GLASS: return (s1 + 0.25f * sine(ph * 3)) * (1.0f / 1.25f);
    case T_REED:  return (s1 + w * (0.50f * sine(ph * 2) + 0.30f * sine(ph * 3) + 0.15f * sine(ph * 4))) / (1.0f + 0.95f * w);
    case T_SOFT:  return (s1 + 0.15f * sine(ph * 2)) * (1.0f / 1.15f);
    default:      return s1;
  }
}

void startTone(const NoteSpec *notes, uint8_t count, float gain, const NoteSpec *custom) {
  ToneSlot *slot = nullptr;
  for (auto &t : s_tone)
    if (!t.active) { slot = &t; break; }
  if (!slot) {   // steal the oldest
    slot = &s_tone[0];
    for (auto &t : s_tone)
      if (t.seq < slot->seq) slot = &t;
  }
  if (custom) {
    slot->custom = *custom;
    notes = &slot->custom;
    count = 1;
  }
  if (count > MAX_NOTES) count = MAX_NOTES;
  slot->notes = notes;
  slot->count = count;
  slot->t = 0;
  slot->gain = gain;
  slot->seq = ++s_seq;
  slot->endFrames = 0;
  for (uint8_t k = 0; k < count; k++) {
    const uint32_t e = msToFrames((uint32_t)notes[k].startMs + notes[k].durMs);
    if (e > slot->endFrames) slot->endFrames = e;
    slot->run[k] = NoteRun{};
  }
  slot->active = count > 0;
}

// Renders one block of a tone slot into buf (overwrites).
void renderTone(ToneSlot &s, float *buf) {
  memset(buf, 0, BLOCK * sizeof(float));
  const uint32_t b0 = s.t, b1 = s.t + BLOCK;
  for (uint8_t k = 0; k < s.count; k++) {
    const NoteSpec &ns = s.notes[k];
    const uint32_t st = msToFrames(ns.startMs);
    const uint32_t du = msToFrames(ns.durMs);
    const uint32_t en = st + du;
    if (du == 0 || b1 <= st || b0 >= en) continue;
    NoteRun &r = s.run[k];
    const uint32_t i0 = st > b0 ? st - b0 : 0;
    const uint32_t i1 = en < b1 ? en - b0 : BLOCK;

    // Per-block pitch (glide + delayed vibrato) and brightness.
    const uint32_t tsBlock = b0 + i0 - st;
    const float p = (float)tsBlock / (float)du;
    const float fEnd = ns.f1 ? ns.f1 : ns.f0;
    float f = ns.f0 + (fEnd - ns.f0) * p;
    if (ns.vibCents && tsBlock > msToFrames(120))
      f *= 1.0f + ns.vibCents * 0.000578f * sinf(6.2831853f * 5.5f * (float)tsBlock / (float)SAMPLE_RATE);
    const uint32_t inc = (uint32_t)(f * 89478.4853f);            // 2^32 / 48000
    const uint32_t inc2 = (uint32_t)(f * 2.756f * 89478.4853f);  // bell partial
    const float w = (ns.flags & NF_WAH) ? 0.15f + 0.85f * sinf(3.1415927f * p) : 1.0f;

    uint32_t A = msToFrames(ns.attackMs);
    if (A == 0) A = 1;
    uint32_t R = msToFrames(ns.releaseMs);
    if (R > du) R = du;
    const float invA = 1.0f / (float)A;
    const float invR = R ? 1.0f / (float)R : 0.0f;
    const float decMul = ns.decayMs ? expf(-1.0f / (float)msToFrames(ns.decayMs)) : 1.0f;
    const float amp = ns.level * (1.0f / 255.0f);

    for (uint32_t i = i0; i < i1; i++) {
      const uint32_t ts = b0 + i - st;
      float e;
      if (ts < A) {
        e = (float)ts * invA;
      } else {
        e = r.dec;
        r.dec *= decMul;
      }
      if (R && ts + R > du) e *= (float)(du - ts) * invR;
      buf[i] += wave(ns.timbre, r.ph, r.ph2, w) * e * amp;
      r.ph += inc;
      r.ph2 += inc2;
    }
  }
  s.t = b1;
  if (s.t >= s.endFrames) s.active = false;
}

void handleCmd(const Cmd &c) {
  switch (c.type) {
    case CMD_SFX: {
      SfxSlot *slot = nullptr;
      for (auto &s : s_sfx)
        if (!s.active) { slot = &s; break; }
      if (!slot) {   // steal the oldest
        slot = &s_sfx[0];
        for (auto &s : s_sfx)
          if (s.seq < slot->seq) slot = &s;
      }
      slot->pcm = c.pcm;
      slot->frames = c.frames;
      slot->pos = 0;
      slot->gain = c.gain;
      slot->seq = ++s_seq;
      slot->fading = false;
      slot->active = c.pcm && c.frames;
      break;
    }
    case CMD_SFX_STOP:
      for (auto &s : s_sfx)
        if (s.active) s.fading = true;   // ramped to zero over the next block
      break;
    case CMD_CHIME:
      if (c.chime < NUM_CHIMES) startTone(CHIMES[c.chime].notes, CHIMES[c.chime].count, c.gain, nullptr);
      break;
    case CMD_TONE:
      startTone(nullptr, 0, c.gain, &c.note);
      break;
    default:
      break;
  }
}

// IDLE -> PLAY when the streamer switched the stream on; PLAY -> FADE when the
// caller switched it off. Runs under s_mux; this is the only place that sets
// mixerReading = true.
void transition(StreamShared &S, MixStream &m, uint8_t fadeBlocks) {
  if (m.st == ST_IDLE && S.on) {
    m.st = ST_PLAY;
    m.g = 0.0f;
    m.epoch = S.epoch;
  } else if (m.st == ST_PLAY && (!S.on || S.epoch != m.epoch)) {
    m.st = ST_FADE;
    m.fadeLeft = fadeBlocks;
    m.fadeStep = m.g / (float)fadeBlocks;
  }
  S.mixerReading = (m.st != ST_IDLE);
}

// Gain at the end of this block (and FADE bookkeeping).
float nextGain(MixStream &m, float target, float step) {
  if (m.st == ST_FADE) {
    float g = m.g - m.fadeStep;
    if (m.fadeLeft) m.fadeLeft--;
    if (m.fadeLeft == 0 || g < 0.0f) {
      g = 0.0f;
      m.st = ST_IDLE;
    }
    m.g = g;
    return g;
  }
  m.g = approach(m.g, target, step);
  return m.g;
}

// acc += src * gain, gain ramping linearly from g0 to g1 across the block.
void mixRamp(int32_t *acc, const int16_t *src, size_t n, float g0, float g1) {
  const float dg = (g1 - g0) / (float)BLOCK;
  float g = g0;
  for (size_t i = 0; i < n; i++) {
    acc[i] += (int32_t)((float)src[i] * g);
    g += dg;
  }
}

// Unity below -2.5 dBFS, then a smooth knee that approaches (never reaches) full scale.
inline int16_t softClip(int32_t x) {
  constexpr int32_t T = 24576;
  constexpr float K = 32767.0f - (float)T;
  if (x > T) {
    const float d = (float)(x - T);
    return (int16_t)(T + (int32_t)(d * K / (K + d)));
  }
  if (x < -T) {
    const float d = (float)(-x - T);
    return (int16_t)(-T - (int32_t)(d * K / (K + d)));
  }
  return (int16_t)x;
}

void mixerTask(void *) {
  static int32_t acc[BLOCK];
  static int16_t tmp[BLOCK];
  static float tbuf[BLOCK];
  static int16_t out[BLOCK * 2];
  MixStream ms, vs;
  float duck = 1.0f, lvl = 0.0f;

  for (;;) {
    Cmd c;
    while (xQueueReceive(s_cmdQ, &c, 0) == pdTRUE) handleCmd(c);

    // ---- snapshot + stream state transitions ----
    float mTarget, vTarget;
    bool vActive, vEof;
    uint32_t vEpoch;
    portENTER_CRITICAL(&s_mux);
    transition(s_music, ms, MUSIC_FADE_BLOCKS);
    transition(s_voice, vs, VOICE_FADE_BLOCKS);
    mTarget = s_music.gain;
    vTarget = s_voice.gain;
    vActive = s_voice.active;
    vEof = s_voice.eof;
    vEpoch = s_voice.epoch;
    portEXIT_CRITICAL(&s_mux);

    memset(acc, 0, sizeof(acc));

    // ---- music bed (ducked while a voice line is active) ----
    const float duck0 = duck;
    duck = approach(duck, vActive ? DUCK_GAIN : 1.0f, DUCK_STEP);
    if (ms.st != ST_IDLE) {
      const size_t got = xStreamBufferReceive(s_music.sb, tmp, sizeof(tmp), 0) / 2;
      const bool playing = (ms.st == ST_PLAY);
      const float g0 = ms.g;
      const float g1 = nextGain(ms, mTarget, MUSIC_RAMP_STEP);
      mixRamp(acc, tmp, got, g0 * duck0, g1 * duck);
      if (playing && got < (size_t)BLOCK) s_musicUnderruns++;
    }

    // ---- voice line ----
    int32_t peak = 0;
    if (vs.st != ST_IDLE) {
      const size_t got = xStreamBufferReceive(s_voice.sb, tmp, sizeof(tmp), 0) / 2;
      for (size_t i = 0; i < got; i++) {
        const int32_t a = tmp[i] < 0 ? -(int32_t)tmp[i] : tmp[i];
        if (a > peak) peak = a;
      }
      const bool playing = (vs.st == ST_PLAY);
      const float g0 = vs.g;
      const float g1 = nextGain(vs, vTarget, VOICE_RAMP_STEP);
      mixRamp(acc, tmp, got, g0, g1);
      if (playing) {
        bool done = false;
        portENTER_CRITICAL(&s_mux);
        if (s_voice.epoch == vs.epoch) {
          s_voice.consumedFrames += (uint32_t)got;
          // eof was already set before this receive, so a short block means drained.
          if (vEof && vEpoch == vs.epoch && got < (size_t)BLOCK) {
            s_voice.active = false;
            s_voice.on = false;
            done = true;
          }
        }
        portEXIT_CRITICAL(&s_mux);
        if (done) {
          vs.st = ST_IDLE;
          vs.g = 0.0f;
        } else if (got < (size_t)BLOCK && !vEof) {
          s_voiceUnderruns++;
        }
      }
    }
    // Short-term envelope for mouth/glow pulsing: fast attack, ~40 ms release.
    {
      const float target = peak >= 24576 ? 1.0f : (float)peak / 24576.0f;
      lvl += (target - lvl) * (target > lvl ? 0.6f : 0.12f);
      s_level.store((uint8_t)(lvl * 255.0f + 0.5f), std::memory_order_relaxed);
    }

    // Publish "done reading" for streams that went idle (after their last receive).
    portENTER_CRITICAL(&s_mux);
    s_music.mixerReading = (ms.st != ST_IDLE);
    s_voice.mixerReading = (vs.st != ST_IDLE);
    portEXIT_CRITICAL(&s_mux);

    // ---- preloaded sound effects ----
    for (auto &s : s_sfx) {
      if (!s.active) continue;
      const float g1 = s.fading ? 0.0f : s.gain;
      const float dg = (g1 - s.gain) / (float)BLOCK;
      float g = s.gain;
      for (int i = 0; i < BLOCK; i++) {
        if (s.pos >= s.frames) {
          s.active = false;
          break;
        }
        acc[i] += (int32_t)((float)s.pcm[s.pos++] * g);
        g += dg;
      }
      if (s.fading) s.active = false;
    }

    // ---- generated chimes / tones ----
    for (auto &t : s_tone) {
      if (!t.active) continue;
      renderTone(t, tbuf);
      const float k = t.gain * 32767.0f;
      for (int i = 0; i < BLOCK; i++) acc[i] += (int32_t)(tbuf[i] * k);
    }

    // ---- out: soft clip, mono -> both speakers ----
    for (int i = 0; i < BLOCK; i++) {
      const int16_t v = softClip(acc[i]);
      out[2 * i] = v;
      out[2 * i + 1] = v;
    }
    const size_t w = s_i2s.write((const uint8_t *)out, sizeof(out));   // blocks on the DMA = pacing
    if (w != sizeof(out)) {
      s_i2sErrors++;
      vTaskDelay(pdMS_TO_TICKS(5));   // keep a 5 ms cadence even if I2S is unhappy
    }
  }
}

void buildSineTable() {
  for (int i = 0; i <= 1024; i++) s_sinTab[i] = (int16_t)lrintf(32767.0f * sinf(6.2831853f * (float)i / 1024.0f));
  s_sinTab[1024] = s_sinTab[0];
}

void releaseEngine() {
  if (s_music.sb) { vStreamBufferDeleteWithCaps(s_music.sb); s_music.sb = nullptr; }
  if (s_voice.sb) { vStreamBufferDeleteWithCaps(s_voice.sb); s_voice.sb = nullptr; }
  if (s_cmdQ) { vQueueDelete(s_cmdQ); s_cmdQ = nullptr; }
  if (s_bounce) { heap_caps_free(s_bounce); s_bounce = nullptr; }
}

}  // namespace

// ============================================================================
//  lifecycle
// ============================================================================
bool begin(uint8_t volumePct) {
  if (volumePct > 100) volumePct = 100;
  if (s_begun) {   // second call: just apply the volume
    setVolume(volumePct);
    return s_ok;
  }
  s_begun = true;
  s_volume = volumePct;
  buildSineTable();

  board::ampEnable(false);   // speakers off while the codec comes up (no pop)

  // I2S first: the ES8389 is clocked from MCLK and must see it before it is programmed.
  s_i2s.setPins(board::PIN_I2S_BCLK, board::PIN_I2S_WS, board::PIN_I2S_DOUT, -1, board::PIN_I2S_MCLK);
  if (!s_i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    ALOG("I2S start FAILED - sound disabled");
    vlog::event("audio: I2S start failed");
    return false;
  }
  s_i2s.setTimeout(I2S_WRITE_TIMEOUT_MS);
  delay(2);   // MCLK = 12.288 MHz is running

  bool present = false;
  for (int i = 0; i < 3 && !present; i++) {
    if (i) delay(5);
    present = board::i2cProbe(board::ADDR_ES8389);
  }
  if (!present) {
    ALOG("ES8389 (0x10) not answering - sound disabled");
    vlog::event("audio: ES8389 codec not found");
    s_i2s.end();
    return false;
  }
  uint8_t id1 = 0, id2 = 0;
  codecRead(0xFD, id1);   // logged only (chip ID values unverified, see RESEARCH-NOTES U8)
  codecRead(0xFE, id2);

  int fails = codecInit();
  if (fails) {
    ALOG("ES8389 init: %d register writes failed, retrying", fails);
    delay(10);
    fails = codecInit();
  }
  if (fails) {
    ALOG("ES8389 init FAILED (%d) - sound disabled", fails);
    vlog::event("audio: ES8389 init failed (%d)", fails);
    s_i2s.end();
    return false;
  }
  dacMute(false);            // reg 0x40 bits[1:0] = 0
  applyVolume(s_volume);

  // Engine buffers: streamed audio in PSRAM, the SD read buffer in internal DMA RAM.
  s_bounce = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  s_music.sb = xStreamBufferCreateWithCaps(MUSIC_SB_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_voice.sb = xStreamBufferCreateWithCaps(VOICE_SB_BYTES, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_cmdQ = xQueueCreate(CMD_QUEUE_LEN, sizeof(Cmd));
  if (!s_bounce || !s_music.sb || !s_voice.sb || !s_cmdQ) {
    ALOG("out of memory for the audio engine - sound disabled");
    vlog::event("audio: engine allocation failed");
    releaseEngine();
    dacMute(true);
    s_i2s.end();
    return false;
  }

  // Streamer first (it idles until asked), then the mixer that feeds I2S.
  if (xTaskCreatePinnedToCore(streamerTask, "audio_sd", STREAMER_STACK, nullptr, STREAMER_PRIO, &s_streamTask, AUDIO_CORE) != pdPASS) {
    s_streamTask = nullptr;
  }
  if (!s_streamTask ||
      xTaskCreatePinnedToCore(mixerTask, "audio_mix", MIXER_STACK, nullptr, MIXER_PRIO, &s_mixerTask, AUDIO_CORE) != pdPASS) {
    ALOG("task create FAILED - sound disabled");
    vlog::event("audio: task create failed");
    if (s_streamTask) {
      vTaskDelete(s_streamTask);   // idle in ulTaskNotifyTake, holds nothing
      s_streamTask = nullptr;
    }
    s_mixerTask = nullptr;
    releaseEngine();
    dacMute(true);
    s_i2s.end();
    return false;
  }

  s_ok = true;
  delay(AMP_ON_DELAY_MS);    // DAC has been playing digital silence; now the amps
  board::ampEnable(true);
  ALOG("ready: ES8389 id %02X %02X, 48 kHz, volume %u%%, internal free %lu",
       id1, id2, (unsigned)s_volume, (unsigned long)board::freeInternal());
  return true;
}

bool ok() { return s_ok; }

// ============================================================================
//  master volume (codec DAC)
// ============================================================================
void setVolume(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_volume = pct;
  if (!s_ok) return;
  applyVolume(pct);
}

uint8_t volume() { return s_volume; }

// ============================================================================
//  music bed
// ============================================================================
bool music(const char *path, uint8_t gainPct) {
  if (!s_ok) return false;
  const float g = pctGain(gainPct);
  // Same bed already playing: just follow the new gain (no restart, no gap).
  if (path && s_musicPath[0] && strcmp(path, s_musicPath) == 0) {
    bool active;
    portENTER_CRITICAL(&s_mux);
    active = s_music.active;
    if (active) s_music.gain = g;
    portEXIT_CRITICAL(&s_mux);
    if (active) return true;
  }
  fs::File *fp = nullptr;
  WavInfo wi;
  if (!openWav(path, fp, wi)) {
    replaceStream(s_music, nullptr, nullptr, 0.0f);   // bad/missing bed = silence
    s_musicPath[0] = 0;
    return false;
  }
  replaceStream(s_music, fp, &wi, g);
  strlcpy(s_musicPath, path, sizeof(s_musicPath));
  return true;
}

void musicStop() {
  if (!s_ok) return;
  replaceStream(s_music, nullptr, nullptr, 0.0f);
  s_musicPath[0] = 0;
}

bool musicPlaying() {
  if (!s_ok) return false;
  bool a;
  portENTER_CRITICAL(&s_mux);
  a = s_music.active;
  portEXIT_CRITICAL(&s_mux);
  return a;
}

// ============================================================================
//  voice line
// ============================================================================
// A new line always replaces the old one. After voice(p) returns d,
// voiceBusy() == (d > 0): a missing/bad file stops the old line and plays nothing.
uint32_t voice(const char *path, uint8_t gainPct) {
  if (!s_ok) return 0;
  fs::File *fp = nullptr;
  WavInfo wi;
  if (!openWav(path, fp, wi)) {
    voiceStop();
    return 0;
  }
  const uint32_t frames = wi.dataLen / 2;
  uint32_t ms = (uint32_t)(((uint64_t)frames * 1000u + SAMPLE_RATE - 1) / SAMPLE_RATE);
  if (ms == 0) ms = 1;
  // Watchdog: a line still "busy" this long after it started is force-stopped.
  s_voiceDeadline = millis() + ms + 1500 + ms / 10;
  replaceStream(s_voice, fp, &wi, pctGain(gainPct));
  return ms;
}

void voiceStop() {
  if (!s_ok) return;
  replaceStream(s_voice, nullptr, nullptr, 0.0f);
}

bool voiceBusy() {
  if (!s_ok) return false;
  bool a;
  portENTER_CRITICAL(&s_mux);
  a = s_voice.active;
  portEXIT_CRITICAL(&s_mux);
  if (a && (int32_t)(millis() - s_voiceDeadline) > 0) {
    ALOG("voice line overran its length (SD/I2S stall?) - stopped");
    voiceStop();
    return false;
  }
  return a;
}

uint32_t voiceRemainingMs() {
  if (!voiceBusy()) return 0;
  uint32_t total, consumed;
  portENTER_CRITICAL(&s_mux);
  total = s_voice.totalFrames;
  consumed = s_voice.consumedFrames;
  portEXIT_CRITICAL(&s_mux);
  const uint32_t left = total > consumed ? total - consumed : 0;
  return (uint32_t)(((uint64_t)left * 1000u + SAMPLE_RATE - 1) / SAMPLE_RATE);
}

uint8_t voiceLevel() {
  if (!s_ok) return 0;
  return s_level.load(std::memory_order_relaxed);
}

// ============================================================================
//  sound effects (preloaded into PSRAM on the caller's task)
// ============================================================================
int sfxLoad(const char *path) {
  if (!s_ok || !path || !*path) return -1;
  const int count = s_clipCount.load();
  for (int i = 0; i < count; i++)
    if (strcmp(s_clips[i].path, path) == 0) return i;
  if (count >= MAX_CLIPS) {
    ALOG("sfx cache full (%d clips), %s not loaded", MAX_CLIPS, path);
    return -1;
  }
  if (strlen(path) >= sizeof(s_clips[0].path)) {
    reject(path, "path too long");
    return -1;
  }

  fs::File *fp = nullptr;
  WavInfo wi;
  if (!openWav(path, fp, wi)) return -1;
  uint32_t bytes = wi.dataLen;
  const bool truncated = bytes > SFX_MAX_BYTES;
  if (truncated) bytes = SFX_MAX_BYTES;

  int16_t *pcm = (int16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t *bounce = (uint8_t *)heap_caps_malloc(CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!pcm || !bounce) {
    ALOG("out of memory loading %s (%lu bytes)", path, (unsigned long)bytes);
    if (pcm) heap_caps_free(pcm);
    if (bounce) heap_caps_free(bounce);
    fp->close();
    delete fp;
    return -1;
  }
  uint32_t got = 0;
  while (got < bytes) {   // SD -> internal DMA buffer -> PSRAM
    const size_t want = (bytes - got) < CHUNK ? (bytes - got) : CHUNK;
    const size_t n = fp->read(bounce, want);
    if (n) memcpy((uint8_t *)pcm + got, bounce, n);
    got += (uint32_t)n;
    if (n != want) break;
  }
  heap_caps_free(bounce);
  fp->close();
  delete fp;

  got &= ~1u;
  if (got == 0) {
    heap_caps_free(pcm);
    reject(path, "read error");
    return -1;
  }
  const uint32_t frames = got / 2;
  if (truncated || got < bytes) {
    ALOG("%s %s at %lu ms", path, truncated ? "truncated" : "short read", (unsigned long)(frames * 1000u / SAMPLE_RATE));
    const uint32_t fade = frames < 240 ? frames : 240;   // 5 ms fade so the cut doesn't click
    for (uint32_t i = 0; i < fade; i++) {
      int16_t &s = pcm[frames - 1 - i];
      s = (int16_t)((int32_t)s * (int32_t)i / (int32_t)fade);
    }
  }
  Clip &c = s_clips[count];
  strlcpy(c.path, path, sizeof(c.path));
  c.pcm = pcm;
  c.frames = frames;
  s_clipCount.store(count + 1);   // publish after the entry is complete
  return count;
}

void sfx(int handle, uint8_t gainPct) {
  if (!s_ok || handle < 0 || handle >= s_clipCount.load()) return;
  Cmd c{};
  c.type = CMD_SFX;
  c.pcm = s_clips[handle].pcm;
  c.frames = s_clips[handle].frames;
  c.gain = pctGain(gainPct);
  xQueueSend(s_cmdQ, &c, 0);   // queue full -> dropped (never blocks the show)
}

void sfxStopAll() {
  if (!s_ok) return;
  Cmd c{};
  c.type = CMD_SFX_STOP;
  xQueueSend(s_cmdQ, &c, 0);
}

// ============================================================================
//  generated tones
// ============================================================================
void chime(Chime ch, uint8_t gainPct) {
  if (!s_ok || (int)ch >= NUM_CHIMES) return;
  Cmd c{};
  c.type = CMD_CHIME;
  c.chime = (uint8_t)ch;
  c.gain = pctGain(gainPct);
  xQueueSend(s_cmdQ, &c, 0);
}

void tone(uint16_t hz, uint16_t ms, uint8_t gainPct) {
  if (!s_ok || hz == 0 || ms == 0) return;
  if (hz < 20) hz = 20;
  if (hz > 12000) hz = 12000;
  if (ms < 10) ms = 10;
  if (ms > 10000) ms = 10000;
  Cmd c{};
  c.type = CMD_TONE;
  c.gain = pctGain(gainPct);
  const uint16_t rel = ms / 4 < 20 ? ms / 4 : 20;
  c.note = NoteSpec{0, ms, hz, 0, 255, T_SINE, 5, 0, rel, 0, 0};
  xQueueSend(s_cmdQ, &c, 0);
}

}  // namespace audio
