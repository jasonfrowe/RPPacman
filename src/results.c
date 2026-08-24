#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "constants.h"
#include "results.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "player.h"
#include "opl.h"
#include "ghost.h" // start_warm_title_screen() -- defined in main.c, declared here like start_results_screen()

typedef enum {
    RESULTS_FADE_TO_SCORE_ONLY,
    RESULTS_JINGLE_PLAY,
    RESULTS_PAN_TO_CENTER,
    RESULTS_HOLD_FOR_JINGLE,
    RESULTS_FADE_OUT_SCORE,
    RESULTS_RESTORE_PAN,
    RESULTS_SWAP_ASSETS,
    RESULTS_FADE_IN_RESULTS,
    RESULTS_DRAW_TOTALS,
    RESULTS_HISTOGRAM_ANIM,
    RESULTS_WAIT_FOR_START,
    RESULTS_RETURN_TO_TITLE
} results_substate_t;

static results_substate_t s_substate;
static uint16_t s_timer;
static uint8_t s_bar_index;  // 0..89 (30 intervals x 3 categories) during HISTOGRAM_ANIM
static uint8_t s_row_index;  // 0..12, rows revealed so far for the current bar

// Score block during gameplay: row 0, cols 9-15 (7 digit tiles), 16px
// TEXT_MAP tiles -> pixel span x=144..256 (center 200), y=0..16 (center 8).
// Pan target centers that block on the 320x240 screen. x_pos_px/y_pos_px
// on vga_mode2_config_t are a direct additive screen-position offset
// (matching how sprite x_pos_px/y_pos_px work elsewhere in this
// codebase), not a subtracted camera-scroll value -- confirmed empirically
// via emulator screenshots (the un-negated version drove the score off
// the top of the screen instead of centering it).
#define SCORE_PAN_TARGET_X ((SCREEN_WIDTH / 2) - 200)  // -40
#define SCORE_PAN_TARGET_Y ((SCREEN_HEIGHT / 2) - 8)   // 112
#define PAN_FRAMES  90
#define HOLD_FRAMES 165
#define FONT_FADE_FRAMES 8

#define HIST_ROWS 13          // tile rows 23 (bottom) .. 11 (top)
#define HIST_MAX_POINTS 32500 // 2500 pts/row * 13 rows
#define HIST_SUBLEVELS (HIST_ROWS * 8) // 104 -- 32500/104 isn't a clean
                                        // divide like the old 13000 scale
                                        // was, so points_to_sublevel() uses
                                        // a real (32-bit-intermediate)
                                        // multiply/divide. Only runs once
                                        // per bar (a handful of times per
                                        // interval during HISTOGRAM_ANIM),
                                        // nowhere near a hot path.
//
// Verified by decoding images/Results_tiles_4bpp.bin directly (153 tiles
// total). Each category's own fill set (against black/blank) has 8
// levels (1-8 eighths filled, level 8 = fully opaque). Given the fixed
// z-order (ghost=back, prize=middle, pellet=front), a category's PARTIAL
// top row can end up overwriting a still-taller earlier category's
// solid color -- pasting real black into what should show that earlier
// color instead. Blend tiles fix that for the 3 pairs that can actually
// occur (ghost never needs one, it's always backmost); each blend set
// only needs 7 levels (1-7 -- level 8 would be fully opaque, so it just
// reuses the plain "-on-black" solid tile, background irrelevant).
#define TILE_BLANK            62
#define TILE_GHOST_BASE       145 // blue-on-black,  8 levels (145-152)
#define TILE_PRIZE_ON_BLACK   130 // pink-on-black,  8 levels (130-137)
#define TILE_PRIZE_ON_BLUE    138 // pink-on-blue,   7 levels (138-144)
#define TILE_PELLET_ON_BLACK  108 // yellow-on-black,8 levels (108-115)
#define TILE_PELLET_ON_PINK   116 // yellow-on-pink, 7 levels (116-122)
#define TILE_PELLET_ON_BLUE   123 // yellow-on-blue, 7 levels (123-129)
#define ROW_REVEAL_FRAMES 3

