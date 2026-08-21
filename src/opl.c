#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "opl.h"

// F-number table for one octave at block 4, indexed starting at Bb (not C):
// fnum_table[0] measures ~233.7 Hz (Bb3), not C4's 261.6 Hz. midi_to_opl_freq
// accounts for that with a base offset of 10, not 12 -- using 12 here made
// every note come out a whole tone (2 semitones) flat, verified both
// mathematically and by measuring a rendered note's actual fundamental.
const uint16_t fnum_table[12] = {
    308, 325, 345, 365, 387, 410, 434, 460, 487, 516, 547, 579
};

uint8_t shadow_b0[9] = {0};
uint8_t shadow_ksl_m[9] = {0};
uint8_t shadow_ksl_c[9] = {0};

uint16_t midi_to_opl_freq(uint8_t midi_note) {
    if (midi_note < 10) midi_note = 10;

    int block = (midi_note - 10) / 12;
    int note_idx = (midi_note - 10) % 12;

    if (block > 7) block = 7;

    uint16_t f_num = fnum_table[note_idx];
    uint8_t high_byte = 0x20 | (block << 2) | ((f_num >> 8) & 0x03);
    uint8_t low_byte = f_num & 0xFF;

    return (high_byte << 8) | low_byte;
}

void opl_write(uint8_t reg, uint8_t data) {
#ifdef USE_NATIVE_OPL2
    RIA.addr1 = OPL_ADDR + reg;
    RIA.rw1 = data;
#else
    RIA.addr1 = OPL_ADDR;
    RIA.step1 = 1;
    RIA.rw1 = reg;
    RIA.rw1 = data;
#endif
}

void opl_silence_all() {
    for (uint8_t i = 0; i < 9; i++) {
        opl_write(0xB0 + i, 0x00);
    }
}

void opl_fifo_clear() {
    RIA.addr1 = OPL_ADDR + 2; 
    RIA.step1 = 0;
    RIA.rw1 = 1;         
}

void OPL_NoteOn(uint8_t channel, uint8_t midi_note) {
    if (channel > 8) return;
    
    uint16_t freq = midi_to_opl_freq(midi_note);
    opl_write(0xA0 + channel, freq & 0xFF);
    opl_write(0xB0 + channel, (freq >> 8) & 0xFF);
    shadow_b0[channel] = (freq >> 8) & 0x1F;
}

void OPL_NoteOff(uint8_t channel) {
    if (channel > 8) return;
    opl_write(0xB0 + channel, shadow_b0[channel] & 0x1F); // Write Octave & F-Num with KeyOn = 0
}

void opl_clear() {
    for (int i = 0; i < 256; i++) {
        opl_write(i, 0x00);
    }
    for (int i = 0; i < 9; i++) {
        shadow_b0[i] = 0;
    }
}

void OPL_SetVolume(uint8_t chan, uint8_t velocity) {
    uint8_t vol = 63 - (velocity >> 1);
    static const uint8_t car_offsets[] = {0x03,0x04,0x05,0x0B,0x0C,0x0D,0x13,0x14,0x15};
    opl_write(0x40 + car_offsets[chan], (shadow_ksl_c[chan] & 0xC0) | vol);
}

void OPL_SetPatch(uint8_t channel, const OPL_Patch* p) {
    static const uint8_t mod_offsets[] = {0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12};
    static const uint8_t car_offsets[] = {0x03,0x04,0x05,0x0B,0x0C,0x0D,0x13,0x14,0x15};
    
    uint8_t m = mod_offsets[channel];
    uint8_t c = car_offsets[channel];

    shadow_ksl_m[channel] = p->m_ksl;
    shadow_ksl_c[channel] = p->c_ksl;

    opl_write(0x20 + m, p->m_ave);
    opl_write(0x20 + c, p->c_ave);
    opl_write(0x40 + m, p->m_ksl);
    opl_write(0x40 + c, p->c_ksl);
    opl_write(0x60 + m, p->m_atdec);
    opl_write(0x60 + c, p->c_atdec);
    opl_write(0x80 + m, p->m_susrel);
    opl_write(0x80 + c, p->c_susrel);
    opl_write(0xE0 + m, p->m_wave);
    opl_write(0xE0 + c, p->c_wave);
    opl_write(0xC0 + channel, p->feedback);
}

