/*
 * ============================================================================
 *  media.cpp — TF card (SD_MMC 1-bit) + visual assets for the Crystal Oracle
 * ----------------------------------------------------------------------------
 *  Implements media.h:
 *    * mount(): EXIO4 (SD DAT3) high, SD_MMC 1-bit on CLK 12 / CMD 11 / D0 13
 *      at 40 MHz (retried at 20 MHz), mount point /sdcard, 10 open files.
 *    * Name cache: /oracle/{clips,voice,sfx,music} are listed ONCE at mount
 *      (and on rescan()) into a sorted, lower-case name table in PSRAM, so
 *      findVisual() / countVariants() / assetCount() — and exists() for paths
 *      in those four folders — never touch the card while the show runs.
 *    * Clip: OMJ1 player (length-prefixed MJPEG, see media.h). Frames decode
 *      with JPEGDEC 1.8.4 in framebuffer mode straight into the back buffer.
 *    * Stills: 800x480 JPEGs decoded once into a 5-slot LRU cache in PSRAM and
 *      memcpy'd per frame. Sprites (drawJpegAt): callback decode with clipping.
 *
 *  SD read path (the part that matters for frame rate):
 *    The S3's SDMMC host cannot DMA into PSRAM. A read that lands in PSRAM makes
 *    the IDF driver fall back to ONE 512-byte sector per card command through
 *    its own bounce buffer — several times slower. So every read here lands in
 *    INTERNAL DMA-capable RAM: the clip's own frame buffer when it could be
 *    allocated internally, otherwise a shared internal bounce buffer (32 KB)
 *    whose contents are then copied to PSRAM.
 *    Files are read with the VFS's POSIX calls (open/read/lseek on "/sdcard/..")
 *    rather than fs::File: an Arduino File is a stdio FILE with a 4 KB buffer,
 *    which chops every frame into 4 KB card commands plus an extra copy. With
 *    read() FatFs moves whole sectors straight into our buffer (multi-block DMA).
 *    Each read() call is <= 32 KB and ends on a 512-byte file offset, so the
 *    FatFs volume lock is never held long: the audio streamer on core 0 reads
 *    its WAVs concurrently through its own files (safe — separate file objects).
 *
 *  Threading: Clip, drawStill, drawJpegAt, flushStillCache and the benchmark
 *  run on the Arduino loop task only (they share one JPEGDEC object, the still
 *  cache and the bounce buffer). The name cache is mutex-protected, so exists(),
 *  findVisual() and countVariants() are safe from any task.
 *  Nothing here aborts or waits forever: failures are logged (once per path for
 *  anything that could otherwise repeat every frame) and return false / None.
 * ============================================================================
 */
#include "media.h"
#include "board.h"
#include <FS.h>
#include <SD_MMC.h>
#include <JPEGDEC.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include <ctype.h>
#include <algorithm>
#include <new>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace media {

