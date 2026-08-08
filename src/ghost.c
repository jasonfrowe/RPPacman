#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "player.h"
#include "ghost.h"

// Ghost array index mapping & base sprite frames:
// Ghost 0: Red (Blinky)   -> Base = 8
// Ghost 1: Pink (Pinky)   -> Base = 16
// Ghost 2: Cyan (Inky)    -> Base = 24
// Ghost 3: Orange (Clyde) -> Base = 32
static const uint8_t GHOST_BASE_FRAMES[NGHOSTS] = { 8, 16, 24, 32 };

#define GHOST_STATE_HOME_BOUNCE   0
#define GHOST_STATE_MOVING_TO_SLOT 1
#define GHOST_STATE_EXITING_HOUSE  2
#define GHOST_STATE_OUTSIDE        3

// Ghost house exit order: 0 (Red), 2 (Cyan), 1 (Pink), 3 (Orange)
static const uint8_t EXIT_ORDER[4] = { 0, 2, 1, 3 };
static uint8_t s_current_exit_idx = 0;
static uint16_t s_exit_delay_timer = 0;
static bool s_game_motion_started = false;

static int8_t get_opposite_dir(int8_t dir) {
    switch (dir) {
        case DIR_RIGHT: return DIR_LEFT;
        case DIR_LEFT:  return DIR_RIGHT;
        case DIR_UP:    return DIR_DOWN;
        case DIR_DOWN:  return DIR_UP;
        default:        return DIR_NONE;
    }
}

static void compute_ghost_target_tile(int ghost_index, int16_t *target_tx, int16_t *target_ty) {
    int16_t pac_tx = (int16_t)(player.world_px / MAZE_TILES_SIZE_PX);
    int16_t pac_ty = (int16_t)(player.world_py / MAZE_TILES_SIZE_PX);

    int8_t pac_dx, pac_dy;
    get_dir_offset(player.dir, &pac_dx, &pac_dy);

    switch (ghost_index) {
        case 0: // Red (Blinky): Direct Target -> Pac-Man's exact tile
            *target_tx = pac_tx;
            *target_ty = pac_ty;
            break;

        case 1: // Pink (Pinky): 4 tiles ahead of Pac-Man (with authentic Arcade UP overflow bug)
            *target_tx = pac_tx + (4 * pac_dx);
            *target_ty = pac_ty + (4 * pac_dy);
            if (player.dir == DIR_UP) {
                // Authentic Arcade behavior: Facing UP adds +4 tiles UP (-4 Y) AND +4 tiles LEFT (-4 X)
                *target_tx -= 4;
            }
            break;

        case 2: // Cyan (Inky): Vector target using Blinky's position and 2 tiles ahead of Pac-Man
            {
                int16_t blinky_tx = (int16_t)(ghosts[0].world_px / MAZE_TILES_SIZE_PX);
                int16_t blinky_ty = (int16_t)(ghosts[0].world_py / MAZE_TILES_SIZE_PX);

                int16_t pivot_x = pac_tx + (2 * pac_dx);
                int16_t pivot_y = pac_ty + (2 * pac_dy);

                if (player.dir == DIR_UP) {
                    pivot_x -= 2; // Match Pinky's UP offset behavior for vector pivot
                }

                *target_tx = pivot_x + (pivot_x - blinky_tx);
                *target_ty = pivot_y + (pivot_y - blinky_ty);
            }
            break;

        case 3: // Orange (Clyde): Direct target if distance > 8 tiles; home corner (0, 27) if distance <= 8
            {
                int16_t clyde_tx = (int16_t)(ghosts[3].world_px / MAZE_TILES_SIZE_PX);
                int16_t clyde_ty = (int16_t)(ghosts[3].world_py / MAZE_TILES_SIZE_PX);

                int32_t dx = (int32_t)(clyde_tx - pac_tx);
                int32_t dy = (int32_t)(clyde_ty - pac_ty);
                int32_t dist_sq = (dx * dx) + (dy * dy);

                if (dist_sq > (8 * 8)) {
                    *target_tx = pac_tx;
                    *target_ty = pac_ty;
                } else {
                    // Target home corner: bottom-left tile (0, 27)
                    *target_tx = 0;
                    *target_ty = 27;
                }
            }
            break;
    }
}

