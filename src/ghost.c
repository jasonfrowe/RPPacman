#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "player.h"
#include "ghost.h"

// Ghost base sprite frames (normal chase mode):
// Ghost 0: Red (Blinky)   -> Base 8 (Up 8..9, Down 10..11, Left 12..13, Right 14..15)
// Ghost 1: Pink (Pinky)   -> Base 16 (Up 16..17, Down 18..19, Left 20..21, Right 22..23)
// Ghost 2: Cyan (Inky)    -> Base 24 (Up 24..25, Down 26..27, Left 28..29, Right 30..31)
// Ghost 3: Orange (Clyde) -> Base 32 (Up 32..33, Down 34..35, Left 36..37, Right 38..39)
static const uint8_t GHOST_BASE_FRAMES[NGHOSTS] = { 8, 16, 24, 32 };

#define GHOST_STATE_HOME_BOUNCE    0
#define GHOST_STATE_MOVING_TO_SLOT 1
#define GHOST_STATE_EXITING_HOUSE   2
#define GHOST_STATE_OUTSIDE         3

#define GHOST_MODE_CHASE        0
#define GHOST_MODE_FRIGHTENED   1
#define GHOST_MODE_EATEN        2

// 4 Queue Slot Positions inside/around home:
// Slot 0: Tile (23, 14) => World (184, 112) [Exit/Top Slot]
// Slot 1: Tile (23, 16) => World (184, 128) [Bottom Slot]
// Slot 2: Tile (21, 15) => World (168, 120) [Left Wing Slot]
// Slot 3: Tile (25, 15) => World (200, 120) [Right Wing Slot]
static const int16_t SLOT_X[4] = { 23 * 8, 23 * 8, 21 * 8, 25 * 8 };
static const int16_t SLOT_Y[4] = { 14 * 8, 16 * 8, 15 * 8, 15 * 8 };

// Initial game start exit order: Red (0), Pink (1), Cyan (2), Orange (3)
static const uint8_t INITIAL_EXIT_ORDER[4] = { 0, 1, 2, 3 };

static uint16_t s_exit_delay_timer = 0;
static bool s_game_motion_started = false;

// Power Pellet Frightened State Tracking
static uint16_t s_frightened_timer = 0;
static uint16_t s_frightened_max_duration = 0;
static uint16_t s_ghosts_eaten_chain = 0; // Continuous combo counter across Power Pellets!

// Level-scaled Power Pellet durations in 60 FPS frames (Cherry 6.0s -> Crown 1.0s)
static const uint16_t FRIGHTENED_DURATION_TABLE[22] = {
    360, 345, 330, 315, 300, 285, 270, 255, 240, 225,
    210, 195, 180, 165, 150, 135, 120, 105,  90,  80,  70,  60
};

static int8_t get_opposite_dir(int8_t dir) {
    switch (dir) {
        case DIR_RIGHT: return DIR_LEFT;
        case DIR_LEFT:  return DIR_RIGHT;
        case DIR_UP:    return DIR_DOWN;
        case DIR_DOWN:  return DIR_UP;
        default:        return DIR_NONE;
    }
}

// Triggered when Pac-Man eats a Super / Power Pellet
void trigger_power_pellet_frightened(void) {
    uint8_t speed_lvl = get_speed_level_index();
    if (speed_lvl > 21) speed_lvl = 21;

    s_frightened_max_duration = FRIGHTENED_DURATION_TABLE[speed_lvl];
    s_frightened_timer = s_frightened_max_duration;
    // NOTE: s_ghosts_eaten_chain is NOT reset here! Combo chain continues across pellets until timer expires.

    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];
        if (g->mode != GHOST_MODE_EATEN) {
            g->mode = GHOST_MODE_FRIGHTENED;
            if (!g->in_house && g->state == GHOST_STATE_OUTSIDE) {
                g->dir = get_opposite_dir(g->dir); // Reverse direction outside when frightened
            }
        }
    }
}