namespace {

// ---------------------------------------------------------------- constants
constexpr const char *MOUNT = "/sdcard";         // SD_MMC mount point (VFS prefix for POSIX calls)
constexpr uint8_t  MAX_OPEN_FILES = 10;
constexpr int      SCR_W = 800, SCR_H = 480;
constexpr uint32_t FRAME_BYTES = (uint32_t)SCR_W * SCR_H * 2;   // 768,000 B per RGB565 frame
constexpr uint32_t SECTOR = 512;
constexpr uint32_t READ_CHUNK = 32u * 1024;       // max bytes per read() call (FatFs lock hold time)
constexpr uint32_t MAX_CLIP_FRAME = 160u * 1024;  // reject clips whose maxFrameBytes is larger
constexpr uint32_t MIN_CLIP_BUF = 64u * 1024;     // clip buffers are at least this big (reusable)
// Internal DMA RAM a clip buffer must leave free: SD single-sector bounces, sfx
// loads, settings I/O and Wi-Fi/ESP-NOW RX all allocate there. Below it the
// buffer goes to PSRAM (reads bounce through s_bounce: one memcpy per frame).
constexpr uint32_t CLIP_INT_RESERVE = 48u * 1024;
constexpr uint32_t CLIP_INT_CAPS = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t MAX_STILL_FILE = 2u << 20;     // a 800x480 JPEG is ~100-300 KB; 2 MB = clearly wrong
constexpr uint32_t MAX_SPRITE_FILE = 1u << 20;
constexpr uint32_t SPRITE_CACHE_FILE = 64u * 1024;  // sprites up to this size stay cached (encoded)
constexpr int      SPRITE_CACHE = 4;
constexpr int      BAD_PATHS = 8;                 // remembered failing paths (no retry, no log spam)
constexpr size_t   PATH_CAP = 80;                 // == sizeof(Visual::path)
constexpr size_t   VFS_PATH_CAP = 112;
constexpr size_t   NAME_MAX_LEN = 63;             // longest cached file name
constexpr size_t   ID_MAX_LEN = 48;               // longest cue id / dir / ext accepted by lookups
constexpr uint32_t MAX_DIR_ENTRIES = 4000;        // per folder (sanity limit)
constexpr TickType_t CACHE_LOCK_WAIT = pdMS_TO_TICKS(200);

enum : uint8_t { DIR_CLIPS = 0, DIR_VOICE, DIR_SFX, DIR_MUSIC, DIR_COUNT };
const char *const kDirs[DIR_COUNT] = {"clips", "voice", "sfx", "music"};

// ---------------------------------------------------------------- state
bool s_mounted = false;
uint32_t s_sdKhz = 0;

// JPEG decoder state is ~18 KB: one static instance (internal RAM), never on a
// stack. Used only from the loop task.
JPEGDEC s_jpeg;

// Shared internal DMA-capable bounce buffer for reads whose destination is PSRAM.
uint8_t *s_bounce = nullptr;
uint32_t s_bounceCap = 0;

// PSRAM scratch for whole encoded files (stills, sprites). Grows, never shrinks
// (except in flushStillCache), so it doesn't fragment PSRAM.
uint8_t *s_scratch = nullptr;
uint32_t s_scratchCap = 0;

// One spare INTERNAL clip buffer kept after Clip::close(), so the next clip reuses
// it instead of fragmenting internal RAM with a 64-160 KB malloc/free every step.
uint8_t *s_poolBuf = nullptr;
uint32_t s_poolCap = 0;

// ---- name cache (PSRAM): NUL-terminated lower-case names + per-folder sorted offsets
char *s_names = nullptr;
uint32_t s_namesLen = 0, s_namesCap = 0;
uint32_t *s_offs = nullptr;
uint32_t s_offsN = 0, s_offsCap = 0;
uint32_t s_dirFirst[DIR_COUNT] = {0, 0, 0, 0};
uint32_t s_dirN[DIR_COUNT] = {0, 0, 0, 0};
StaticSemaphore_t s_cacheMtxBuf;
SemaphoreHandle_t s_cacheMtx = nullptr;

// ---- still cache (decoded 800x480 frames in PSRAM, LRU)
struct StillSlot {
  char path[PATH_CAP];
  uint16_t *px;
  uint32_t used;
  bool valid;
};
StillSlot s_still[STILL_CACHE] = {};
uint32_t s_stillTick = 0;

// ---- sprite cache (encoded bytes in PSRAM, LRU)
struct SpriteSlot {
  char path[PATH_CAP];
  uint8_t *data;
  uint32_t len;
  uint32_t used;
};
SpriteSlot s_sprite[SPRITE_CACHE] = {};
uint32_t s_spriteTick = 0;

// ---- paths that failed (unreadable / not decodable): skipped until rescan/flush
char s_bad[BAD_PATHS][PATH_CAP] = {};
uint8_t s_badNext = 0;

uint32_t s_lastClipErrMs = 0;

// ============================================================================
//  small helpers
// ============================================================================
inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline uint32_t roundUp(uint32_t v, uint32_t a) { return (v + a - 1) / a * a; }

// Lower-case copy; false if src is empty or doesn't fit.
bool lowerCopy(char *dst, size_t cap, const char *src) {
  if (!src || !*src) return false;
  size_t i = 0;
  for (; src[i]; i++) {
    if (i + 1 >= cap) return false;
    dst[i] = (char)tolower((unsigned char)src[i]);
  }
  dst[i] = 0;
  return true;
}

// "/oracle/x.omj" -> "/sdcard/oracle/x.omj" (a path that already carries the
// mount prefix is used as is).
bool vfsPath(char *out, size_t cap, const char *path) {
  if (!path || path[0] != '/') return false;
  const size_t m = strlen(MOUNT);
  const bool prefixed = strncmp(path, MOUNT, m) == 0 && (path[m] == '/' || path[m] == 0);
  const int n = prefixed ? snprintf(out, cap, "%s", path) : snprintf(out, cap, "%s%s", MOUNT, path);
  return n > 0 && (size_t)n < cap;
}

// Grow a PSRAM array to hold at least `need` elements (keeps its contents).
template <typename T>
bool grow(T *&p, uint32_t &cap, uint32_t need) {
  if (need <= cap) return true;
  uint32_t nc = cap ? cap : 256;
  while (nc < need) nc *= 2;
  void *np = heap_caps_realloc(p, (size_t)nc * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!np) np = heap_caps_realloc(p, (size_t)nc * sizeof(T), MALLOC_CAP_8BIT);   // no PSRAM left
  if (!np) return false;
  p = (T *)np;
  cap = nc;
  return true;
}

// ---- failing-path memory -------------------------------------------------------
bool isBad(const char *path) {
  for (auto &b : s_bad)
    if (b[0] && strcasecmp(b, path) == 0) return true;
  return false;
}
void markBad(const char *path, const char *why) {
  Serial.printf("[media] %s: %s (skipped until rescan)\n", path, why);
  if (isBad(path)) return;
  strlcpy(s_bad[s_badNext], path, PATH_CAP);
  s_badNext = (uint8_t)((s_badNext + 1) % BAD_PATHS);
}
void clearBad() {
  for (auto &b : s_bad) b[0] = 0;
  s_badNext = 0;
}

// ---- name cache lock -----------------------------------------------------------
SemaphoreHandle_t cacheMutex() {
  if (!s_cacheMtx) s_cacheMtx = xSemaphoreCreateMutexStatic(&s_cacheMtxBuf);
  return s_cacheMtx;
}
class CacheLock {
 public:
  CacheLock() : _m(cacheMutex()), _ok(_m && xSemaphoreTake(_m, CACHE_LOCK_WAIT) == pdTRUE) {}
  ~CacheLock() {
    if (_ok) xSemaphoreGive(_m);
  }
  bool ok() const { return _ok; }

 private:
  SemaphoreHandle_t _m;
  bool _ok;
};

// Binary search in one folder's sorted range. Caller holds the cache lock.
bool hasName(uint8_t dir, const char *lowerName) {
  if (dir >= DIR_COUNT || !s_names || !s_offs) return false;
  uint32_t lo = s_dirFirst[dir], hi = lo + s_dirN[dir];
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    const int c = strcmp(s_names + s_offs[mid], lowerName);
    if (c == 0) return true;
    if (c < 0) lo = mid + 1;
    else hi = mid;
  }
  return false;
}

