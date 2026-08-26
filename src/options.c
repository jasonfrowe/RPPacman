#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include "constants.h"
#include "options.h"
#include "tile_mode2.h"
#include "sprite_mode5.h" // player global
#include "opl.h"
#include "ghost.h"   // start_rankings_screen()/start_options_screen()/set_pacman_cursor_hidden() -- defined in main.c
#include "results.h" // blank_text_map_all()/return_to_title_from_post_game()

#define FADE_FRAMES 8

static void load_rom_asset_to_xram(const char *filename, unsigned dest, unsigned size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return;
    read_xram(dest, size, fd);
    close(fd);
}

typedef enum {
    OPTIONS_FADE_OUT,
    OPTIONS_SWAP_ASSETS,
    OPTIONS_FADE_IN,
    OPTIONS_MENU_READY,
    OPTIONS_FADE_OUT_TO_RANKINGS,
    OPTIONS_FADE_OUT_TO_SOUNDTEST
} options_substate_t;

typedef enum {
    OPT_ACHIEVEMENTS = 0,
    OPT_RANKING,
    OPT_REPLAY_NORMAL,
    OPT_REPLAY_EXTRA,
    OPT_SOUND_TEST,
    OPT_EXIT,
    OPT_COUNT
} options_item_t;

// Pac-Man cursor position for each of the 6 items -- fixed pixel
// coordinates from the user's own layout. Column 88 throughout; EXIT
// sits apart from the other 5 (which are evenly spaced 24px apart).
#define OPTIONS_CURSOR_X 88
static const int16_t OPTIONS_ROW_Y[OPT_COUNT] = { 64, 88, 112, 136, 160, 200 };

static options_substate_t s_substate;
static uint16_t s_timer;
static uint8_t s_selection;
static uint8_t s_pacman_anim_timer;
static uint8_t s_pacman_anim_frame;

static void position_cursor(void) {
    player.x_pos_px = OPTIONS_CURSOR_X;
    player.y_pos_px = OPTIONS_ROW_Y[s_selection];
    player.frame = s_pacman_anim_frame;
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
}

void options_init(void) {
    s_substate = OPTIONS_FADE_OUT;
    s_timer = 0;
    s_selection = OPT_ACHIEVEMENTS;
    s_pacman_anim_timer = 0;
    s_pacman_anim_frame = 6;
}

void options_update(bool press_up, bool press_down, bool press_action) {
    switch (s_substate) {

        case OPTIONS_FADE_OUT: {
            // 8 frames, same ramp idiom as congrats.c/rankings.c -- fades
            // out the title menu (title_palette; the menu's own TEXT_MAP
            // text is cleared below once the screen is already black).
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                s_substate = OPTIONS_SWAP_ASSETS;
                s_timer = 0;
            }
            break;
        }

        case OPTIONS_SWAP_ASSETS: {
            load_rom_asset_to_xram("ROM:optionsmap", TITLE_MAP_DATA, TITLE_MAP_DATA_SIZE);
            load_rom_asset_to_xram("ROM:optionstiles", TITLE_TILES_DATA, TITLE_TILES_DATA_SIZE);
            // The title menu's own "NORMAL/EXTRA/OPTIONS" text is still
            // sitting on the TEXT_MAP layer -- clear it now that the
            // screen is already black.
            blank_text_map_all();
            music_init("ROM:pacman07");
            s_substate = OPTIONS_FADE_IN;
            s_timer = 0;
            break;
        }

        case OPTIONS_FADE_IN: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = (s_timer / 2) + 1;
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                restore_title_palette();
                position_cursor();
                s_substate = OPTIONS_MENU_READY;
                s_timer = 0;
            }
            break;
        }

        case OPTIONS_MENU_READY: {
            if (press_up) {
                s_selection = (s_selection == 0) ? (uint8_t)(OPT_COUNT - 1) : (uint8_t)(s_selection - 1);
            } else if (press_down) {
                s_selection = (s_selection == OPT_COUNT - 1) ? 0 : (uint8_t)(s_selection + 1);
            }

            // Animate Pac-Man cursor (tiles 6 and 7, 4 frames each) --
            // same idiom as the title menu's own cursor.
            s_pacman_anim_timer++;
            if (s_pacman_anim_timer >= 4) {
                s_pacman_anim_timer = 0;
                s_pacman_anim_frame = (s_pacman_anim_frame == 6) ? 7 : 6;
            }
            position_cursor();

            if (press_action) {
                switch (s_selection) {
                    case OPT_RANKING:
                        // Rankings itself has no fade-out of its own (it
                        // expects the screen already black, same as
                        // congrats.c's own hand-off) -- fade out here first.
                        set_pacman_cursor_hidden();
                        s_substate = OPTIONS_FADE_OUT_TO_RANKINGS;
                        s_timer = 0;
                        break;
                    case OPT_SOUND_TEST:
                        // Same fade-out-first hand-off as Ranking:
                        // soundtest.c has no fade-out of its own, it
                        // expects the screen already black.
                        set_pacman_cursor_hidden();
                        s_substate = OPTIONS_FADE_OUT_TO_SOUNDTEST;
                        s_timer = 0;
                        break;
                    case OPT_EXIT:
                        return_to_title_from_post_game();
                        break;
                    default:
                        // Achievements / Replay Normal / Replay Extra:
                        // not yet implemented -- no-op for now.
                        break;
                }
            }
            break;
        }

        case OPTIONS_FADE_OUT_TO_RANKINGS: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                // Rankings returns here (rather than to the title screen)
                // via rankings_init()'s return_to_options flag.
                start_rankings_screen(true);
            }
            break;
        }

        case OPTIONS_FADE_OUT_TO_SOUNDTEST: {
            if (s_timer < FADE_FRAMES) {
                uint8_t step = FADE_FRAMES / 2 - (s_timer / 2);
                set_title_palette_scaled(step, 4, false, 0, false);
                s_timer++;
            } else {
                set_title_palette_black();
                start_soundtest_screen();
            }
            break;
        }
    }
}
