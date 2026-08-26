#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include "constants.h"
#include "soundtest.h"
#include "tile_mode2.h"
#include "sprite_mode5.h" // player global
#include "opl.h"
#include "ghost.h"   // start_options_screen()/set_pacman_cursor_hidden() -- defined in main.c
#include "results.h" // blank_text_map_all()

#define FADE_FRAMES 8

static void load_rom_asset_to_xram(const char *filename, unsigned dest, unsigned size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return;
    read_xram(dest, size, fd);
    close(fd);
}

typedef enum {
    SOUNDTEST_FADE_OUT,
    SOUNDTEST_SWAP_ASSETS,
    SOUNDTEST_FADE_IN,
    SOUNDTEST_MENU_READY,
    SOUNDTEST_FADE_OUT_TO_OPTIONS
} soundtest_substate_t;

typedef enum {
    ST_OPL2 = 0,
    ST_SE = 1,
    ST_EXIT = 2,
    ST_COUNT = 3
} soundtest_item_t;

// Pac-Man cursor position for each of the 3 items -- same column as the
// options screen, rows from the user's own layout.
#define SOUNDTEST_CURSOR_X 88
static const int16_t SOUNDTEST_ROW_Y[ST_COUNT] = { 80, 112, 200 };

#define NUM_TRACKS 8
static const char *const OPL2_TRACKS[NUM_TRACKS] = {
    "ROM:pacman00", "ROM:pacman01", "ROM:pacman02", "ROM:pacman03",
    "ROM:pacman04", "ROM:pacman05", "ROM:pacman06", "ROM:pacman07",
};
static const char *const SE_TRACKS[NUM_TRACKS] = {
    "ROM:sfxdeath", "ROM:sfxpellet", "ROM:sfxghosteat", "ROM:sfxmazeup",
    "ROM:sfxprizeplace", "ROM:sfxextralife", "ROM:sfxfrightened", "ROM:sfxnormal",
};

static soundtest_substate_t s_substate;
static uint16_t s_timer;
static uint8_t s_selection;   // ST_OPL2/ST_SE/ST_EXIT
static uint8_t s_opl2_index;  // 0..7, independent of s_se_index
static uint8_t s_se_index;    // 0..7, independent of s_opl2_index
static uint8_t s_pacman_anim_timer;
static uint8_t s_pacman_anim_frame;

static void position_cursor(void) {
    player.x_pos_px = SOUNDTEST_CURSOR_X;
    player.y_pos_px = SOUNDTEST_ROW_Y[s_selection];
    player.frame = s_pacman_anim_frame;
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
}

// OPL2's and SE's 00-07 indices are independent and both always visible,
// regardless of which row is currently highlighted (including EXIT) --
// tile rows 5 and 7 match OPL2's/SE's own pixel rows (80, 112) divided by
// the 16px text-tile size, same columns (22,23) the user specified for
// OPL2's original single indicator.
static void draw_index(uint8_t row, uint8_t index) {
    char buf[3];
    buf[0] = (char)('0' + (index / 10));
    buf[1] = (char)('0' + (index % 10));
    buf[2] = '\0';
    write_text_to_text_map(22, row, buf);
}

static void draw_indices(void) {
    draw_index(5, s_opl2_index);
    draw_index(7, s_se_index);
}

void soundtest_init(void) {
    s_substate = SOUNDTEST_FADE_OUT;
    s_timer = 0;
    s_selection = ST_OPL2;
    s_opl2_index = 0;
    s_se_index = 0;
    s_pacman_anim_timer = 0;
    s_pacman_anim_frame = 6;
}

void soundtest_update(bool press_up, bool press_down, bool press_left, bool press_right, bool press_action, bool press_fire) {
    switch (s_substate) {

        case SOUNDTEST_FADE_OUT: {
            // 8 frames, same ramp idiom as options.c/congrats.c -- fades
            // out the options menu (title_palette).
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                s_substate = SOUNDTEST_SWAP_ASSETS;
                s_timer = 0;
            }
            break;
        }

        case SOUNDTEST_SWAP_ASSETS: {
            load_rom_asset_to_xram("ROM:soundtestmap", TITLE_MAP_DATA, TITLE_MAP_DATA_SIZE);
            load_rom_asset_to_xram("ROM:soundtesttiles", TITLE_TILES_DATA, TITLE_TILES_DATA_SIZE);
            // The options menu's own text is still sitting on the
            // TEXT_MAP layer -- clear it now that the screen is black.
            blank_text_map_all();
            // No music on this screen -- the whole point is auditioning
            // tracks/SFX on demand, not a background loop competing with
            // them.
            music_stop();
            sfx_stop();
            s_substate = SOUNDTEST_FADE_IN;
            s_timer = 0;
            break;
        }

        case SOUNDTEST_FADE_IN: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = (s_timer / 2) + 1;
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                restore_title_palette();
                position_cursor();
                draw_indices();
                s_substate = SOUNDTEST_MENU_READY;
                s_timer = 0;
            }
            break;
        }

        case SOUNDTEST_MENU_READY: {
            if (press_up) {
                s_selection = (s_selection == 0) ? (uint8_t)(ST_COUNT - 1) : (uint8_t)(s_selection - 1);
            } else if (press_down) {
                s_selection = (s_selection == ST_COUNT - 1) ? 0 : (uint8_t)(s_selection + 1);
            }

            // Left/right only ever change whichever row is currently
            // highlighted -- OPL2's and SE's indices are independent.
            if (s_selection == ST_OPL2) {
                if (press_left) {
                    s_opl2_index = (s_opl2_index == 0) ? (NUM_TRACKS - 1) : (uint8_t)(s_opl2_index - 1);
                } else if (press_right) {
                    s_opl2_index = (s_opl2_index == NUM_TRACKS - 1) ? 0 : (uint8_t)(s_opl2_index + 1);
                }
            } else if (s_selection == ST_SE) {
                if (press_left) {
                    s_se_index = (s_se_index == 0) ? (NUM_TRACKS - 1) : (uint8_t)(s_se_index - 1);
                } else if (press_right) {
                    s_se_index = (s_se_index == NUM_TRACKS - 1) ? 0 : (uint8_t)(s_se_index + 1);
                }
            }

            if (press_left || press_right) {
                draw_indices();
            }

            // Animate cursor (chomp) same as the title/options menus.
            s_pacman_anim_timer++;
            if (s_pacman_anim_timer >= 4) {
                s_pacman_anim_timer = 0;
                s_pacman_anim_frame = (s_pacman_anim_frame == 6) ? 7 : 6;
            }
            position_cursor();

            if (press_action) {
                if (s_selection == ST_OPL2) {
                    music_init(OPL2_TRACKS[s_opl2_index]);
                } else if (s_selection == ST_SE) {
                    sfx_play(SE_TRACKS[s_se_index], SFX_PRIORITY_TOP);
                } else { // ST_EXIT
                    set_pacman_cursor_hidden();
                    music_stop();
                    sfx_stop();
                    s_substate = SOUNDTEST_FADE_OUT_TO_OPTIONS;
                    s_timer = 0;
                }
            }
            if (press_fire) {
                // 'B' always stops both, regardless of which row is
                // highlighted -- simpler than tracking which one is
                // actually playing.
                music_stop();
                sfx_stop();
            }
            break;
        }

        case SOUNDTEST_FADE_OUT_TO_OPTIONS: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                start_options_screen();
            }
            break;
        }
    }
}
