#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include "constants.h"
#include "congrats.h"
#include "tile_mode2.h"
#include "sprite_mode5.h" // player global
#include "player.h"  // game_mode_t / get_game_mode()
#include "hiscores.h"
#include "opl.h"
#include "ghost.h"   // start_rankings_screen() -- defined in main.c, declared there like start_congrats_screen()
#include "results.h" // return_to_title_from_post_game()

#define FADE_FRAMES 8

// All dynamic text (rank/score banner, initials, rankings list) renders
// through the existing TEXT_MAP_DATA/FONT_TILES_DATA font via
// write_text_to_text_map() (tile_mode2.c) -- the same font already used
// for "1UP"/"HI SCORE" and everywhere else in the game, extended there
// with '_' and the s/t/n/d/h ordinal-suffix letters this screen needs.
// TITLE_MAP_DATA/TITLE_TILES_DATA (congratsmap/congratstiles,
// rankingsmap/rankingstiles) is only the static background art -- same
// split the results screen uses.

static void load_rom_asset_to_xram(const char *filename, unsigned dest, unsigned size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return;
    read_xram(dest, size, fd);
    close(fd);
}

// congratsmap places the "NORMAL" banner (tiles 115,116,117 = "NO","RM",
// "AL") at map cells (13,14)-(15,14) -- confirmed by decoding
// Congrats_map.bin directly. In EXTRA mode, repoint those same 3 cells at
// congratstiles' own "EXTRA" banner (126,127,128 = "EX","TR","A ") added
// alongside it.
static void patch_congrats_mode_label(bool is_extra) {
    if (!is_extra) return;
    static const uint8_t EXTRA_TILES[3] = { 126, 127, 128 };
    for (uint8_t i = 0; i < 3; i++) {
        RIA.addr0 = TITLE_MAP_DATA + (14 * TITLE_MAP_WIDTH) + 13 + i;
        RIA.step0 = 1;
        RIA.rw0 = EXTRA_TILES[i];
    }
}

// ---------------------------------------------------------------------
// Congrats screen: rank/score banner + 3-letter initials entry
// ---------------------------------------------------------------------

typedef enum {
    CONGRATS_FADE_OUT,
    CONGRATS_SWAP_ASSETS,
    CONGRATS_FADE_IN,
    CONGRATS_DRAW_BANNER,
    CONGRATS_ENTER_INITIALS,
    CONGRATS_FADE_OUT_TO_RANKINGS
} congrats_substate_t;

static congrats_substate_t s_substate;
static uint16_t s_timer;
static int8_t s_rank;    // 0-based (0 = 1st place)
static uint8_t s_cursor; // 0..2, which initial is currently being edited
static char s_initials[3];

static void ordinal_suffix(uint8_t rank1based, char out[2]) {
    if (rank1based == 1) { out[0] = 's'; out[1] = 't'; }
    else if (rank1based == 2) { out[0] = 'n'; out[1] = 'd'; }
    else if (rank1based == 3) { out[0] = 'r'; out[1] = 'd'; }
    else { out[0] = 't'; out[1] = 'h'; }
}

// Writes "<rank><suffix>   <7-digit score>" starting at (14,8), e.g.
// "1st   0012345". The 3 spaces where a colon would visually sit are
// deliberate -- the user's example was "1st : 0000000" but no tile index
// was given for ':', so this leaves a blank gap there rather than risk
// showing a wrong glyph; trivial to fill in once that tile is known.
static void draw_rank_and_score(int8_t rank, uint32_t score) {
    uint8_t rank1 = (uint8_t)(rank + 1);
    char buf[17];
    uint8_t n = 0;

    if (rank1 >= 10) {
        buf[n++] = (char)('0' + (rank1 / 10));
        buf[n++] = (char)('0' + (rank1 % 10));
    } else {
        buf[n++] = (char)('0' + rank1);
    }

    char suf[2];
    ordinal_suffix(rank1, suf);
    buf[n++] = suf[0];
    buf[n++] = suf[1];
    buf[n++] = ' ';
    buf[n++] = ' ';
    buf[n++] = ' ';

    uint32_t val = score;
    char digits[7];
    for (int8_t i = 6; i >= 0; i--) {
        uint32_t next_val = val / 10;
        digits[i] = (char)('0' + (val - next_val * 10));
        val = next_val;
    }
    for (uint8_t i = 0; i < 7; i++) buf[n++] = digits[i];
    buf[n] = '\0';

    write_text_to_text_map(14, 8, buf);
}