void opl_init() {
    for (uint8_t i = 0; i < 9; i++) {
        opl_write(0xB0 + i, 0x00);
        shadow_b0[i] = 0;
    }

    for (int i = 0x01; i <= 0xF5; i++) {
        opl_write(i, 0x00);
    }

    for (int i = 0; i < 9; i++) {
        shadow_b0[i] = 0;
    }

    opl_write(0x01, 0x20); // Enable Waveform Select
    opl_write(0xBD, 0x00); // Ensure Melodic Mode
}

void opl_fifo_flush() {
    RIA.addr1 = OPL_ADDR + 2;
    RIA.step1 = 0;
    RIA.rw1 = 0xAA; 
}

void shutdown_audio() {
    opl_silence_all();       
    opl_fifo_flush();        
    OPL_Config(0, OPL_ADDR);   
}

void OPL_Config(uint8_t enable, uint16_t addr) {
#ifdef USE_NATIVE_OPL2
    xreg(0, 1, 0x01, addr); 
#else
    xregn(2, 0, 0, 2, enable, addr);
#endif
}

static int music_fd = -1;
static uint8_t music_buffer[512];
static uint16_t music_buf_idx = 0;
static uint16_t music_bytes_ready = 0;
static uint16_t music_wait_ticks = 0;
static bool music_error_state = false;
static bool music_just_looped = false;
static bool music_paused = false;

#define MUSIC_BUF_SIZE 512u
#define MUSIC_MAX_BGM_CH 8u
#define MUSIC_EVENTS_PER_FRAME_BUDGET 64u

static bool music_is_bgm_slot(uint8_t slot_offset) {
    switch (slot_offset) {
        case 0x00: case 0x01: case 0x02:
        case 0x03: case 0x04: case 0x05:
        case 0x08: case 0x09:
        case 0x0B: case 0x0C:
        case 0x0D: case 0x0E:
        case 0x10: case 0x11: case 0x12:
        case 0x13: case 0x14: case 0x15:
            return true;
        default:
            return false;
    }
}

static bool music_reg_allowed(uint8_t reg) {
    if (reg >= 0xA0 && reg <= 0xA8) return (uint8_t)(reg - 0xA0) <= MUSIC_MAX_BGM_CH;
    if (reg >= 0xB0 && reg <= 0xB8) return (uint8_t)(reg - 0xB0) <= MUSIC_MAX_BGM_CH;
    if (reg >= 0xC0 && reg <= 0xC8) return (uint8_t)(reg - 0xC0) <= MUSIC_MAX_BGM_CH;

    if ((reg >= 0x20 && reg <= 0x35) ||
        (reg >= 0x40 && reg <= 0x55) ||
        (reg >= 0x60 && reg <= 0x75) ||
        (reg >= 0x80 && reg <= 0x95) ||
        (reg >= 0xE0 && reg <= 0xF5)) {
        return music_is_bgm_slot(reg & 0x1F);
    }

    return true;
}

static bool music_refill_buffer(void) {
    int res = read(music_fd, music_buffer, MUSIC_BUF_SIZE);
    if (res < 0) {
        music_error_state = true;
        return false;
    }
    music_buf_idx = 0;
    music_bytes_ready = (uint16_t)res;
    return true;
}

static uint16_t s_music_tempo_scale = 256; // 256 = 1.0x (default tempo speed)
static uint16_t s_tempo_acc = 0;

void music_set_tempo_scale(uint16_t scale_256) {
    s_music_tempo_scale = scale_256;
}