static void update_ghost_outside_movement(int ghost_index) {
    ghost_struct *g = &ghosts[ghost_index];

    uint8_t speed_lvl = get_speed_level_index();
    uint16_t speed_fp = SPEED_TABLE[speed_lvl];

    // Blinky (Red - Ghost 0): Arcade "Cruise Elroy" speed boost (+5% speed while chasing)
    if (ghost_index == 0) {
        speed_fp += (speed_fp >> 4); // +5% speed boost (speed_fp * 1.05)
    }

    int8_t dx, dy;
    get_dir_offset(g->dir, &dx, &dy);

    int16_t move_pixels = 0;
    if (dx > 0) {
        g->sub_px += speed_fp;
        move_pixels = g->sub_px >> 8;
        g->sub_px &= 0x00FF;
    } else if (dx < 0) {
        g->sub_px += speed_fp;
        move_pixels = g->sub_px >> 8;
        g->sub_px &= 0x00FF;
    } else if (dy > 0) {
        g->sub_py += speed_fp;
        move_pixels = g->sub_py >> 8;
        g->sub_py &= 0x00FF;
    } else if (dy < 0) {
        g->sub_py += speed_fp;
        move_pixels = g->sub_py >> 8;
        g->sub_py &= 0x00FF;
    }

    for (int16_t step = 0; step < move_pixels; step++) {
        // Move 1px per step along current direction
        get_dir_offset(g->dir, &dx, &dy);
        g->world_px += dx;
        g->world_py += dy;

        // Handle horizontal tunnel wrapping
        if (g->world_px < 0) g->world_px += WORLD_WIDTH;
        else if (g->world_px >= WORLD_WIDTH) g->world_px -= WORLD_WIDTH;

        // Handle vertical tunnel wrapping (using drawn screen Y: world_py + VISUAL_Y_OFFSET)
        int16_t drawn_y = g->world_py - 3;
        if (g->dir == DIR_DOWN && (drawn_y + SPRITE_SIZE_PX) >= 216) {
            g->world_py -= 184; // Moving down: bottom of drawn sprite hits >= 216 -> shift up
        } else if (g->dir == DIR_UP && drawn_y <= 28) {
            g->world_py += 184; // Moving up: top of drawn sprite hits <= 28 -> shift down
        }

        // Evaluate turn decision at 8px grid intersections on every 1px step
        bool at_intersection = (g->world_px % MAZE_TILES_SIZE_PX == 0) &&
                               (g->world_py % MAZE_TILES_SIZE_PX == 0);

        if (at_intersection) {
            int16_t cur_tx = (int16_t)(g->world_px / MAZE_TILES_SIZE_PX);
            int16_t cur_ty = (int16_t)(g->world_py / MAZE_TILES_SIZE_PX);

            int16_t target_tx, target_ty;
            compute_ghost_target_tile(ghost_index, &target_tx, &target_ty);

            int8_t opposite_dir = get_opposite_dir(g->dir);
            int8_t best_dir = g->dir;
            int32_t min_dist_sq = 0x7FFFFFFF;

            // Arcade direction choice priority in case of tie: UP (3), LEFT (1), DOWN (4), RIGHT (2)
            static const int8_t EVAL_DIRS[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };

            for (uint8_t d = 0; d < 4; d++) {
                int8_t test_dir = EVAL_DIRS[d];

                // 180-degree turns prohibited
                if (test_dir == opposite_dir) continue;

                // Check if step in test direction is valid (not a wall)
                if (can_step_dir(g->world_px, g->world_py, test_dir)) {
                    int8_t t_dx, t_dy;
                    get_dir_offset(test_dir, &t_dx, &t_dy);

                    int16_t next_tx = cur_tx + t_dx;
                    int16_t next_ty = cur_ty + t_dy;

                    int32_t diff_x = (int32_t)(next_tx - target_tx);
                    int32_t diff_y = (int32_t)(next_ty - target_ty);
                    int32_t dist_sq = (diff_x * diff_x) + (diff_y * diff_y);

                    if (dist_sq < min_dist_sq) {
                        min_dist_sq = dist_sq;
                        best_dir = test_dir;
                    }
                }
            }

            g->dir = best_dir;
        }
    }
}

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
                g->sub_px = 0;
                g->sub_py = 0;
                g->in_house = false;
                g->state = GHOST_STATE_OUTSIDE;
                g->dir = DIR_LEFT; // Default outside initial direction

                // Advance to next ghost in exit queue after 32-frame delay
                s_current_exit_idx++;
                s_exit_delay_timer = 0;
            }
        }
        else if (g->state == GHOST_STATE_OUTSIDE) {
            update_ghost_outside_movement(i);
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