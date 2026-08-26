#ifndef OPL_H
#define OPL_H

#include <stdint.h>
#include <stdbool.h>

#define OPL_ADDR            0xFE00U
#define USE_NATIVE_OPL2     1

typedef struct {
    uint8_t m_ave, m_ksl, m_atdec, m_susrel, m_wave;
    uint8_t c_ave, c_ksl, c_atdec, c_susrel, c_wave;
    uint8_t feedback;
} OPL_Patch;

extern uint8_t shadow_b0[9]; 
extern uint8_t shadow_ksl_m[9];
extern uint8_t shadow_ksl_c[9];

extern void OPL_NoteOn(uint8_t channel, uint8_t midi_note);
extern void OPL_NoteOff(uint8_t channel);
extern void opl_clear(void);
extern void opl_write(uint8_t reg, uint8_t value);
extern void update_music(void);
extern void update_music_advance(uint8_t ticks);
extern void OPL_SetVolume(uint8_t chan, uint8_t velocity);
extern void opl_init(void);
extern void opl_fifo_clear(void);
extern void opl_silence_all(void);
extern void OPL_Config(uint8_t enable, uint16_t addr);
extern void music_init(const char* filename);
extern void music_set_tempo_scale(uint16_t scale_256);
extern void music_stop(void);
extern void music_pause(void);
extern void music_resume(void);

// The reserved SFX channel (5): a fully independent second audio lane
// alongside the always-playing gameplay music. sfx_set_ambient() picks
// what loops there by default (the frightened/normal ambient tracks,
// lowest priority -- they only ever play when no one-shot is active);
// sfx_play() cuts in a one-shot event stinger that automatically falls
// back to the current ambient once it finishes. update_sfx_advance()
// must be called once per real vsync tick, the same way
// update_music_advance() is.
//
// One-shots carry a priority so a torrent of low-priority events (dot
// pellets, eaten many times a second) can't cut off a rarer, more
// important cue (death, eating a ghost, a maze transition, a prize
// appearing, an extra life) that's still playing: sfx_play() only
// starts a new one-shot if nothing is currently playing, or the new one
// is at least as high priority as whatever's active. A same-or-higher
// priority call still always cuts in ("newest wins" within a tier); a
// strictly lower one is dropped outright rather than queued.
#define SFX_PRIORITY_MEDIUM 1 // sfxpellet
#define SFX_PRIORITY_TOP    2 // sfxdeath/sfxghosteat/sfxmazeup/sfxprizeplace/sfxextralife

extern void sfx_set_ambient(const char* filename);
extern void sfx_play(const char* filename, uint8_t priority);
extern void sfx_stop(void);
extern void sfx_pause(void);
extern void sfx_resume(void);
extern void update_sfx_advance(uint8_t ticks);

// Returns true once for each real beat hit -- rhythm register 0xBD's
// bass-drum, snare, or cymbal bit going 0->1 (whichever one is currently
// carrying the beat; not every rhythm voice toggles for the whole track,
// see track_kick_hit's comment in opl.c) -- written by the gameplay music
// since the last call, then clears the flag -- call exactly once per frame.
extern bool opl_consume_kick_hit(void);
extern void OPL_SetPatch(uint8_t channel, const OPL_Patch* patch);
extern void shutdown_audio(void);

#endif // OPL_H