// Histogram bars now share one column per interval (10-second intervals,
// matching the real NES CE layout) instead of 3 side-by-side columns --
// drawn in this fixed z-order (ghost first/bottom, pellet last/top) per
// interval, so a later category's bar overwrites an earlier one's tiles
// wherever they overlap at the same row.
static const uint8_t HISTOGRAM_DRAW_ORDER[SCORE_CAT_COUNT] = {
    SCORE_CAT_GHOST, SCORE_CAT_PRIZE, SCORE_CAT_PELLET
};

void results_init(void) {
    s_substate = RESULTS_FADE_TO_SCORE_ONLY;
    s_timer = 0;
    s_bar_index = 0;
    s_row_index = 0;
}

static void load_rom_asset_to_xram(const char *filename, unsigned dest, unsigned size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        printf("results: failed to open %s\n", filename);
        return;
    }
    int n = read_xram(dest, size, fd);
    if (n < 0) {
        printf("results: read_xram(%s) failed (%d)\n", filename, n);
    }
    close(fd);
}

// Blanks all of TEXT_MAP_DATA except the 7 score-digit tiles at row 0,
// cols 9-15, so nothing but the live score survives onto the results
// screen's font layer.
static void blank_text_map_except_score(void) {
    for (uint8_t ty = 0; ty < TEXT_MAP_HEIGHT; ty++) {
        if (ty == 0) {
            RIA.addr0 = TEXT_MAP_DATA;
            RIA.step0 = 1;
            for (uint8_t tx = 0; tx < 9; tx++) RIA.rw0 = 0;

            RIA.addr0 = TEXT_MAP_DATA + 16;
            RIA.step0 = 1;
            for (uint8_t tx = 16; tx < TEXT_MAP_WIDTH; tx++) RIA.rw0 = 0;
        } else {
            RIA.addr0 = TEXT_MAP_DATA + (ty * TEXT_MAP_WIDTH);
            RIA.step0 = 1;
            for (uint8_t tx = 0; tx < TEXT_MAP_WIDTH; tx++) RIA.rw0 = 0;
        }
    }
}

static void blank_text_map_all(void) {
    RIA.addr0 = TEXT_MAP_DATA;
    RIA.step0 = 1;
    for (uint16_t i = 0; i < (uint16_t)TEXT_MAP_WIDTH * TEXT_MAP_HEIGHT; i++) RIA.rw0 = 0;
}

// Generalizes the digit_tile_map pattern used (separately, per call site)
// by tile_mode2.c's various score/lives/timer displays -- this one is
// parameterized since the results screen needs 4 different displays at 4
// different coordinates.
static void write_digits(uint16_t base_addr, uint16_t width, uint8_t tx, uint8_t ty, uint32_t value, uint8_t ndigits) {
    static const uint8_t digit_tile_map[10] = {
        46, 37, 38, 39, 40, 41, 42, 43, 44, 45
    };
    uint8_t digits[7];
    uint32_t val = value;
    for (int8_t i = (int8_t)ndigits - 1; i >= 0; i--) {
        uint32_t next_val = val / 10;
        uint8_t rem = (uint8_t)(val - next_val * 10);
        digits[i] = digit_tile_map[rem];
        val = next_val;
    }

    RIA.addr0 = base_addr + ((uint16_t)ty * width) + tx;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < ndigits; i++) {
        RIA.rw0 = digits[i];
    }
}

static uint16_t points_to_sublevel(uint16_t points) {
    if (points > HIST_MAX_POINTS) points = HIST_MAX_POINTS;
    return (uint16_t)(((uint32_t)points * HIST_SUBLEVELS) / HIST_MAX_POINTS);
}

// remaining = how many of this row's 8 sub-levels are filled, given a
// bar's total sublevel height and how many full rows sit below this one.
// 0 means this category has no presence at this row at all.
static uint8_t row_remaining(uint16_t sublevel, uint8_t row_from_bottom) {
    int16_t remaining = (int16_t)sublevel - (int16_t)row_from_bottom * 8;
    if (remaining <= 0) return 0;
    if (remaining > 8) remaining = 8;
    return (uint8_t)remaining;
}

static bool category_present_at_row(uint8_t interval, uint8_t category, uint8_t row_from_bottom) {
    uint16_t sublevel = points_to_sublevel(get_score_history_bucket(interval, category));
    return row_remaining(sublevel, row_from_bottom) > 0;
}

