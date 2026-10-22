#pragma once
// ============================================================================
//  media.h — TF card (SD_MMC 1-bit, 40 MHz) + visual assets.
//
//  SD layout (everything under /oracle, built by tools/build_assets.py):
//    /oracle/config.ini              station settings (settings.h)
//    /oracle/captions.ini            on-screen words, rank/tier/bowl names, colours
//    /oracle/clips/<id>[_<n>].omj    animated visual (OMJ1 = length-prefixed MJPEG, 800x480)
//    /oracle/clips/<id>[_<n>].jpg    still visual (800x480 baseline JPEG)
//    /oracle/voice/<group>_<n>.wav   voice lines (48 kHz mono s16), n = 1..9 variants
//    /oracle/sfx/<id>.wav            sound effects (preloaded)
//    /oracle/music/<id>.wav          music beds (streamed, looped)
//    /oracle/state/...               counters (written by the firmware)
//    /oracle/logs/...                visit + event logs (written by the firmware)
//
//  OMJ1 clip file (little-endian):
//    header 20 B: "OMJ1" u16 width, u16 height, u16 fps*100, u16 reserved,
//                 u32 frameCount, u32 maxFrameBytes
//    frames:      u32 len, len bytes of baseline JPEG (4:2:0), zero pad to 4 B
//  Frames are independent, so late frames are skipped by seeking.
//
//  Reads go through an internal-RAM DMA-capable buffer (the S3 SDMMC host cannot
//  DMA into PSRAM); keep single reads <= 32 KB so the audio streamer is never
//  starved of the FatFs volume lock for long.
// ============================================================================
#include <Arduino.h>

namespace media {

bool mount();                   // EXIO4 high, SD_MMC.setPins(12,11,13), begin(1-bit, 40 MHz, fallback 20 MHz)
bool sdOk();
uint32_t sdSizeMB();
bool exists(const char *path);
bool ensureDir(const char *path);

enum class Kind : uint8_t { None = 0, Clip, Still };
struct Visual {
  Kind kind = Kind::None;
  char path[80] = {0};
};
// Looks under /oracle/clips for, in order: <id>_<variant>.omj, <id>_<variant>.jpg,
// <id>.omj, <id>.jpg  (variant 0 = skip the variant forms). Results are cached
// (a directory listing is read once at mount) so lookups are cheap per frame.
Visual findVisual(const char *id, uint8_t variant = 0);
// How many consecutive variants <dirRel>/<id>_1.<ext> .. _9.<ext> exist (0 if none).
// dirRel is relative to /oracle (e.g. "voice", "clips"). Uses the same cache.
uint8_t countVariants(const char *dirRel, const char *id, const char *ext);
bool rescan();                  // re-read the directory caches (service menu); false = no card / the
                                // mounted card stopped answering (pulled + re-inserted: reboot)
uint16_t assetCount();          // files found under /oracle/{clips,voice,sfx,music}

class Clip {
 public:
  Clip() = default;
  Clip(const Clip &) = delete;              // owns a file + a heap buffer: never copy
  Clip &operator=(const Clip &) = delete;
  bool open(const char *path);  // header check + allocate a frame buffer sized from maxFrameBytes
  void close();
  bool isOpen() const { return _open; }
  uint32_t frames() const { return _frames; }
  float fps() const { return _fps100 / 100.0f; }
  uint32_t frameMs() const { return _fps100 ? (100000u / _fps100) : 77; }
  uint32_t durationMs() const { return _fps100 ? (uint32_t)((uint64_t)_frames * 100000u / _fps100) : 0; }
  // Decode the frame due at `elapsedMs` into fb (skipping frames that are already
  // late). With loop=true the clip wraps; with loop=false it holds the last frame
  // once elapsedMs >= durationMs(). Returns false only on read/decode failure.
  bool render(uint32_t elapsedMs, bool loop, uint16_t *fb);
  ~Clip() { close(); }
 private:
  bool _open = false;
  void *_file = nullptr;        // fs::File* (kept opaque here)
  uint8_t *_buf = nullptr;
  uint32_t _cap = 0, _frames = 0, _maxBytes = 0, _next = 0;   // _next = index of the next frame in the file
  uint32_t _dataStart = 20;
  uint16_t _fps100 = 0, _w = 0, _h = 0;
  int32_t _lastShown = -1;
};

// Decode an 800x480 baseline JPEG into fb. Decoded stills are cached in PSRAM
// (LRU, up to STILL_CACHE entries of 768 KB) so repeated frames are a memcpy.
constexpr int STILL_CACHE = 5;
bool drawStill(const char *path, uint16_t *fb);
void flushStillCache();

// Decode any baseline JPEG (e.g. a small sprite/icon) at (x,y) into fb, clipped.
bool drawJpegAt(const char *path, uint16_t *fb, int16_t x, int16_t y);

// Service-menu helper: sequential read throughput of a file, MB/s (0 on error).
float benchmarkReadMBps(const char *path, uint32_t maxBytes = 4u << 20);

}  // namespace media