// Check collision between Pac-Man and ghosts
void check_pacman_ghost_collisions(void) {
    int16_t pm_center_x = player.world_px + 8;
    int16_t pm_center_y = player.world_py + 8;

    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];
        
        // Ghost must be active (outside or just reached tile 23,12) and in FRIGHTENED mode
        if (g->mode == GHOST_MODE_FRIGHTENED && g->state == GHOST_STATE_OUTSIDE) {
            int16_t ghost_center_x = g->world_px + 8;
            int16_t ghost_center_y = g->world_py + 8;

            int16_t dx = pm_center_x - ghost_center_x;
            int16_t dy = pm_center_y - ghost_center_y;

            if (dx >= -6 && dx <= 6 && dy >= -6 && dy <= 6) {
                // Pac-Man eats the frightened ghost!
                g->mode = GHOST_MODE_EATEN;
                s_ghosts_eaten_chain++;

                // Scoring table from README.md:
                // 1st: 400, 2nd: 800, 3rd: 1200, 4th: 1600, 5th: 2000, 6th: 2400, 7th: 2800, 8th+: 3200
                uint32_t pts = 0;
                if (s_ghosts_eaten_chain == 1)      pts = 400;
                else if (s_ghosts_eaten_chain == 2) pts = 800;
                else if (s_ghosts_eaten_chain == 3) pts = 1200;
                else if (s_ghosts_eaten_chain == 4) pts = 1600;
                else if (s_ghosts_eaten_chain == 5) pts = 2000;
                else if (s_ghosts_eaten_chain == 6) pts = 2400;
                else if (s_ghosts_eaten_chain == 7) pts = 2800;
                else                                pts = 3200;

                player.score += pts;
                update_player_score_display(player.score);
            }
        }
    }
}

// Compute Arcade target tile for ghosts in normal CHASE mode
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
                    pivot_x -= 2;
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
                    *target_tx = 0;
                    *target_ty = 27;
                }
            }
            break;
    }
}

