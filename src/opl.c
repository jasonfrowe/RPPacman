#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
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

#define MUSIC_BUF_SIZE 512u
#define MUSIC_MAX_BGM_CH 8u
#define MUSIC_EVENTS_PER_FRAME_BUDGET 64u

// One instance of this drives the main gameplay music (channels 0-8,
// never stops); a second, completely independent instance drives the
// reserved SFX channel (5 only) -- an ambient loop (ready via
// player_open's loop=true) that one-shot event stingers (loop=false)
// temporarily take over from, then hand back to. Pulling this out of
// what used to be a single hardcoded set of music_* statics is what
// makes a second independent stream possible without duplicating the
// buffered-read/tick/tempo-scale logic.
typedef struct {
    int fd;
    const char *filename; // currently-open ROM: path, to detect same-file reopens
    uint8_t buffer[MUSIC_BUF_SIZE];
    uint16_t buf_idx;
    uint16_t bytes_ready;
    uint16_t wait_ticks;
    bool error_state;
    bool just_looped;
    bool paused;
    bool loop;   // false: stop (don't wrap) at the 0xFF,0xFF end marker
    bool ended;  // true once a non-looping player has hit its end marker
    uint16_t tempo_scale; // 256 = 1.0x; see music_init's comment
    uint16_t tempo_acc;
} music_player_t;

static music_player_t s_music_player = { .fd = -1 };
static music_player_t s_sfx_player = { .fd = -1 };