static void draw_initials(void) {
    char c0[2] = { s_initials[0], '\0' };
    char c1[2] = { s_initials[1], '\0' };
    char c2[2] = { s_initials[2], '\0' };
    write_text_to_text_map(18, 10, c0);
    write_text_to_text_map(20, 10, c1);
    write_text_to_text_map(22, 10, c2);
}

void congrats_init(int8_t rank) {
    s_substate = CONGRATS_FADE_OUT;
    s_timer = 0;
    s_rank = rank;
    s_cursor = 0;
    s_initials[0] = 'A';
    s_initials[1] = '_';
    s_initials[2] = '_';
}

void congrats_update(bool press_up, bool press_down, bool press_action) {
    switch (s_substate) {

        case CONGRATS_FADE_OUT: {
            // 8 frames, same ramp idiom as results.c's own fades -- fades
            // out whatever the results screen was showing (title palette,
            // since results has already swapped over to its own
            // TITLE_MAP-based content by the time this can trigger).
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                s_substate = CONGRATS_SWAP_ASSETS;
                s_timer = 0;
            }
            break;
        }

        case CONGRATS_SWAP_ASSETS: {
            load_rom_asset_to_xram("ROM:congratsmap", TITLE_MAP_DATA, TITLE_MAP_DATA_SIZE);
            load_rom_asset_to_xram("ROM:congratstiles", TITLE_TILES_DATA, TITLE_TILES_DATA_SIZE);
            patch_congrats_mode_label(get_game_mode() == GAME_MODE_EXTRA);
            // Results' own score-totals digits (rows 3-4) are still
            // sitting on the TEXT_MAP layer at this point -- clear them
            // so they don't bleed through on top of this screen.
            blank_text_map_all();
            s_substate = CONGRATS_FADE_IN;
            s_timer = 0;
            break;
        }

        case CONGRATS_FADE_IN: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = (s_timer / 2) + 1;
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                restore_title_palette();
                // pacman05 (results music) keeps playing through this
                // screen, unchanged, per the user's own instruction.
                s_substate = CONGRATS_DRAW_BANNER;
                s_timer = 0;
            }
            break;
        }

        case CONGRATS_DRAW_BANNER: {
            draw_rank_and_score(s_rank, player.score);
            draw_initials();
            s_substate = CONGRATS_ENTER_INITIALS;
            break;
        }

        case CONGRATS_ENTER_INITIALS: {
            bool changed = false;

            if (press_up) {
                s_initials[s_cursor]++;
                if (s_initials[s_cursor] > 'Z') s_initials[s_cursor] = 'A';
                changed = true;
            } else if (press_down) {
                s_initials[s_cursor]--;
                if (s_initials[s_cursor] < 'A') s_initials[s_cursor] = 'Z';
                changed = true;
            }

            if (press_action) {
                s_cursor++;
                if (s_cursor < 3) {
                    s_initials[s_cursor] = 'A'; // reveal the next slot
                    changed = true;
                } else {
                    hiscores_insert(get_game_mode(), s_rank, player.score, s_initials);
                    s_substate = CONGRATS_FADE_OUT_TO_RANKINGS;
                    s_timer = 0;
                    break;
                }
            }

            if (changed) draw_initials();
            break;
        }

        case CONGRATS_FADE_OUT_TO_RANKINGS: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                start_rankings_screen(false);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------
// Rankings screen: top-10 table
// ---------------------------------------------------------------------

typedef enum {
    RANKINGS_SWAP_ASSETS,
    RANKINGS_FADE_IN,
    RANKINGS_DRAW_LIST,
    RANKINGS_WAIT_FOR_CONFIRM
} rankings_substate_t;

static rankings_substate_t s_rank_substate;
static uint16_t s_rank_timer;
static bool s_rank_return_to_options;
static game_mode_t s_rank_view_mode;
static bool s_rank_allow_toggle;