// Update ghost movement when OUTSIDE in the maze
static void update_ghost_outside_movement(int ghost_index) {
    ghost_struct *g = &ghosts[ghost_index];

    uint8_t speed_lvl = get_speed_level_index();
    uint16_t speed_fp = SPEED_TABLE[speed_lvl]; // Full normal level speed for ghosts in normal CHASE mode

    if (g->mode == GHOST_MODE_FRIGHTENED) {
        // Vulnerable ghosts move at 0.25x speed (1/4 of normal speed)
        speed_fp = speed_fp >> 2;
    } else if (g->mode == GHOST_MODE_EATEN) {
        // Eaten eyes move at 2.0x speed to return home quickly
        speed_fp = speed_fp * 2;
    } else if (ghost_index == 0) {
        // Blinky Cruise Elroy speed boost (+5% speed while chasing)
        speed_fp += (speed_fp >> 4);
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
        get_dir_offset(g->dir, &dx, &dy);
        g->world_px += dx;
        g->world_py += dy;

        // Handle horizontal tunnel wrapping
        if (g->world_px < 0) g->world_px += WORLD_WIDTH;
        else if (g->world_px >= WORLD_WIDTH) g->world_px -= WORLD_WIDTH;

        // Handle vertical tunnel wrapping
        int16_t drawn_y = g->world_py - 3;
        if (g->dir == DIR_DOWN && (drawn_y + SPRITE_SIZE_PX) >= 216) {
            g->world_py -= 184;
        } else if (g->dir == DIR_UP && drawn_y <= 28) {
            g->world_py += 184;
        }

        // Check if eaten eyes reached home entrance tile (23, 12) => world (184, 96)
        if (g->mode == GHOST_MODE_EATEN) {
            int16_t door_x = 23 * MAZE_TILES_SIZE_PX; // 184
            int16_t door_y = 12 * MAZE_TILES_SIZE_PX; // 96

            if (g->world_px == door_x && g->world_py == door_y) {
                // Find highest priority free slot in home queue:
                // Priority: Slot 0 (23,14), Slot 1 (23,16), Slot 2 (21,15), Slot 3 (25,15)
                int target_slot = 0;
                for (int s = 0; s < 4; s++) {
                    bool occupied = false;
                    for (int o = 0; o < NGHOSTS; o++) {
                        if (o != ghost_index && ghosts[o].in_house && ghosts[o].queue_slot == s) {
                            occupied = true;
                            break;
                        }
                    }
                    if (!occupied) {
                        target_slot = s;
                        break;
                    }
                }

                g->in_house = true;
                g->queue_slot = target_slot;
                g->state = GHOST_STATE_MOVING_TO_SLOT;
                g->dir = DIR_DOWN;
                g->sub_px = g->world_px << 8;
                g->sub_py = g->world_py << 8;
                break;
            }
        }

        // Evaluate intersection turn decisions
        bool at_intersection = (g->world_px % MAZE_TILES_SIZE_PX == 0) &&
                               (g->world_py % MAZE_TILES_SIZE_PX == 0);

        if (at_intersection) {
            int16_t cur_tx = (int16_t)(g->world_px / MAZE_TILES_SIZE_PX);
            int16_t cur_ty = (int16_t)(g->world_py / MAZE_TILES_SIZE_PX);

            int16_t target_tx, target_ty;
            if (g->mode == GHOST_MODE_EATEN) {
                // Eaten eyes target home door tile (23, 12)
                target_tx = 23;
                target_ty = 12;
            } else if (g->mode == GHOST_MODE_FRIGHTENED) {
                // Frightened ghosts path pseudo-randomly / away from Pac-Man
                // We pick target away from Pac-Man
                int16_t pac_tx = (int16_t)(player.world_px / MAZE_TILES_SIZE_PX);
                int16_t pac_ty = (int16_t)(player.world_py / MAZE_TILES_SIZE_PX);
                target_tx = cur_tx + (cur_tx - pac_tx);
                target_ty = cur_ty + (cur_ty - pac_ty);
            } else {
                compute_ghost_target_tile(ghost_index, &target_tx, &target_ty);
            }

            int8_t opposite_dir = get_opposite_dir(g->dir);
            int8_t best_dir = g->dir;
            int32_t min_dist_sq = 0x7FFFFFFF;

            static const int8_t EVAL_DIRS[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };

            for (uint8_t d = 0; d < 4; d++) {
                int8_t test_dir = EVAL_DIRS[d];
                if (test_dir == opposite_dir) continue;

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

    anim_timer++;
    if (anim_timer >= 8) {
        anim_timer = 0;
        anim_cell ^= 1;
    }

    // --- Power Pellet Frightened Timer Management ---
    if (s_frightened_timer > 0) {
        s_frightened_timer--;
        if (s_frightened_timer == 0) {
            // Frightened timer expired! Reset combo chain and return ghosts to CHASE mode
            s_ghosts_eaten_chain = 0;
            for (int i = 0; i < NGHOSTS; i++) {
                if (ghosts[i].mode == GHOST_MODE_FRIGHTENED) {
                    ghosts[i].mode = GHOST_MODE_CHASE;
                }
            }
        }
    }

    // Start game motion when player first inputs a direction
    if (!s_game_motion_started && player.dir != DIR_NONE) {
        s_game_motion_started = true;
        s_exit_delay_timer = 0;
    }

    // Check collisions with active ghosts
    check_pacman_ghost_collisions();

    // --- 1. Queue Promotion System ---
    // Ghosts progress through queue slots: 3 (25,15) -> 2 (21,15) -> 1 (23,16) -> 0 (23,14)
    // If a lower slot index is empty, a ghost in a higher slot index shifts toward it.
    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];
        if (g->in_house && g->state == GHOST_STATE_HOME_BOUNCE) {
            if (g->queue_slot > 0) {
                int target_slot = g->queue_slot - 1;
                // Check if target_slot is free
                bool occupied = false;
                for (int o = 0; o < NGHOSTS; o++) {
                    if (o != i && ghosts[o].in_house && ghosts[o].queue_slot == target_slot) {
                        occupied = true;
                        break;
                    }
                }
                if (!occupied) {
                    // Promote to lower slot index and move towards it
                    g->queue_slot = target_slot;
                    g->state = GHOST_STATE_MOVING_TO_SLOT;
                }
            }
        }
    }

    // --- 2. Manage Queue Exits (Slot 0 Exits Home) ---
    if (s_game_motion_started) {
        // Check if any ghost is currently moving to slot or exiting house
        bool exit_in_progress = false;
        for (int i = 0; i < NGHOSTS; i++) {
            if (ghosts[i].state == GHOST_STATE_MOVING_TO_SLOT || ghosts[i].state == GHOST_STATE_EXITING_HOUSE) {
                exit_in_progress = true;
                break;
            }
        }

        if (!exit_in_progress) {
            // Find ghost occupying Slot 0
            for (int i = 0; i < NGHOSTS; i++) {
                ghost_struct *g = &ghosts[i];
                if (g->in_house && g->queue_slot == 0 && g->state == GHOST_STATE_HOME_BOUNCE) {
                    s_exit_delay_timer++;
                    if (s_exit_delay_timer >= 32) {
                        s_exit_delay_timer = 0;
                        g->state = GHOST_STATE_EXITING_HOUSE;
                        g->dir = DIR_UP;
                    }
                    break;
                }
            }
        }
    }

    // --- 3. Update Ghost Positions per State ---
    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];

        if (g->state == GHOST_STATE_HOME_BOUNCE) {
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
            // Move toward assigned queue_slot coordinate
            int16_t target_x = SLOT_X[g->queue_slot];
            int16_t target_y = SLOT_Y[g->queue_slot];

            if (g->world_px < target_x) {
                g->dir = DIR_RIGHT;
                g->world_px++;
            } else if (g->world_px > target_x) {
                g->dir = DIR_LEFT;
                g->world_px--;
            } else if (g->world_py < target_y) {
                g->dir = DIR_DOWN;
                g->world_py++;
            } else if (g->world_py > target_y) {
                g->dir = DIR_UP;
                g->world_py--;
            } else {
                // Reached target slot! Transition back to bouncing
                g->state = GHOST_STATE_HOME_BOUNCE;
                g->min_home_py = 14 * MAZE_TILES_SIZE_PX;
                g->max_home_py = 16 * MAZE_TILES_SIZE_PX;
                g->dir = DIR_DOWN;

                if (g->mode == GHOST_MODE_EATEN) {
                    // Reset mode: ghost body respawns and returns to normal CHASE state (even if Power Pellet is active)
                    g->mode = GHOST_MODE_CHASE;
                }
            }
            g->sub_px = g->world_px << 8;
            g->sub_py = g->world_py << 8;
        }
        else if (g->state == GHOST_STATE_EXITING_HOUSE) {
            int16_t target_exit_y = 12 * MAZE_TILES_SIZE_PX; // 96px
            g->dir = DIR_UP;
            g->sub_py -= 0x0040; // 0.25 px/frame exit speed
            g->world_py = g->sub_py >> 8;

            if (g->world_py <= target_exit_y) {
                g->world_py = target_exit_y;
                g->sub_px = 0;
                g->sub_py = 0;
                g->in_house = false;
                g->queue_slot = -1; // No longer in home queue
                g->state = GHOST_STATE_OUTSIDE;
                g->dir = DIR_LEFT;

                s_exit_delay_timer = 0;
            }
        }
        else if (g->state == GHOST_STATE_OUTSIDE) {
            update_ghost_outside_movement(i);
        }

        // --- 4. Sprite Frame Selection ---
        if (g->mode == GHOST_MODE_FRIGHTENED) {
            // Flashing cadence increases as timer nears the end
            // Last 180 frames (3 seconds):
            // - 180..121 frames: slow flash (every 16 frames)
            // - 120..61 frames:  medium flash (every 8 frames)
            // - 60..1 frames:    fast flash (every 4 frames)
            bool show_white = false;
            if (s_frightened_timer <= 60) {
                show_white = ((s_frightened_timer % 8) < 4);
            } else if (s_frightened_timer <= 120) {
                show_white = ((s_frightened_timer % 16) < 8);
            } else if (s_frightened_timer <= 180) {
                show_white = ((s_frightened_timer % 32) < 16);
            }

            if (show_white) {
                g->frame = 42 + anim_cell; // White flashing frames (42, 43)
            } else {
                g->frame = 40 + anim_cell; // Vulnerable blue frames (40, 41)
            }
        }
        else if (g->mode == GHOST_MODE_EATEN) {
            // Eyes only sprite frames: 44 (UP), 45 (DOWN), 46 (LEFT), 47 (RIGHT)
            switch (g->dir) {
                case DIR_UP:    g->frame = 44; break;
                case DIR_DOWN:  g->frame = 45; break;
                case DIR_LEFT:  g->frame = 46; break;
                case DIR_RIGHT: g->frame = 47; break;
                default:        g->frame = 45; break;
            }
        }
        else {
            // Normal chase mode directional sprites
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
        }

        // Update ghost sprite configuration in XRAM
        unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, g->x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, g->y_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (g->frame * SPRITE_FRAME_SIZE)));
    }
}