static bool player_is_bgm_slot(uint8_t slot_offset) {
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

// min_ch/max_ch restrict the direct per-channel registers (freq/keyon/
// feedback, where channel = reg - base is trivial) to the range this
// player is allowed to touch -- 0-8 for the main player, 5-5 for the SFX
// player, so a stray write from one stream can never land on a channel
// the other stream owns. The operator registers (0x20-0xF5) aren't
// independently channel-checked here (that needs a reverse offset->
// channel lookup this doesn't have) -- relying instead on generation-time
// guarantees that SFX tracks only ever contain channel-5 operator writes
// (opl2_translate.py's sfx profile never emits anything else, confirmed
// by inspecting every generated SFX track's actual register list).
static bool player_reg_allowed(uint8_t reg, uint8_t min_ch, uint8_t max_ch) {
    if (reg >= 0xA0 && reg <= 0xA8) { uint8_t ch = reg - 0xA0; return ch >= min_ch && ch <= max_ch; }
    if (reg >= 0xB0 && reg <= 0xB8) { uint8_t ch = reg - 0xB0; return ch >= min_ch && ch <= max_ch; }
    if (reg >= 0xC0 && reg <= 0xC8) { uint8_t ch = reg - 0xC0; return ch >= min_ch && ch <= max_ch; }

    if ((reg >= 0x20 && reg <= 0x35) ||
        (reg >= 0x40 && reg <= 0x55) ||
        (reg >= 0x60 && reg <= 0x75) ||
        (reg >= 0x80 && reg <= 0x95) ||
        (reg >= 0xE0 && reg <= 0xF5)) {
        return player_is_bgm_slot(reg & 0x1F);
    }

    return true;
}

static bool player_refill_buffer(music_player_t *p) {
    int res = read(p->fd, p->buffer, MUSIC_BUF_SIZE);
    if (res < 0) {
        p->error_state = true;
        return false;
    }
    p->buf_idx = 0;
    p->bytes_ready = (uint16_t)res;
    return true;
}

// loop=true: wrap back to the start at the 0xFF,0xFF end marker forever
// (gameplay music, SFX-channel ambient loops). loop=false: stop there and
// set p->ended (one-shot event stingers) so the caller can fall back to
// whatever should play next.
static void player_open(music_player_t *p, const char *filename, bool loop) {
    // Reopening the exact same file that's already active -- most
    // commonly "ROM:sfxpellet", retriggered on every single dot eaten,
    // easily several times a second -- is both wasteful and, on real
    // hardware, appears to occasionally leave the SFX channel silenced:
    // close()+open() churn on the same small file many times a second
    // hits a transient failure on the real SD-card-backed filesystem that
    // the emulator's much faster/idealized file I/O never reproduces.
    // Restarting via a plain seek avoids that open()/close() churn
    // entirely for this, by far the most common, retrigger case.
    if (p->fd >= 0 && p->filename && strcmp(p->filename, filename) == 0) {
        if (lseek(p->fd, 0, SEEK_SET) >= 0) {
            p->buf_idx = 0;
            p->bytes_ready = 0;
            p->wait_ticks = 0;
            p->just_looped = false;
            p->paused = false;
            p->loop = loop;
            p->ended = false;
            p->error_state = false;
            p->tempo_scale = 1024; // Default to 4.0x -- see music_init's comment
            p->tempo_acc = 0;
            player_refill_buffer(p);
            return;
        }
        // Seek failed -- fall through to a full close+reopen below.
    }

    if (p->fd >= 0) close(p->fd);
    p->fd = open(filename, O_RDONLY);
    if (p->fd < 0) {
        // A fresh open() has also been observed to fail transiently on
        // real hardware; one retry recovers from that instead of
        // permanently latching error_state (silent until some later,
        // unrelated trigger happens to call player_open() again).
        p->fd = open(filename, O_RDONLY);
    }
    p->filename = filename;

    p->buf_idx = 0;
    p->bytes_ready = 0;
    p->wait_ticks = 0;
    p->just_looped = false;
    p->paused = false;
    p->loop = loop;
    p->ended = false;
    p->error_state = (p->fd < 0);
    p->tempo_scale = 1024; // Default to 4.0x -- see music_init's comment
    p->tempo_acc = 0;

    if (p->error_state) return;
    player_refill_buffer(p);
}

static void player_stop(music_player_t *p) {
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
    p->filename = 0;
    p->buf_idx = 0;
    p->bytes_ready = 0;
    p->wait_ticks = 0;
    p->just_looped = false;
    p->error_state = false;
    p->paused = false;
    p->ended = false;
    p->tempo_scale = 1024;
    p->tempo_acc = 0;
}

// Kick-drum beat detection for the palette-flash effect (tile_mode2.c).
// Register 0xBD's bit 4 is the rhythm-mode bass-drum key-on flag; only
// the gameplay music's own .BIN stream ever writes 0xBD at all (SFX
// tracks skip rhythm_setup() entirely, and nothing else writes raw
// register bytes through this path), so hooking it here needs no extra
// bookkeeping about which player is calling. A real kick is a 0->1
// transition on that bit specifically -- the translator always writes a
// bit-clearing "off" value immediately before the "on" value when
// forcing a fresh retrigger (see opl2_translate.py's process_rhythm),
// but once set, bit 4 otherwise stays set across unrelated hi-hat/snare/
// tom/cymbal retriggers until the next real kick, so testing "is the bit
// set" alone would fire on every one of those too.
#define RYT_BD_BIT 0x10
static uint8_t s_prev_rhythm_reg = 0x00;
static bool s_kick_hit_pending = false;

static void track_kick_hit(uint8_t reg, uint8_t val) {
    if (reg != 0xBD) return;
    if ((val & RYT_BD_BIT) && !(s_prev_rhythm_reg & RYT_BD_BIT)) {
        s_kick_hit_pending = true;
    }
    s_prev_rhythm_reg = val;
}

// Consumes (clears) the pending kick-hit flag. Call once per frame.
bool opl_consume_kick_hit(void) {
    bool hit = s_kick_hit_pending;
    s_kick_hit_pending = false;
    return hit;
}

static void player_advance(music_player_t *p, uint8_t ticks, uint8_t min_ch, uint8_t max_ch) {
    if (p->paused || p->error_state || p->fd < 0 || p->ended) return;
    if (ticks == 0u) ticks = 1u;

    uint16_t effective_ticks = ticks;
    if (p->tempo_scale != 256) {
        if (p->tempo_scale == 1024) {
            // Common case (the current, and currently only, tempo
            // scale in use): an exact multiple of 256, so a shift
            // stands in for the general fixed-point multiply below --
            // the 6502 has no hardware multiplier, so a real 16x16
            // multiply here would mean a genuine subroutine call every
            // single frame for no benefit over `<< 2`.
            effective_ticks = (uint16_t)ticks << 2;
        } else {
            p->tempo_acc += (uint16_t)ticks * p->tempo_scale;
            effective_ticks = p->tempo_acc >> 8;
            p->tempo_acc &= 0x00FF;
        }
        if (effective_ticks == 0) return;
    }

    if (p->wait_ticks > effective_ticks) {
        p->wait_ticks -= effective_ticks;
    } else {
        p->wait_ticks = 0;
    }

    if (p->wait_ticks == 0) {
        // Budget must scale with effective_ticks (the real number of .BIN
        // ticks being advanced this call), not the raw vsync-frame delta --
        // with a 4x tempo scale, 4 ticks' worth of events can legitimately
        // need processing in a single call. Using raw `ticks` here (always
        // 1) capped this at MUSIC_EVENTS_PER_FRAME_BUDGET regardless of
        // tempo scale, causing a real, audible stall on any burst denser
        // than that -- confirmed by scanning music/PacManCE_01.BIN: one
        // burst of 68 zero-delay events, just over the old effective cap.
        uint16_t budget = (uint16_t)MUSIC_EVENTS_PER_FRAME_BUDGET * effective_ticks;
        if (budget == 0u) budget = MUSIC_EVENTS_PER_FRAME_BUDGET;
        if (budget > 255u) budget = 255u;

        uint16_t events_processed = 0;
        while (p->wait_ticks == 0 && events_processed < budget) {
            if (p->buf_idx > p->bytes_ready ||
                (uint16_t)(p->bytes_ready - p->buf_idx) < 4u) {
                if (!player_refill_buffer(p)) {
                    return;
                }
                if (p->bytes_ready == 0) {
                    if (!p->loop) {
                        p->ended = true;
                        return;
                    }
                    if (lseek(p->fd, 0, SEEK_SET) < 0) {
                        p->error_state = true;
                        return;
                    }
                    if (!player_refill_buffer(p)) {
                        return;
                    }
                    p->just_looped = true;
                    continue;
                }
            }

            uint8_t reg  = p->buffer[p->buf_idx++];
            uint8_t val  = p->buffer[p->buf_idx++];
            uint8_t d_lo = p->buffer[p->buf_idx++];
            uint8_t d_hi = p->buffer[p->buf_idx++];
            uint16_t delay = ((uint16_t)d_hi << 8) | d_lo;
            events_processed++;

            if (p->just_looped &&
                reg >= 0xB0 && reg <= 0xB8 &&
                (val & 0x20u) == 0u &&
                delay <= 1u) {
                p->just_looped = false;
                continue;
            }
            p->just_looped = false;

            if (reg == 0xFF && val == 0xFF) {
                if (!p->loop) {
                    p->ended = true;
                    return;
                }
                if (lseek(p->fd, 0, SEEK_SET) < 0) {
                    p->error_state = true;
                    return;
                }
                if (!player_refill_buffer(p)) {
                    return;
                }
                p->just_looped = true;
                continue;
            } else {
                if (player_reg_allowed(reg, min_ch, max_ch)) {
                    opl_write(reg, val);
                    track_kick_hit(reg, val);
                }
            }

            if (delay > 0) {
                p->wait_ticks = delay;
            }
        }

        // Yield to the rest of the frame if a pathological stream keeps delay at zero.
        if (p->wait_ticks == 0 && events_processed >= budget) {
            p->wait_ticks = 1;
        }
    }
}

void music_set_tempo_scale(uint16_t scale_256) {
    s_music_player.tempo_scale = scale_256;
}

void music_init(const char* filename) {
    player_open(&s_music_player, filename, true);
    // Re-initialize OPL registers to clear lingering patches and key-on states from previous songs
    opl_init();
}

void music_stop(void) {
    player_stop(&s_music_player);
    opl_init();
}

void music_pause(void) {
    if (s_music_player.fd >= 0 && !s_music_player.error_state) {
        s_music_player.paused = true;
        for (uint8_t i = 0; i <= MUSIC_MAX_BGM_CH; i++) {
            opl_write(0xB0 + i, 0x00);
        }
    }
}

void music_resume(void) {
    if (s_music_player.fd >= 0 && !s_music_player.error_state) {
        s_music_player.paused = false;
    }
}

void update_music_advance(uint8_t ticks) {
    player_advance(&s_music_player, ticks, 0, MUSIC_MAX_BGM_CH);
}

void update_music() {
    update_music_advance(1u);
}

// --- SFX channel (5): an ambient loop, temporarily interrupted by
// one-shot event stingers, "newest always wins" -- a new trigger (either
// sfx_play or a sfx_set_ambient that lands while nothing else is
// playing) always immediately cuts off whatever's currently on the
// channel. ---

static const char *s_sfx_ambient_file = 0; // last-set ambient path, for fallback
static bool s_sfx_is_oneshot = false;

// Every switch on the SFX channel risks leaving a stuck note: a track can
// end (or get cut off mid-playback by a newer trigger) with channel 5's
// key-on bit still set, and nothing downstream ever clears it on its own
// -- player_open() just starts reading a new file, it doesn't know
// whether the channel it's about to reuse was mid-note. Silencing
// channel 5 directly (not through OPL_NoteOff, which relies on
// shadow_b0[] -- never updated by the raw .BIN playback path) before
// every switch guarantees no held note ever survives a transition,
// regardless of how the interrupted content happened to end.
static void sfx_silence_channel(void) {
    opl_write(0xB5, 0x00);
}

void sfx_set_ambient(const char *filename) {
    s_sfx_ambient_file = filename;
    if (!s_sfx_is_oneshot) {
        sfx_silence_channel();
        player_open(&s_sfx_player, filename, true);
    }
    // A one-shot is currently playing: just remember the new ambient: sfx
    // channel's the update loop switches to it once the one-shot ends.
}

void sfx_play(const char *filename) {
    s_sfx_is_oneshot = true;
    sfx_silence_channel();
    player_open(&s_sfx_player, filename, false);
}

void sfx_stop(void) {
    player_stop(&s_sfx_player);
    s_sfx_ambient_file = 0;
    s_sfx_is_oneshot = false;
    sfx_silence_channel();
}

void update_sfx_advance(uint8_t ticks) {
    player_advance(&s_sfx_player, ticks, 5, 5);
    if (s_sfx_is_oneshot && s_sfx_player.ended) {
        s_sfx_is_oneshot = false;
        sfx_silence_channel();
        if (s_sfx_ambient_file) {
            player_open(&s_sfx_player, s_sfx_ambient_file, true);
        }
    }
}