// "clips", "/oracle/clips", "oracle/clips/" ... -> DIR_x, or -1 for other folders.
int dirIndex(const char *dirRel) {
  if (!dirRel) return -1;
  while (*dirRel == '/') dirRel++;
  if (strncasecmp(dirRel, "oracle/", 7) == 0) dirRel += 7;
  size_t n = strlen(dirRel);
  while (n && dirRel[n - 1] == '/') n--;
  for (int d = 0; d < DIR_COUNT; d++)
    if (strlen(kDirs[d]) == n && strncasecmp(dirRel, kDirs[d], n) == 0) return d;
  return -1;
}

// "/oracle/voice/greet_r1_2.wav" -> (DIR_VOICE, "greet_r1_2.wav") when the path is
// a plain file directly inside one of the cached folders.
bool cachedDirOf(const char *path, int &dir, const char *&name) {
  if (strncasecmp(path, "/oracle/", 8) != 0) return false;
  const char *d = path + 8;
  const char *slash = strchr(d, '/');
  if (!slash || strchr(slash + 1, '/')) return false;
  for (int i = 0; i < DIR_COUNT; i++) {
    const size_t n = strlen(kDirs[i]);
    if ((size_t)(slash - d) == n && strncasecmp(d, kDirs[i], n) == 0) {
      dir = i;
      name = slash + 1;
      return *name != 0;
    }
  }
  return false;
}

void buildCache() {
  CacheLock lk;
  if (!lk.ok()) {
    Serial.println("[media] name cache busy - not rebuilt");
    return;
  }
  s_namesLen = 0;
  s_offsN = 0;
  for (int d = 0; d < DIR_COUNT; d++) s_dirFirst[d] = s_dirN[d] = 0;
  if (!s_mounted) return;

  bool oom = false;
  for (uint8_t d = 0; d < DIR_COUNT && !oom; d++) {
    s_dirFirst[d] = s_offsN;
    char full[48];
    snprintf(full, sizeof(full), "%s/oracle/%s", MOUNT, kDirs[d]);
    DIR *dir = opendir(full);
    if (!dir) continue;   // folder missing: no assets of that kind (built-in fallbacks)
    uint32_t seen = 0;
    struct dirent *e;
    while ((e = readdir(dir)) != nullptr && seen < MAX_DIR_ENTRIES) {
      if (e->d_type == DT_DIR) continue;
      const char *nm = e->d_name;
      if (nm[0] == '.') continue;   // ".", "..", and macOS "._name" AppleDouble files
      const size_t len = strlen(nm);
      if (len == 0) continue;
      if (len > NAME_MAX_LEN) {
        Serial.printf("[media] name too long, ignored: /oracle/%s/%s\n", kDirs[d], nm);
        continue;
      }
      if (!grow(s_names, s_namesCap, s_namesLen + (uint32_t)len + 1) || !grow(s_offs, s_offsCap, s_offsN + 1)) {
        Serial.println("[media] out of memory while listing assets - list truncated");
        oom = true;
        break;
      }
      s_offs[s_offsN++] = s_namesLen;
      for (size_t i = 0; i <= len; i++) s_names[s_namesLen + i] = (char)tolower((unsigned char)nm[i]);
      s_namesLen += (uint32_t)len + 1;
      seen++;
    }
    closedir(dir);
    s_dirN[d] = s_offsN - s_dirFirst[d];
    std::sort(s_offs + s_dirFirst[d], s_offs + s_offsN,
              [](uint32_t a, uint32_t b) { return strcmp(s_names + a, s_names + b) < 0; });
  }
  Serial.printf("[media] assets: %lu clips/stills, %lu voice, %lu sfx, %lu music\n",
                (unsigned long)s_dirN[DIR_CLIPS], (unsigned long)s_dirN[DIR_VOICE],
                (unsigned long)s_dirN[DIR_SFX], (unsigned long)s_dirN[DIR_MUSIC]);
}

// ============================================================================
//  reading
// ============================================================================
void ensureBounce() {
  if (s_bounce) return;
  static const uint32_t sizes[] = {32768, 16384, 8192, 4096};
  for (uint32_t sz : sizes) {
    s_bounce = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (s_bounce) {
      s_bounceCap = sz;
      return;
    }
  }
  Serial.println("[media] no internal RAM for the SD bounce buffer - stills/sprites disabled");
}

// Reads up to n bytes at the file's current offset `pos` (tracked by the caller)
// into dst. A destination in internal DMA-capable RAM is filled directly by the
// SDMMC DMA; anything else (PSRAM) goes through the internal bounce buffer and is
// copied. Every read() is <= 32 KB and, where possible, ends on a 512-byte file
// offset (so the next one starts sector-aligned and FatFs can DMA whole sectors).
// Returns the bytes read (< n only at end of file), or -1 on an I/O error.
int32_t readAt(int fd, uint32_t &pos, uint8_t *dst, uint32_t n) {
  if (fd < 0 || !dst) return -1;
  const bool direct = esp_ptr_dma_capable(dst) && (((uintptr_t)dst & 3) == 0);
  if (!direct && !s_bounce) return -1;
  const uint32_t lim = direct ? READ_CHUNK : s_bounceCap;
  uint32_t done = 0;
  while (done < n) {
    uint32_t chunk = lim - (pos % SECTOR);
    if (chunk > n - done) chunk = n - done;
    uint8_t *to = direct ? dst + done : s_bounce;
    const ssize_t got = ::read(fd, to, chunk);
    if (got < 0) return -1;
    if (got == 0) break;
    if (!direct) memcpy(dst + done, s_bounce, (size_t)got);
    done += (uint32_t)got;
    pos += (uint32_t)got;
    if ((uint32_t)got < chunk) break;   // end of file
  }
  return (int32_t)done;
}

bool ensureScratch(uint32_t need) {
  if (need <= s_scratchCap && s_scratch) return true;
  const uint32_t cap = roundUp(need, 64u * 1024);
  if (s_scratch) heap_caps_free(s_scratch);
  s_scratch = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!s_scratch) s_scratch = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_8BIT);
  s_scratchCap = s_scratch ? cap : 0;
  return s_scratch != nullptr;
}