void music_init(const char* filename) {
    if (music_fd >= 0) close(music_fd);
    music_fd = open(filename, O_RDONLY);

    music_buf_idx = 0;
    music_bytes_ready = 0;
    music_wait_ticks = 0;
    music_just_looped = false;
    music_paused = false;
    music_error_state = (music_fd < 0);
    s_music_tempo_scale = 256; // Default to 1.0x speed
    s_tempo_acc = 0;

    // Re-initialize OPL registers to clear lingering patches and key-on states from previous songs
    opl_init();

    if (music_error_state) {
        return;
    }

    if (!music_refill_buffer()) {
        return;
    }
}

void music_stop(void) {
    if (music_fd >= 0) {
        close(music_fd);
        music_fd = -1;
    }
    music_buf_idx = 0;
    music_bytes_ready = 0;
    music_wait_ticks = 0;
    music_just_looped = false;
    music_error_state = false;
    music_paused = false;
    s_music_tempo_scale = 256;
    s_tempo_acc = 0;
    opl_init();
}

void music_pause(void) {
    if (music_fd >= 0 && !music_error_state) {
        music_paused = true;
        for (uint8_t i = 0; i <= MUSIC_MAX_BGM_CH; i++) {
            opl_write(0xB0 + i, 0x00);
        }
    }
}

void music_resume(void) {
    if (music_fd >= 0 && !music_error_state) {
        music_paused = false;
    }
}

void update_music_advance(uint8_t ticks) {
    if (music_paused || music_error_state || music_fd < 0) return;
    if (ticks == 0u) ticks = 1u;

    uint16_t effective_ticks = ticks;
    if (s_music_tempo_scale != 256) {
        s_tempo_acc += (uint16_t)ticks * s_music_tempo_scale;
        effective_ticks = s_tempo_acc >> 8;
        s_tempo_acc &= 0x00FF;
        if (effective_ticks == 0) return;
    }

    if (music_wait_ticks > effective_ticks) {
        music_wait_ticks -= effective_ticks;
    } else {
        music_wait_ticks = 0;
    }

    if (music_wait_ticks == 0) {
        uint16_t budget = (uint16_t)MUSIC_EVENTS_PER_FRAME_BUDGET * (uint16_t)ticks;
        if (budget == 0u) budget = MUSIC_EVENTS_PER_FRAME_BUDGET;
        if (budget > 255u) budget = 255u;

        uint16_t events_processed = 0;
        while (music_wait_ticks == 0 && events_processed < budget) {
            if (music_buf_idx > music_bytes_ready ||
                (uint16_t)(music_bytes_ready - music_buf_idx) < 4u) {
                if (!music_refill_buffer()) {
                    return;
                }
                if (music_bytes_ready == 0) {
                    if (lseek(music_fd, 0, SEEK_SET) < 0) {
                        music_error_state = true;
                        return;
                    }
                    if (!music_refill_buffer()) {
                        return;
                    }
                    music_just_looped = true;
                    continue;
                }
            }

            uint8_t reg  = music_buffer[music_buf_idx++];
            uint8_t val  = music_buffer[music_buf_idx++];
            uint8_t d_lo = music_buffer[music_buf_idx++];
            uint8_t d_hi = music_buffer[music_buf_idx++];
            uint16_t delay = ((uint16_t)d_hi << 8) | d_lo;
            events_processed++;

            if (music_just_looped &&
                reg >= 0xB0 && reg <= 0xB8 &&
                (val & 0x20u) == 0u &&
                delay <= 1u) {
                music_just_looped = false;
                continue;
            }
            music_just_looped = false;

            if (reg == 0xFF && val == 0xFF) {
                if (lseek(music_fd, 0, SEEK_SET) < 0) {
                    music_error_state = true;
                    return;
                }
                if (!music_refill_buffer()) {
                    return;
                }
                music_just_looped = true;
                continue;
            } else {
                if (music_reg_allowed(reg)) {
                    opl_write(reg, val);
                }
            }

            if (delay > 0) {
                music_wait_ticks = delay;
            }
        }

        // Yield to the rest of the frame if a pathological stream keeps delay at zero.
        if (music_wait_ticks == 0 && events_processed >= budget) {
            music_wait_ticks = 1;
        }
    }
}

void update_music() {
    update_music_advance(1u);
}
