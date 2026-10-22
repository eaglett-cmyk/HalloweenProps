#pragma once
// ============================================================================
//  audio.h — ES8389 codec (I2C 0x10) on I2S1 at a FIXED 48 kHz, MCLK = 256*fs
//  on GPIO6, BCLK 44, WS 16, DOUT 15. Speaker amps (2x NS4150B) enabled by
//  expander EXIO3 (hardware mutes them while a 3.5 mm plug is inserted).
//
//  Engine: a mixer task on core 0 (priority 18) sums, every 5 ms block:
//    * one MUSIC bed     — streamed from SD, looped, ducked -10 dB under voice
//    * one VOICE line    — streamed from SD (a new line replaces the old one)
//    * up to 4 SFX       — preloaded into PSRAM
//    * one TONE          — generated (fallback chimes, service-menu test)
//  A streamer task (core 0, priority 6) keeps both stream buffers filled, reading
//  8 KB at a time through an internal DMA buffer. The mixer never touches the SD.
//
//  Asset format: RIFF WAV, PCM s16le, MONO, 48000 Hz (tools/build_assets.py makes
//  these). Anything else is rejected (logged) and treated as missing.
//  ES8389 note: esp_codec_dev's set_mute() writes the ADC mute register; the DAC
//  is muted with reg 0x40 bits[1:0] — we use that (or the amp enable) instead.
// ============================================================================
#include <Arduino.h>

namespace audio {

constexpr uint32_t SAMPLE_RATE = 48000;

bool begin(uint8_t volumePct);   // I2S first (MCLK running), then codec init, then amp on
bool ok();

void setVolume(uint8_t pct);     // master: codec DAC volume (0 = mute), 0..100
uint8_t volume();

// ---- music bed (streamed, looped) ----
bool music(const char *path, uint8_t gainPct = 55);  // false if missing/bad (silence)
void musicStop();
bool musicPlaying();

// ---- voice line (streamed) ----
// Starts the line and returns its duration in ms (0 if missing/bad -> nothing plays).
uint32_t voice(const char *path, uint8_t gainPct = 100);
void voiceStop();
bool voiceBusy();
uint32_t voiceRemainingMs();
uint8_t voiceLevel();            // 0..255 short-term envelope (for mouth/glow pulsing)

// ---- sound effects (preloaded) ----
int  sfxLoad(const char *path);  // handle >= 0, or -1 if missing/bad. Same path twice -> same handle.
void sfx(int handle, uint8_t gainPct = 100);   // ignored if handle < 0
void sfxStopAll();

// ---- generated tones (always available, no files needed) ----
enum Chime : uint8_t { CHIME_ABSORB, CHIME_REVEAL, CHIME_RARE, CHIME_LEGEND, CHIME_TRICK, CHIME_TAP, CHIME_ERROR, CHIME_TEST };
void chime(Chime c, uint8_t gainPct = 60);
void tone(uint16_t hz, uint16_t ms, uint8_t gainPct = 50);

}  // namespace audio