// Reads a whole file into the PSRAM scratch buffer. Returns its length (0 = failed;
// `why` says what went wrong).
uint32_t readWholeFile(const char *path, uint32_t maxLen, const char *&why) {
  why = "not found";
  char full[VFS_PATH_CAP];
  if (!s_mounted || !vfsPath(full, sizeof(full), path)) return 0;
  const int fd = ::open(full, O_RDONLY);
  if (fd < 0) return 0;
  struct stat st;
  uint32_t len = 0;
  if (fstat(fd, &st) == 0 && st.st_size > 0) len = (uint32_t)st.st_size;
  uint32_t got = 0;
  if (!len) why = "empty file";
  else if (len > maxLen) why = "file too large";
  else if (!ensureScratch(len)) why = "out of memory";
  else {
    uint32_t pos = 0;
    const int32_t r = readAt(fd, pos, s_scratch, len);
    if (r == (int32_t)len) got = len;
    else why = "read error";
  }
  ::close(fd);
  return got;
}

// ============================================================================
//  decoding (JPEGDEC 1.8.4)
// ============================================================================
int jpegNoDraw(JPEGDRAW *) { return 1; }   // framebuffer mode never calls it

struct BlitCtx {
  uint16_t *fb;
};

// Callback mode: copy each decoded block into the 800x480 frame, clipped.
// The block's row pitch is iWidth; only iWidthUsed pixels per row are valid.
int jpegBlit(JPEGDRAW *d) {
  const BlitCtx *c = (const BlitCtx *)d->pUser;
  if (!c || !c->fb || !d->pPixels) return 0;
  if (d->y >= SCR_H) return 0;   // everything from here on is below the screen: stop early
  int x = d->x, w = d->iWidthUsed, sx = 0;
  if (x < 0) {
    sx = -x;
    w += x;
    x = 0;
  }
  if (x + w > SCR_W) w = SCR_W - x;
  if (w <= 0) return 1;
  for (int r = 0; r < d->iHeight; r++) {
    const int y = d->y + r;
    if (y < 0) continue;
    if (y >= SCR_H) break;
    memcpy(c->fb + (size_t)y * SCR_W + x, d->pPixels + (size_t)r * d->iWidth + sx, (size_t)w * 2);
  }
  return 1;
}

// Any baseline JPEG placed at (x, y), clipped to the screen.
bool decodeAt(uint8_t *jpg, uint32_t len, uint16_t *fb, int x, int y) {
  if (!jpg || !fb || !len) return false;
  if (!s_jpeg.openRAM(jpg, (int)len, jpegBlit)) return false;
  if (s_jpeg.getJPEGType() != JPEG_MODE_BASELINE) {   // progressive would come out at 1/8 size
    s_jpeg.close();
    return false;
  }
  const int w = s_jpeg.getWidth(), h = s_jpeg.getHeight();
  if (x >= SCR_W || y >= SCR_H || x + w <= 0 || y + h <= 0) {   // entirely off screen
    s_jpeg.close();
    return true;
  }
  BlitCtx ctx = {fb};
  s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
  s_jpeg.setUserPointer(&ctx);
  const int ok = s_jpeg.decode(x, y, 0);
  s_jpeg.close();
  return ok == 1;
}

// A full-screen 800x480 baseline JPEG straight into fb (framebuffer mode: no
// callback, no copy; the S3 SIMD colour conversion is used automatically).
// `err` (optional) receives a short reason on failure.
bool decodeFull(uint8_t *jpg, uint32_t len, uint16_t *fb, const char **err = nullptr) {
  const char *dummy;
  const char *&why = err ? *err : dummy;
  why = "bad JPEG";
  if (!jpg || !fb || !len) return false;
  if (!s_jpeg.openRAM(jpg, (int)len, jpegNoDraw)) return false;
  if (s_jpeg.getWidth() != SCR_W || s_jpeg.getHeight() != SCR_H) {
    why = "not 800x480";
    s_jpeg.close();
    return false;
  }
  if (s_jpeg.getJPEGType() != JPEG_MODE_BASELINE) {
    why = "progressive JPEG (needs baseline)";
    s_jpeg.close();
    return false;
  }
  if (((uintptr_t)fb & 15) != 0) {
    // The SIMD colour conversion stores 16-byte vectors and ignores the low four
    // address bits, so a misaligned buffer would be garbled: use the (slower)
    // callback path, which copies rows with memcpy. Display buffers are aligned.
    s_jpeg.close();
    if (decodeAt(jpg, len, fb, 0, 0)) return true;
    why = "decode error";
    return false;
  }
  s_jpeg.setPixelType(RGB565_LITTLE_ENDIAN);   // panel byte order
  s_jpeg.setFramebuffer(fb);                   // AFTER openRAM (openRAM clears the state)
  const int ok = s_jpeg.decode(0, 0, 0);
  s_jpeg.close();
  if (ok != 1) why = "decode error";
  return ok == 1;
}

// ============================================================================
//  clip helpers
// ============================================================================
struct ClipFile {
  int fd;
  uint32_t pos;   // current file offset (kept in step with read/lseek)
};

