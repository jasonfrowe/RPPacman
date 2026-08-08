#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "player.h"
#include "ghost.h"

// Ghost base sprite frames:
// Red (0):    Up 8..9,   Down 10..11, Left 12..13, Right 14..15  (Base = 8)
// Pink (1):   Up 16..17, Down 18..19, Left 20..21, Right 22..23  (Base = 16)
// Cyan (2):   Up 24..25, Down 26..27, Left 28..29, Right 30..31  (Base = 24)
// Orange (3): Up 32..33, Down 34..35, Left 36..37, Right 38..39  (Base = 32)
static const uint8_t GHOST_BASE_FRAMES[NGHOSTS] = { 8, 16, 24, 32 };

#define GHOST_STATE_HOME_BOUNCE   0
#define GHOST_STATE_MOVING_TO_SLOT 1
#define GHOST_STATE_EXITING_HOUSE  2
#define GHOST_STATE_OUTSIDE        3

// Ghost house exit order: 0 (Red), 1 (Cyan), 2 (Pink), 3 (Orange)
static const uint8_t EXIT_ORDER[4] = { 0, 1, 2, 3 };
static uint8_t s_current_exit_idx = 0;
static uint16_t s_exit_delay_timer = 0;
static bool s_game_motion_started = false;

void ghost_update_motion(void) {
    static uint8_t anim_timer = 0;
    static uint8_t anim_cell = 0;

    // Toggle animation cell (0 or 1) every 8 frames (~7.5 Hz)
    anim_timer++;
    if (anim_timer >= 8) {
        anim_timer = 0;
        anim_cell ^= 1;
    }

    // Check if Pac-Man has started moving to begin ghost house exit sequence
    if (!s_game_motion_started && player.dir != DIR_NONE) {
        s_game_motion_started = true;
        s_exit_delay_timer = 0;
    }

    // --- 1. Manage Active Ghost Exit Queue ---
    if (s_game_motion_started && s_current_exit_idx < 4) {
        uint8_t active_ghost_idx = EXIT_ORDER[s_current_exit_idx];
        ghost_struct *g = &ghosts[active_ghost_idx];

        if (g->state == GHOST_STATE_HOME_BOUNCE) {
            s_exit_delay_timer++;
            if (s_exit_delay_timer >= 32) {
                // Check if ghost needs to move to center exit column (23 * 8 = 184)
                if (g->world_px != (23 * MAZE_TILES_SIZE_PX)) {
                    g->state = GHOST_STATE_MOVING_TO_SLOT;
                } else {
                    g->state = GHOST_STATE_EXITING_HOUSE;
                    g->dir = DIR_UP;
                }
            }
        }
    }

    // --- 2. Update Ghost Positions per State ---
    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];

        if (g->state == GHOST_STATE_HOME_BOUNCE) {
            // Normal vertical bounce in home box
            if (g->dir == DIR_DOWN) {
                g->world_py++;
                if (g->world_py >= g->max_home_py) {
                    g->world_py = g->max_home_py;
                    g->dir = DIR_UP;
                }
            } else if (g->dir == DIR_UP) {
                g->world_py--;
                if (g->world_py <= g->min_home_py) {
                    g->world_py = g->min_home_py;
                    g->dir = DIR_DOWN;
                }
            }
            g->sub_py = g->world_py << 8;
            g->sub_px = g->world_px << 8;
        }
        else if (g->state == GHOST_STATE_MOVING_TO_SLOT) {
            // Move horizontally at 0.5 px/frame to center exit slot column (23 * 8 = 184)
            int16_t target_x = 23 * MAZE_TILES_SIZE_PX;
            if (g->world_px < target_x) {
                g->sub_px += 0x0080; // +0.5 px/frame
                g->dir = DIR_RIGHT;
                if ((g->sub_px >> 8) >= target_x) {
                    g->sub_px = target_x << 8;
                    g->state = GHOST_STATE_EXITING_HOUSE;
                    g->dir = DIR_UP;
                }
            } else if (g->world_px > target_x) {
                g->sub_px -= 0x0080; // -0.5 px/frame
                g->dir = DIR_LEFT;
                if ((g->sub_px >> 8) <= target_x) {
                    g->sub_px = target_x << 8;
                    g->state = GHOST_STATE_EXITING_HOUSE;
                    g->dir = DIR_UP;
                }
            }
            g->world_px = g->sub_px >> 8;
        }
        else if (g->state == GHOST_STATE_EXITING_HOUSE) {
            // Move vertically UP at 0.25 px/frame (0x0040 in 8.8 fixed-point) to tile (23, 12) => py 96
            int16_t target_exit_y = 12 * MAZE_TILES_SIZE_PX; // 96px
            g->dir = DIR_UP;
            g->sub_py -= 0x0040; // -0.25 px/frame
            g->world_py = g->sub_py >> 8;

            if (g->world_py <= target_exit_y) {
                g->world_py = target_exit_y;
                g->sub_py = target_exit_y << 8;
                g->in_house = false;
                g->state = GHOST_STATE_OUTSIDE;
                g->dir = DIR_LEFT; // Default outside initial direction

                // Advance to next ghost in exit queue after 32-frame delay
                s_current_exit_idx++;
                s_exit_delay_timer = 0;
            }
        }

        // --- 3. Sprite Frame Selection ---
        uint8_t base_frame = GHOST_BASE_FRAMES[i];
        uint8_t dir_offset = 0;

        switch (g->dir) {
            case DIR_UP:    dir_offset = 0; break;
            case DIR_DOWN:  dir_offset = 2; break;
            case DIR_LEFT:  dir_offset = 4; break;
            case DIR_RIGHT: dir_offset = 6; break;
            default:        dir_offset = 2; break;
        }

        g->frame = base_frame + dir_offset + anim_cell;

        // Update the ghost sprite XRAM configuration
        unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, g->x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, g->y_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (g->frame * SPRITE_FRAME_SIZE)));
    }
}