// Picks the correct tile for one category at one row of one interval's
// shared column, given the fixed z-order (ghost back, prize middle,
// pellet front): a fully-solid row (remaining==8) is always fully
// opaque, so it just uses the plain "-on-black" solid tile regardless of
// what's behind it. A partial row (1-7) needs a blend tile whenever an
// earlier (further-back) category still has any presence at this same
// row -- otherwise that category's color would get erased by real black
// where it should still show through.
static uint8_t histogram_tile_for(uint8_t interval, uint8_t category, uint8_t row_from_bottom) {
    uint16_t sublevel = points_to_sublevel(get_score_history_bucket(interval, category));
    uint8_t remaining = row_remaining(sublevel, row_from_bottom);
    if (remaining == 0) return TILE_BLANK;

    if (category == SCORE_CAT_GHOST) {
        return (uint8_t)(TILE_GHOST_BASE + (remaining - 1));
    }

    if (category == SCORE_CAT_PRIZE) {
        if (remaining == 8) return (uint8_t)(TILE_PRIZE_ON_BLACK + 7);
        bool ghost_present = category_present_at_row(interval, SCORE_CAT_GHOST, row_from_bottom);
        uint8_t base = ghost_present ? TILE_PRIZE_ON_BLUE : TILE_PRIZE_ON_BLACK;
        return (uint8_t)(base + (remaining - 1));
    }

    // SCORE_CAT_PELLET
    if (remaining == 8) return (uint8_t)(TILE_PELLET_ON_BLACK + 7);
    if (category_present_at_row(interval, SCORE_CAT_PRIZE, row_from_bottom)) {
        return (uint8_t)(TILE_PELLET_ON_PINK + (remaining - 1));
    }
    bool ghost_present = category_present_at_row(interval, SCORE_CAT_GHOST, row_from_bottom);
    uint8_t base = ghost_present ? TILE_PELLET_ON_BLUE : TILE_PELLET_ON_BLACK;
    return (uint8_t)(base + (remaining - 1));
}

static void draw_totals(void) {
    write_digits(TEXT_MAP_DATA, TEXT_MAP_WIDTH, 28, 3, player.score, 7);
    write_digits(TEXT_MAP_DATA, TEXT_MAP_WIDTH, 8, 4, player.score_by_cat[SCORE_CAT_PELLET], 7);
    write_digits(TEXT_MAP_DATA, TEXT_MAP_WIDTH, 18, 4, player.score_by_cat[SCORE_CAT_PRIZE], 7);
    write_digits(TEXT_MAP_DATA, TEXT_MAP_WIDTH, 28, 4, player.score_by_cat[SCORE_CAT_GHOST], 7);
}