uint8_t *takeClipBuffer(uint32_t need, uint32_t &cap) {
  if (s_poolBuf && s_poolCap >= need) {
    uint8_t *p = s_poolBuf;
    cap = s_poolCap;
    s_poolBuf = nullptr;
    s_poolCap = 0;
    return p;
  }
  if (s_poolBuf) {   // too small for this clip: give the memory back first
    heap_caps_free(s_poolBuf);
    s_poolBuf = nullptr;
    s_poolCap = 0;
  }
  const uint32_t exact = roundUp(need, SECTOR);
  const uint32_t want = std::max(exact, MIN_CLIP_BUF);
  const size_t freeInt = heap_caps_get_free_size(CLIP_INT_CAPS);
  const size_t largest = heap_caps_get_largest_free_block(CLIP_INT_CAPS);
  static bool s_marginLogged = false;
  if (!s_marginLogged) {   // first clip: the real internal-RAM margin (to tune CLIP_INT_RESERVE)
    s_marginLogged = true;
    Serial.printf("[media] first clip: internal DMA RAM %lu B free, largest block %lu B, frame buffer %lu B\n",
                  (unsigned long)freeInt, (unsigned long)largest, (unsigned long)exact);
  }
  uint8_t *p = nullptr;
  cap = 0;
  if (freeInt >= (size_t)want + CLIP_INT_RESERVE && largest >= want) {
    p = (uint8_t *)heap_caps_malloc(want, CLIP_INT_CAPS);
    cap = want;
  }
  if (!p && exact < want && freeInt >= (size_t)exact + CLIP_INT_RESERVE && largest >= exact) {
    p = (uint8_t *)heap_caps_malloc(exact, CLIP_INT_CAPS);
    cap = exact;
  }
  if (!p) {
    // Internal RAM is short: PSRAM works (reads then go through the bounce buffer),
    // just slower to fill and to decode from.
    p = (uint8_t *)heap_caps_malloc(want, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    cap = want;
    static bool s_psramLogged = false;
    if (p && !s_psramLogged) {
      s_psramLogged = true;
      Serial.printf("[media] clip buffers in PSRAM (internal DMA RAM %lu B free, keeping %lu B for SD/audio/ESP-NOW)\n",
                    (unsigned long)freeInt, (unsigned long)CLIP_INT_RESERVE);
    }
  }
  if (!p) cap = 0;
  return p;
}

void giveClipBuffer(uint8_t *p, uint32_t cap) {
  if (!p) return;
  // Keep one internal buffer for the next clip, unless internal RAM is short without it.
  if (!s_poolBuf && esp_ptr_internal(p) && heap_caps_get_free_size(CLIP_INT_CAPS) >= CLIP_INT_RESERVE) {
    s_poolBuf = p;
    s_poolCap = cap;
    return;
  }
  heap_caps_free(p);
}

// Moves the clip's file to frame `target` and reads that frame into buf as
// [u32 len][JPEG bytes][pad]. Frames in between are skipped by reading only their
// u32 length and seeking past them (MJPEG frames are independent).
// Returns 1 = ok, 0 = the file ended before `target` (frames was corrected to the
// real count; the caller retries), -1 = read error / corrupt clip.
int loadFrame(ClipFile &f, uint8_t *buf, uint32_t cap, uint32_t maxBytes, uint32_t dataStart,
              uint32_t &next, uint32_t &frames, uint32_t target) {
  if (target < next) {   // wrap (or time went backwards): back to the first frame
    const off_t p = lseek(f.fd, (off_t)dataStart, SEEK_SET);
    if (p < 0) return -1;
    f.pos = (uint32_t)p;
    next = 0;
  }
  while (true) {
    uint8_t lenBuf[4];
    uint8_t *lp = (next == target) ? buf : lenBuf;
    const int32_t got = readAt(f.fd, f.pos, lp, 4);
    if (got == 0 && next > 0) {   // clean end of file: the header over-counted
      frames = next;
      return 0;
    }
    if (got != 4) return -1;
    const uint32_t len = rd32(lp);
    if (len == 0 || len > maxBytes) return -1;
    const uint32_t padded = (len + 3) & ~3u;
    if (next == target) {
      if (4 + padded > cap) return -1;   // can't happen: cap is sized from maxBytes
      const int32_t r = readAt(f.fd, f.pos, buf + 4, padded);
      if (r < (int32_t)len) return -1;   // (a missing final pad is tolerated)
      next++;
      return 1;
    }
    const off_t p = lseek(f.fd, (off_t)(f.pos + padded), SEEK_SET);
    if (p < 0) return -1;
    f.pos = (uint32_t)p;
    next++;
  }
}

bool clipErrDue() {
  const uint32_t now = millis();
  if (s_lastClipErrMs && now - s_lastClipErrMs < 2000) return false;
  s_lastClipErrMs = now ? now : 1;
  return true;
}

// ---- still cache slot choice: a free slot with memory, a free slot (allocate),
// else the least recently used one. nullptr = no memory at all.
StillSlot *pickStillSlot() {
  for (auto &s : s_still)
    if (!s.valid && s.px) return &s;
  for (auto &s : s_still) {
    if (!s.valid && !s.px) {
      s.px = (uint16_t *)heap_caps_aligned_alloc(64, FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (s.px) return &s;
      break;   // PSRAM exhausted: recycle the LRU slot instead
    }
  }
  StillSlot *lru = nullptr;
  for (auto &s : s_still)
    if (s.valid && s.px && (!lru || s.used < lru->used)) lru = &s;
  return lru;
}

SpriteSlot *findSprite(const char *path) {
  for (auto &s : s_sprite)
    if (s.data && strcasecmp(s.path, path) == 0) return &s;
  return nullptr;
}

void dropSprite(const char *path) {
  SpriteSlot *s = findSprite(path);
  if (!s) return;
  heap_caps_free(s->data);
  s->data = nullptr;
  s->len = 0;
  s->path[0] = 0;
}

// Copies the encoded bytes of a small sprite into the cache (LRU). nullptr = not cached.
SpriteSlot *cacheSprite(const char *path, const uint8_t *data, uint32_t len) {
  if (len > SPRITE_CACHE_FILE || strlen(path) >= PATH_CAP) return nullptr;
  SpriteSlot *slot = nullptr;
  for (auto &s : s_sprite)
    if (!s.data) {
      slot = &s;
      break;
    }
  if (!slot)
    for (auto &s : s_sprite)
      if (!slot || s.used < slot->used) slot = &s;
  if (slot->data) heap_caps_free(slot->data);
  slot->data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!slot->data) {
    slot->path[0] = 0;
    slot->len = 0;
    return nullptr;
  }
  memcpy(slot->data, data, len);
  slot->len = len;
  strlcpy(slot->path, path, PATH_CAP);
  slot->used = ++s_spriteTick;
  return slot;
}

}  // namespace

