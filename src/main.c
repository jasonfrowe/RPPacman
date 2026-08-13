#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "input.h"
#include "player.h"
#include "ghost.h"
#include "prizes.h"

typedef enum {
    STATE_TITLE,
    STATE_GAMEPLAY
} game_state_t;

static game_state_t s_game_state = STATE_TITLE;
static uint8_t s_menu_selection = 0; // 0: NORMAL, 1: EXTRAS, 2: OPTION
static uint8_t s_pacman_anim_timer = 0;
static uint8_t s_pacman_anim_frame = 6; // 6 or 7

void hide_all_ghosts(void) {
    for (int i = 0; i < NGHOSTS; i++) {
        ghosts[i].x_pos_px = -32;
        ghosts[i].y_pos_px = -32;
        unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, -32);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, -32);
    }
}

void start_title_screen(void) {
    s_game_state = STATE_TITLE;
    s_menu_selection = 0;

    // 1. Hide maze by setting maze palette to all black & restore title palette
    set_maze_palette_black();
    restore_title_palette();

    // 2. Hide ghost sprites & reset/hide all prize sprites and munchers
    hide_all_ghosts();
    reset_prizes_and_mazes_level();

    // 3. Write menu and header text to TEXT_MAP_CONFIG
    write_text_to_text_map(12, 6,  "PICOCOMPUTER 6502");
    write_text_to_text_map(17, 8,  "NORMAL");
    write_text_to_text_map(17, 9,  "EXTRA ");
    write_text_to_text_map(17, 10, "OPTIONS");

    // 4. Position Pac-Man sprite to the left of "NORMAL" (117px, y offset +1)
    int16_t cursor_x = 117;
    int16_t cursor_y = (8 + s_menu_selection) * 16 + 1;

    player.x_pos_px = cursor_x;
    player.y_pos_px = cursor_y;
    player.frame = 6;
    s_pacman_anim_timer = 0;
    s_pacman_anim_frame = 6;

    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
}

void start_normal_game(void) {
    s_game_state = STATE_GAMEPLAY;

    // 1. Clear menu text from TEXT_MAP_CONFIG & hide title screen (set TITLE_PALETTE_ADDR to all black 0x0000)
    write_text_to_text_map(12, 6,  "                 ");
    write_text_to_text_map(17, 8,  "       ");
    write_text_to_text_map(17, 9,  "       ");
    write_text_to_text_map(17, 10, "       ");
    set_title_palette_black();

    // 2. Restore maze palette
    restore_maze_palette();

    // 3. Reset ghosts to initial positions and restore sprite visibility
    reset_ghosts_to_initial_positions();
    reset_prizes_and_mazes_level();

    // 4. Reset player attributes and position to initial start (23, 21)
    player.world_px = 23 * MAZE_TILES_SIZE_PX; // 184
    player.world_py = 21 * MAZE_TILES_SIZE_PX; // 168
    player.x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    player.y_pos_px = player.world_py - 3;
    player.score = 0;
    player.pellets_eaten = 0;
    player.lives = 3;
    player.dir = DIR_NONE;
    clear_player_queued_dir();
    player.frame = 5; // Facing left / closed

    update_player_score_display(player.score);
    update_player_lives_display(player.lives);
    reset_game_timer();

    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
}

static bool init_graphics(void)
{
    // 320×240 canvas
    int rc;
    rc = xreg_vga_canvas(1);
    if (rc < 0) {
        return false;
    }

    // Initialize tile mode 2 (8×8 tiles, 8-bit color index)
    tile_mode2_init();
    init_ghost_data();
    sprite_mode5_init();
    tile_mode2_text_map_init();
    tile_mode2_title_map_init();
    init_side_pellet_counters();

    return true;
}

uint8_t vsync_last = 0;
uint8_t frame = 0;

int main(void)
{
    puts("Hello from RPPacMan!");

    input_actions_t actions;
    input_init();

    // Initialise graphics
    if (!init_graphics()) {
        return 1;
    }

    // Start on Title Screen
    start_title_screen();

    static bool prev_up = false;
    static bool prev_down = false;
    static bool prev_action = false;

    // Main loop
    while (true) {
        // 1. INPUT
        input_poll(&actions);

        bool press_up = (actions.up && !prev_up);
        bool press_down = (actions.down && !prev_down);
        // Use gamepad 'A' button (mapped to actions.bomb) to start the game
        bool press_action = (actions.bomb && !prev_action);

        prev_up = actions.up;
        prev_down = actions.down;
        prev_action = actions.bomb;

        if (s_game_state == STATE_TITLE) {
            // Menu Navigation
            if (press_up) {
                if (s_menu_selection > 0) {
                    s_menu_selection--;
                } else {
                    s_menu_selection = 2;
                }
            } else if (press_down) {
                if (s_menu_selection < 2) {
                    s_menu_selection++;
                } else {
                    s_menu_selection = 0;
                }
            }

            // Select Menu Option
            if (press_action) {
                if (s_menu_selection == 0) {
                    // "NORMAL" selected -> Start Gameplay!
                    start_normal_game();
                }
            }

            if (s_game_state == STATE_TITLE) {
                // Animate Pac-Man cursor sprite (tiles 6 and 7, 4 frames each)
                s_pacman_anim_timer++;
                if (s_pacman_anim_timer >= 4) {
                    s_pacman_anim_timer = 0;
                    s_pacman_anim_frame = (s_pacman_anim_frame == 6) ? 7 : 6;
                }

                // Position Pac-Man cursor to the left of the selected menu item (117px, y offset +1)
                player.x_pos_px = 117;
                player.y_pos_px = (8 + s_menu_selection) * 16 + 1;
                player.frame = s_pacman_anim_frame;

                xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
                xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
                xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));

                // Keep ghosts hidden during Title Screen
                hide_all_ghosts();
            }
        } else {
            // 2. STATE_GAMEPLAY Updates
            player_update_motion(&actions);
            ghost_update_motion();
            prize_update_motion();
            update_maze_munchers_animation();
            update_lives_blink_animation();
            update_game_timer_display();

            if (is_game_timer_expired()) {
                // 5 minute timer expired -> End game and return to Title Screen
                start_title_screen();
            } else {
                // Update maze palette animation
                tile_mode2_palette_update(frame);
            }
        }

        // 4. SYNC (Wait for VSYNC)
        // Spin here until the hardware vsync register changes
        while (RIA.vsync == vsync_last) {
            // Do nothing, just wait
        }
        
        // Update our tracker and frame counter
        vsync_last = RIA.vsync;
        
        frame++;
        if (frame >= 60) frame = 0;
    }

    return 0;
}
