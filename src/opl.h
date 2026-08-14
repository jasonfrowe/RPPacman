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
extern void music_stop(void);
extern void music_pause(void);
extern void music_resume(void);
extern void OPL_SetPatch(uint8_t channel, const OPL_Patch* patch);
extern void shutdown_audio(void);

#endif // OPL_H