// ============================================================================
//  card
// ============================================================================
bool mount() {
  if (s_mounted) return true;
  cacheMutex();

  // DAT3 doubles as card-select: it must be HIGH for SD-bus mode (board::begin
  // already set it; re-assert in case anything touched the expander since).
  if (!board::exioWrite(board::EXIO_SD_D3, true))
    Serial.println("[media] warning: could not drive EXIO4 (SD DAT3) high");
  delay(5);

  const uint32_t t0 = millis();
  SD_MMC.setPins(board::PIN_SD_CLK, board::PIN_SD_CMD, board::PIN_SD_D0);
  // 20 MHz, not 40 (first hardware run, 2026-10-23): with the card at 40 MHz the panel showed torn
  // bands of stale lines even with 20-line bounce buffers; 20 MHz + 20-line buffers together cured it
  // (the SD DMA adds to the PSRAM/bus pressure the bounce ISR competes with). 1-bit @ 20 MHz (~2 MB/s)
  // still covers clips + voice.
  bool ok = SD_MMC.begin(MOUNT, true /*1-bit*/, false /*never format*/, SDMMC_FREQ_DEFAULT, MAX_OPEN_FILES);
  s_sdKhz = SDMMC_FREQ_DEFAULT;
  if (ok && SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    ok = false;
  }
  if (!ok) {
    s_sdKhz = 0;
    Serial.printf("[media] no usable TF card (%lu ms)\n", (unsigned long)(millis() - t0));
    return false;
  }
  s_mounted = true;
  ensureBounce();
  const sdcard_type_t t = SD_MMC.cardType();
  Serial.printf("[media] TF card %s, %lu MB, 1-bit @ %lu MHz, mounted in %lu ms\n",
                t == CARD_SDHC ? "SDHC/SDXC" : t == CARD_SD ? "SDSC" : t == CARD_MMC ? "MMC" : "?",
                (unsigned long)sdSizeMB(), (unsigned long)(s_sdKhz / 1000), (unsigned long)(millis() - t0));
  buildCache();
  return true;
}

bool sdOk() { return s_mounted; }

uint32_t sdSizeMB() { return s_mounted ? (uint32_t)(SD_MMC.cardSize() >> 20) : 0; }

bool exists(const char *path) {
  if (!s_mounted || !path || path[0] != '/') return false;
  int dir = -1;
  const char *name = nullptr;
  char lname[NAME_MAX_LEN + 1];
  if (cachedDirOf(path, dir, name) && name[0] != '.' && lowerCopy(lname, sizeof(lname), name)) {
    CacheLock lk;
    if (lk.ok()) return hasName((uint8_t)dir, lname);
  }
  char full[VFS_PATH_CAP];
  if (!vfsPath(full, sizeof(full), path)) return false;
  struct stat st;
  return ::stat(full, &st) == 0;
}

bool ensureDir(const char *path) {
  if (!s_mounted || !path || path[0] != '/') return false;
  char p[96];
  if (strlcpy(p, path, sizeof(p)) >= sizeof(p)) return false;
  size_t n = strlen(p);
  while (n > 1 && p[n - 1] == '/') p[--n] = 0;
  if (n == 1) return true;   // "/" = the card's root
  // Create each level in turn (SD_MMC.mkdir is a no-op for an existing folder).
  for (char *s = p + 1;; s++) {
    if (*s != '/' && *s != 0) continue;
    const char c = *s;
    *s = 0;
    if (!SD_MMC.mkdir(p)) {
      Serial.printf("[media] mkdir %s failed\n", p);
      return false;
    }
    *s = c;
    if (!c) break;
  }
  return true;
}

// ============================================================================
//  asset lookup (name cache)
// ============================================================================
Visual findVisual(const char *id, uint8_t variant) {
  Visual v;
  char lid[ID_MAX_LEN + 1];
  if (!s_mounted || !lowerCopy(lid, sizeof(lid), id)) return v;
  CacheLock lk;
  if (!lk.ok()) return v;
  static const char *const kExt[2] = {"omj", "jpg"};
  char name[NAME_MAX_LEN + 1];
  for (int pass = variant ? 0 : 1; pass < 2; pass++) {
    for (int e = 0; e < 2; e++) {
      const int n = pass == 0 ? snprintf(name, sizeof(name), "%s_%u.%s", lid, (unsigned)variant, kExt[e])
                              : snprintf(name, sizeof(name), "%s.%s", lid, kExt[e]);
      if (n <= 0 || (size_t)n >= sizeof(name)) continue;
      if (hasName(DIR_CLIPS, name)) {
        v.kind = e == 0 ? Kind::Clip : Kind::Still;
        snprintf(v.path, sizeof(v.path), "/oracle/clips/%s", name);
        return v;
      }
    }
  }
  return v;
}