void results_update(bool press_start) {
    switch (s_substate) {

        case RESULTS_FADE_TO_SCORE_ONLY: {
            blank_text_map_except_score();
            set_maze_palette_black();
            s_substate = RESULTS_JINGLE_PLAY;
            s_timer = 0;
            break;
        }

        case RESULTS_JINGLE_PLAY: {
            music_init("ROM:pacman01");
            s_substate = RESULTS_PAN_TO_CENTER;
            s_timer = 0;
            break;
        }

        case RESULTS_PAN_TO_CENTER: {
            if (s_timer < PAN_FRAMES) {
                s_timer++;
                int16_t x = (int16_t)((SCORE_PAN_TARGET_X * (int16_t)s_timer) / PAN_FRAMES);
                int16_t y = (int16_t)((SCORE_PAN_TARGET_Y * (int16_t)s_timer) / PAN_FRAMES);
                xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, x_pos_px, x);
                xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, y_pos_px, y);
            } else {
                s_substate = RESULTS_HOLD_FOR_JINGLE;
                s_timer = 0;
            }
            break;
        }

        case RESULTS_HOLD_FOR_JINGLE: {
            s_timer++;
            if (s_timer >= HOLD_FRAMES) {
                s_substate = RESULTS_FADE_OUT_SCORE;
                s_timer = 0;
            }
            break;
        }

        case RESULTS_FADE_OUT_SCORE: {
            // 8 frames, same ramp idiom as TITLE_SUBSTATE_WARM_FADE_OUT.
            if (s_timer < FONT_FADE_FRAMES) {
                uint8_t step = FONT_FADE_FRAMES / 2 - (s_timer / 2); // 4,4,3,3,2,2,1,1
                set_font_palette_scaled(step, 4);
                s_timer++;
            } else {
                set_font_palette_black();
                s_substate = RESULTS_RESTORE_PAN;
                s_timer = 0;
            }
            break;
        }

        case RESULTS_RESTORE_PAN: {
            xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, x_pos_px, 0);
            xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, y_pos_px, 0);
            blank_text_map_all(); // clear stale score digits before the results totals get drawn
            s_substate = RESULTS_SWAP_ASSETS;
            s_timer = 0;
            break;
        }

        case RESULTS_SWAP_ASSETS: {
            load_rom_asset_to_xram("ROM:resultsmap", TITLE_MAP_DATA, TITLE_MAP_DATA_SIZE);
            load_rom_asset_to_xram("ROM:resultstiles", TITLE_TILES_DATA, TITLE_TILES_DATA_SIZE);
            s_substate = RESULTS_FADE_IN_RESULTS;
            s_timer = 0;
            break;
        }

        case RESULTS_FADE_IN_RESULTS: {
            // 8 frames, same ramp idiom as TITLE_SUBSTATE_WARM_FADE_IN.
            if (s_timer < FONT_FADE_FRAMES) {
                uint8_t step = (s_timer / 2) + 1; // 1,1,2,2,3,3,4,4
                set_title_palette_scaled(step, 4, false, 0, false);
                set_font_palette_scaled(step, 4);
                s_timer++;
            } else {
                restore_title_palette();
                restore_font_palette();
                music_init("ROM:pacman05");
                s_substate = RESULTS_DRAW_TOTALS;
                s_timer = 0;
            }
            break;
        }

        case RESULTS_DRAW_TOTALS: {
            draw_totals();
            s_substate = RESULTS_HISTOGRAM_ANIM;
            s_timer = 0;
            s_bar_index = 0;
            s_row_index = 0;
            break;
        }

        case RESULTS_HISTOGRAM_ANIM: {
            if (s_bar_index >= SCORE_HISTORY_BUCKETS * SCORE_CAT_COUNT) {
                s_substate = RESULTS_WAIT_FOR_START;
                s_timer = 0;
                break;
            }

            uint8_t interval = s_bar_index / SCORE_CAT_COUNT;
            uint8_t category = HISTOGRAM_DRAW_ORDER[s_bar_index % SCORE_CAT_COUNT];
            uint16_t points = get_score_history_bucket(interval, category);
            uint16_t sublevel = points_to_sublevel(points);
            uint8_t target_rows = (sublevel == 0) ? 0 : (uint8_t)((sublevel + 7) / 8);
            if (target_rows > HIST_ROWS) target_rows = HIST_ROWS;

            if (s_row_index >= target_rows) {
                s_bar_index++;
                s_row_index = 0;
                s_timer = 0;
                break;
            }

            if (s_timer == 0) {
                uint8_t tx = (uint8_t)(5 + interval); // one shared column per interval now
                uint8_t ty = (uint8_t)(23 - s_row_index);
                uint8_t tile = histogram_tile_for(interval, category, s_row_index);

                RIA.addr0 = TITLE_MAP_DATA + ((uint16_t)ty * TITLE_MAP_WIDTH) + tx;
                RIA.step0 = 1;
                RIA.rw0 = tile;
            }
            s_timer++;
            if (s_timer >= ROW_REVEAL_FRAMES) {
                s_row_index++;
                s_timer = 0;
            }
            break;
        }

        case RESULTS_WAIT_FOR_START: {
            if (press_start) {
                s_substate = RESULTS_RETURN_TO_TITLE;
                s_timer = 0;
            }
            break;
        }

        case RESULTS_RETURN_TO_TITLE: {
            load_rom_asset_to_xram("ROM:titlemap", TITLE_MAP_DATA, TITLE_MAP_DATA_SIZE);
            load_rom_asset_to_xram("ROM:titletile", TITLE_TILES_DATA, TITLE_TILES_DATA_SIZE);
            // start_warm_title_screen() never blanks TEXT_MAP_DATA -- it
            // only overwrites specific rows for the menu text, assuming
            // whatever gameplay left in the other rows is harmless. Our
            // 4 score-total digit runs (rows 3-4) aren't; clear them so
            // they don't reappear once the font palette fades back in.
            blank_text_map_all();
            start_warm_title_screen();
            break;
        }
    }
}