// rankingsmap places the "NORMAL"/"EXTRA" banner across two rows: row 5
// (60,61,62,63,64,65 = N,O,R,M,A,L) and row 6 (66,67,68,69,68,70 = the
// same letters' bottom halves), both at columns 17-22 -- confirmed by
// decoding Rankings_map.bin directly. rankingstiles' own added "EXTRA"
// glyphs (106,107,110,112,114,116 / 108,109,111,113,115,117) sit
// alongside the NORMAL ones in the same loaded tileset, so switching
// which board is showing is just repointing these 12 map cells, no asset
// reload needed.
static void patch_rankings_mode_label(bool is_extra) {
    static const uint8_t NORMAL_ROW0[6] = { 60, 61, 62, 63, 64, 65 };
    static const uint8_t NORMAL_ROW1[6] = { 66, 67, 68, 69, 68, 70 };
    static const uint8_t EXTRA_ROW0[6]  = { 106, 107, 110, 112, 114, 116 };
    static const uint8_t EXTRA_ROW1[6]  = { 108, 109, 111, 113, 115, 117 };
    const uint8_t *row0 = is_extra ? EXTRA_ROW0 : NORMAL_ROW0;
    const uint8_t *row1 = is_extra ? EXTRA_ROW1 : NORMAL_ROW1;

    for (uint8_t i = 0; i < 6; i++) {
        RIA.addr0 = TITLE_MAP_DATA + (5 * TITLE_MAP_WIDTH) + 17 + i;
        RIA.step0 = 1;
        RIA.rw0 = row0[i];
    }
    for (uint8_t i = 0; i < 6; i++) {
        RIA.addr0 = TITLE_MAP_DATA + (6 * TITLE_MAP_WIDTH) + 17 + i;
        RIA.step0 = 1;
        RIA.rw0 = row1[i];
    }
}

static void draw_rankings_list(void) {
    for (uint8_t i = 0; i < HISCORE_COUNT; i++) {
        uint32_t score = hiscores_get_score(s_rank_view_mode, i);
        const char *initials = hiscores_get_initials(s_rank_view_mode, i);

        char sbuf[8];
        uint32_t val = score;
        for (int8_t d = 6; d >= 0; d--) {
            uint32_t next_val = val / 10;
            sbuf[d] = (char)('0' + (val - next_val * 10));
            val = next_val;
        }
        sbuf[7] = '\0';
        write_text_to_text_map(17, 4 + i, sbuf);

        char ibuf[4] = { initials[0], initials[1], initials[2], '\0' };
        write_text_to_text_map(25, 4 + i, ibuf);
    }
}

// return_to_options screens (reached from the Options menu) always start
// on NORMAL and let the confirm button cycle NORMAL -> EXTRA -> back to
// Options. Reached from Congrats (just qualified in whichever mode was
// actually played), there's no toggle -- it shows only that mode's board,
// and a single press returns to the title, exactly as before.
void rankings_init(bool return_to_options) {
    s_rank_substate = RANKINGS_SWAP_ASSETS;
    s_rank_timer = 0;
    s_rank_return_to_options = return_to_options;
    s_rank_allow_toggle = return_to_options;
    s_rank_view_mode = return_to_options ? GAME_MODE_NORMAL : get_game_mode();
}

void rankings_update(bool press_confirm) {
    switch (s_rank_substate) {

        case RANKINGS_SWAP_ASSETS: {
            load_rom_asset_to_xram("ROM:rankingsmap", TITLE_MAP_DATA, TITLE_MAP_DATA_SIZE);
            load_rom_asset_to_xram("ROM:rankingstiles", TITLE_TILES_DATA, TITLE_TILES_DATA_SIZE);
            patch_rankings_mode_label(s_rank_view_mode == GAME_MODE_EXTRA);
            // Congrats' own rank/score banner and initials (rows 6, 10)
            // are still sitting on the TEXT_MAP layer, overlapping the
            // rows this screen's list uses (4-13) -- clear them first.
            blank_text_map_all();
            music_init("ROM:pacman03");
            s_rank_substate = RANKINGS_FADE_IN;
            s_rank_timer = 0;
            break;
        }

        case RANKINGS_FADE_IN: {
            if (s_rank_timer < FADE_FRAMES) {
                uint8_t step = (s_rank_timer / 2) + 1;
                set_title_palette_scaled(step, 4, false, 0, false);
                s_rank_timer++;
            } else {
                restore_title_palette();
                s_rank_substate = RANKINGS_DRAW_LIST;
                s_rank_timer = 0;
            }
            break;
        }

        case RANKINGS_DRAW_LIST: {
            draw_rankings_list();
            s_rank_substate = RANKINGS_WAIT_FOR_CONFIRM;
            break;
        }

        case RANKINGS_WAIT_FOR_CONFIRM: {
            if (press_confirm) {
                if (s_rank_allow_toggle && s_rank_view_mode == GAME_MODE_NORMAL) {
                    // First press: swap the board to EXTRA and stay here.
                    s_rank_view_mode = GAME_MODE_EXTRA;
                    patch_rankings_mode_label(true);
                    draw_rankings_list();
                } else if (s_rank_return_to_options) {
                    start_options_screen();
                } else {
                    return_to_title_from_post_game();
                }
            }
            break;
        }
    }
}