uint8_t countVariants(const char *dirRel, const char *id, const char *ext) {
  if (!s_mounted || !dirRel || !ext) return 0;
  while (*ext == '.') ext++;
  char lid[ID_MAX_LEN + 1], lext[12];
  if (!lowerCopy(lid, sizeof(lid), id) || !lowerCopy(lext, sizeof(lext), ext)) return 0;
  const int d = dirIndex(dirRel);
  char name[NAME_MAX_LEN + 1];
  uint8_t count = 0;
  if (d >= 0) {
    CacheLock lk;
    if (!lk.ok()) return 0;
    for (unsigned i = 1; i <= 9; i++) {
      const int n = snprintf(name, sizeof(name), "%s_%u.%s", lid, i, lext);
      if (n <= 0 || (size_t)n >= sizeof(name) || !hasName((uint8_t)d, name)) break;
      count = (uint8_t)i;
    }
    return count;
  }
  // A folder that isn't cached: ask the card (slow-ish; not meant for per-frame use).
  while (*dirRel == '/') dirRel++;
  if (strncasecmp(dirRel, "oracle/", 7) == 0) dirRel += 7;
  char ldir[ID_MAX_LEN + 1];
  if (!lowerCopy(ldir, sizeof(ldir), dirRel)) return 0;
  size_t dl = strlen(ldir);
  while (dl && ldir[dl - 1] == '/') ldir[--dl] = 0;
  char path[VFS_PATH_CAP];
  for (unsigned i = 1; i <= 9; i++) {
    const int n = snprintf(path, sizeof(path), "%s/oracle/%s/%s_%u.%s", MOUNT, ldir, lid, i, lext);
    struct stat st;
    if (n <= 0 || (size_t)n >= sizeof(path) || ::stat(path, &st) != 0) break;
    count = (uint8_t)i;
  }
  return count;
}

// True if the mounted card still answers a raw sector read. A card pulled and
// re-inserted powers up idle: the old mount can't reach it (SD_MMC never
// re-initialises it), and FatFs's cached sectors could hide that from opendir.
bool cardAnswers() {
  uint8_t *b = (uint8_t *)heap_caps_malloc(SECTOR, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b) return true;   // can't tell: assume it's fine
  const bool ok = SD_MMC.readRAW(b, 0);
  heap_caps_free(b);
  return ok;
}

bool rescan() {
  if (!s_mounted) {
    mount();   // a card inserted after boot (or one that failed the first time)
  } else if (!cardAnswers()) {
    Serial.println("[media] rescan: the card doesn't answer (removed / swapped?) - keeping the old cache");
    return false;          // don't wipe the name cache: a reboot re-mounts it
  } else {
    buildCache();
  }
  flushStillCache();
  if (s_poolBuf) {
    heap_caps_free(s_poolBuf);
    s_poolBuf = nullptr;
    s_poolCap = 0;
  }
  return s_mounted;
}

uint16_t assetCount() {
  CacheLock lk;
  if (!lk.ok()) return 0;
  uint32_t n = 0;
  for (int d = 0; d < DIR_COUNT; d++) n += s_dirN[d];
  return (uint16_t)std::min<uint32_t>(n, 0xFFFF);
}

// ============================================================================
//  Clip (OMJ1)
// ============================================================================
bool Clip::open(const char *path) {
  close();
  if (!s_mounted || !path || !*path) return false;
  if (isBad(path)) return false;
  char full[VFS_PATH_CAP];
  if (!vfsPath(full, sizeof(full), path)) {
    markBad(path, "bad path");
    return false;
  }
  const int fd = ::open(full, O_RDONLY);
  if (fd < 0) {
    markBad(path, "can't open");
    return false;
  }
  // header: "OMJ1" u16 w, u16 h, u16 fps*100, u16 reserved, u32 frames, u32 maxFrameBytes
  uint8_t h[20];
  uint32_t pos = 0;
  const char *bad = nullptr;
  uint16_t w = 0, ht = 0, fps100 = 0;
  uint32_t frames = 0, maxBytes = 0;
  if (readAt(fd, pos, h, sizeof(h)) != (int32_t)sizeof(h) || memcmp(h, "OMJ1", 4) != 0) {
    bad = "not an OMJ1 clip";
  } else {
    w = rd16(h + 4);
    ht = rd16(h + 6);
    fps100 = rd16(h + 8);
    frames = rd32(h + 12);
    maxBytes = rd32(h + 16);
    if (w != SCR_W || ht != SCR_H) bad = "frames are not 800x480";
    else if (frames == 0) bad = "no frames";
    else if (maxBytes == 0 || maxBytes > MAX_CLIP_FRAME) bad = "maxFrameBytes is 0 or > 160 KB";
  }
  if (bad) {
    ::close(fd);
    markBad(path, bad);
    return false;
  }
  if (fps100 == 0) {
    Serial.printf("[media] %s: fps is 0 in the header, assuming 13\n", path);
    fps100 = 1300;
  }

  ClipFile *f = new (std::nothrow) ClipFile{fd, pos};
  uint32_t cap = 0;
  // buffer layout: [u32 len][JPEG][pad to 4] — the length rides along with the bytes
  uint8_t *buf = f ? takeClipBuffer(4 + ((maxBytes + 3) & ~3u), cap) : nullptr;
  if (!f || !buf) {
    Serial.printf("[media] %s: out of memory for a %lu B frame buffer\n", path, (unsigned long)maxBytes);
    if (buf) giveClipBuffer(buf, cap);
    delete f;
    ::close(fd);
    return false;
  }
  _file = f;
  _buf = buf;
  _cap = cap;
  _frames = frames;
  _maxBytes = maxBytes;
  _next = 0;
  _dataStart = pos;   // 20
  _fps100 = fps100;
  _w = w;
  _h = ht;
  _lastShown = -1;
  _open = true;
  return true;
}

void Clip::close() {
  if (_file) {
    ClipFile *f = static_cast<ClipFile *>(_file);
    if (f->fd >= 0) ::close(f->fd);
    delete f;
    _file = nullptr;
  }
  if (_buf) {
    giveClipBuffer(_buf, _cap);
    _buf = nullptr;
  }
  _cap = 0;
  _open = false;
  _frames = 0;
  _maxBytes = 0;
  _next = 0;
  _fps100 = 0;
  _w = _h = 0;
  _lastShown = -1;
}

bool Clip::render(uint32_t elapsedMs, bool loop, uint16_t *fb) {
  if (!_open || !_file || !_buf || !fb) return false;
  ClipFile &f = *static_cast<ClipFile *>(_file);
  for (int attempt = 0; attempt < 2 && _frames > 0; attempt++) {
    uint32_t target = (uint32_t)((uint64_t)elapsedMs * _fps100 / 100000u);
    if (loop) target %= _frames;
    else if (target >= _frames) target = _frames - 1;   // hold the last frame

    if ((int32_t)target != _lastShown) {
      _lastShown = -1;   // _buf is about to be overwritten
      const int r = loadFrame(f, _buf, _cap, _maxBytes, _dataStart, _next, _frames, target);
      if (r == 0) {
        if (clipErrDue())
          Serial.printf("[media] clip ends after %lu frames (header says more) - using that\n", (unsigned long)_frames);
        continue;   // retry with the corrected frame count
      }
      if (r < 0) {
        _next = 0xFFFFFFFFu;   // forces a clean re-seek from the first frame next time
        if (clipErrDue()) Serial.printf("[media] clip read error at frame %lu\n", (unsigned long)target);
        return false;
      }
      _lastShown = (int32_t)target;
    }
    // Decode the kept bytes (also when the frame didn't change: fb is a different
    // buffer every time, so the picture has to be produced again).
    const char *why = nullptr;
    if (decodeFull(_buf + 4, rd32(_buf), fb, &why)) return true;
    if (clipErrDue()) Serial.printf("[media] clip frame %lu: %s\n", (unsigned long)target, why ? why : "?");
    return false;
  }
  return false;
}

// ============================================================================
//  stills + sprites
// ============================================================================
bool drawStill(const char *path, uint16_t *fb) {
  if (!path || !*path || !fb) return false;
  for (auto &s : s_still) {
    if (s.valid && strcasecmp(s.path, path) == 0) {
      s.used = ++s_stillTick;
      memcpy(fb, s.px, FRAME_BYTES);
      return true;
    }
  }
  if (!s_mounted || isBad(path)) return false;

  const char *why = nullptr;
  const uint32_t len = readWholeFile(path, MAX_STILL_FILE, why);
  if (!len) {
    markBad(path, why);
    return false;
  }
  StillSlot *slot = strlen(path) < PATH_CAP ? pickStillSlot() : nullptr;
  if (!slot) {   // no PSRAM for the cache (or an over-long path): decode straight into fb
    if (decodeFull(s_scratch, len, fb, &why)) return true;
    markBad(path, why);
    return false;
  }
  slot->valid = false;
  if (!decodeFull(s_scratch, len, slot->px, &why)) {
    markBad(path, why);
    return false;
  }
  strlcpy(slot->path, path, PATH_CAP);
  slot->valid = true;
  slot->used = ++s_stillTick;
  memcpy(fb, slot->px, FRAME_BYTES);
  return true;
}

void flushStillCache() {
  for (auto &s : s_still) {
    if (s.px) heap_caps_free(s.px);
    s.px = nullptr;
    s.valid = false;
    s.path[0] = 0;
    s.used = 0;
  }
  for (auto &s : s_sprite) {
    if (s.data) heap_caps_free(s.data);
    s.data = nullptr;
    s.len = 0;
    s.path[0] = 0;
    s.used = 0;
  }
  if (s_scratch) heap_caps_free(s_scratch);
  s_scratch = nullptr;
  s_scratchCap = 0;
  clearBad();
}

bool drawJpegAt(const char *path, uint16_t *fb, int16_t x, int16_t y) {
  if (!path || !*path || !fb) return false;
  SpriteSlot *sp = findSprite(path);
  if (sp) {
    sp->used = ++s_spriteTick;
    if (decodeAt(sp->data, sp->len, fb, x, y)) return true;
    dropSprite(path);
    markBad(path, "decode error");
    return false;
  }
  if (!s_mounted || isBad(path)) return false;
  const char *why = nullptr;
  const uint32_t len = readWholeFile(path, MAX_SPRITE_FILE, why);
  if (!len) {
    markBad(path, why);
    return false;
  }
  sp = cacheSprite(path, s_scratch, len);
  const bool ok = sp ? decodeAt(sp->data, sp->len, fb, x, y) : decodeAt(s_scratch, len, fb, x, y);
  if (!ok) {
    dropSprite(path);
    markBad(path, "not a baseline JPEG / decode error");
  }
  return ok;
}

// ============================================================================
//  service
// ============================================================================
float benchmarkReadMBps(const char *path, uint32_t maxBytes) {
  if (!s_mounted || !path || !s_bounce || maxBytes == 0) return 0.0f;
  char full[VFS_PATH_CAP];
  if (!vfsPath(full, sizeof(full), path)) return 0.0f;
  const int fd = ::open(full, O_RDONLY);
  if (fd < 0) return 0.0f;
  uint32_t pos = 0, total = 0;
  const int64_t t0 = esp_timer_get_time();
  while (total < maxBytes) {
    const uint32_t want = std::min(maxBytes - total, s_bounceCap);
    const int32_t got = readAt(fd, pos, s_bounce, want);   // internal buffer: direct DMA
    if (got <= 0) break;
    total += (uint32_t)got;
    if ((uint32_t)got < want) break;   // end of file
  }
  const int64_t us = esp_timer_get_time() - t0;
  ::close(fd);
  if (!total || us <= 0) return 0.0f;
  const float mbps = (float)total / (float)us;   // bytes per microsecond == MB/s (1 MB = 10^6 B)
  Serial.printf("[media] read %lu B of %s in %lu ms = %.2f MB/s\n", (unsigned long)total, path,
                (unsigned long)(us / 1000), (double)mbps);
  return mbps;
}

}  // namespace media
