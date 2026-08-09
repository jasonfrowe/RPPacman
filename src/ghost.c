#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
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
#define GHOST_STATE_ENTERING_HOUSE  4

#define GHOST_MODE_CHASE        0
#define GHOST_MODE_FRIGHTENED   1
#define GHOST_MODE_EATEN        2

// Designated starting positions and bounce bounds per ghost:
// Ghost 0 (Red / Blinky):   start (23, 14) -> X=184, Y=112..128
// Ghost 1 (Pink / Pinky):   start (23, 16) -> X=184, Y=112..128
// Ghost 2 (Cyan / Inky):    start (21, 15) -> X=168, Y=112..128
// Ghost 3 (Orange / Clyde): start (25, 15) -> X=200, Y=112..128
static const int16_t GHOST_HOME_X[4] = { 23 * 8, 23 * 8, 21 * 8, 25 * 8 };
static const int16_t GHOST_MIN_Y[4]  = { 14 * 8, 14 * 8, 14 * 8, 14 * 8 };
static const int16_t GHOST_MAX_Y[4]  = { 16 * 8, 16 * 8, 16 * 8, 16 * 8 };

// Initial game exit order: 0 (Blinky), 1 (Pinky), 2 (Inky), 3 (Clyde)
static const uint8_t INITIAL_EXIT_ORDER[4] = { 0, 1, 2, 3 };
static uint8_t s_initial_exit_idx = 0;

// FIFO queue array for eaten ghosts returning home (capacity 4)
static int8_t s_fifo_queue[4] = { -1, -1, -1, -1 };
static uint8_t s_fifo_count = 0;

static uint16_t s_exit_delay_timer = 0;
static bool s_game_motion_started = false;
static uint8_t s_eat_pause_timer = 0; // Freeze movement during 30-frame eat animation pause

bool is_eat_pause_active(void) {
    return s_eat_pause_timer > 0;
}

// Sparkle offsets relative to Pac-Man's drawn position for frames 1..10
typedef struct {
    int8_t dx;
    int8_t dy;
} sparkle_pos_t;

static const sparkle_pos_t SPARKLE_ANIM_POS[10][NSPARKLES] = {
    // Frame 1
    { {-20, 10}, {-9, -1},  {-8, 11},  {3, 2},   {4, -7},   {17, -19} },
    // Frame 2
    { {-30, 17}, {-17, 1},  {-16, 3},  {7, 8},   {9, -10},  {27, -26} },
    // Frame 3
    { {-40, 23}, {-25, 3},  {-24, -5}, {11, 14}, {14, -13}, {37, -33} },
    // Frame 4
    { {-45, 30},  {-29, 4},  {-28, -9}, {13, 17}, {16, 14},  {42, -40} },
    // Frame 5
    { {-50, 37},  {-33, 5},  {-32, -13},{15, 20}, {18, -16}, {47, -44} },
    // Frame 6
    { {-52, 44},  {-35, 5},  {-34, -15},{16, 21}, {19, -16}, {52, -46} },
    // Frame 7
    { {-54, 51},  {-35, 5},  {-34, -15},{16, 21}, {19, -16}, {54, -48}  },
    // Frame 8
    { {-54, 51},  {-35, 5},  {-34, -15},{16, 21}, {19, -16}, {54, -48}  },
    // Frame 9
    { {-54, 51},  {-35, 5},  {-34, -15},{16, 21}, {19, -16}, {54, -48}  },
    // Frame 10
    { {-54, 51},  {-35, 5},  {-34, -15},{16, 21}, {19, -16}, {54, -48}  }
};

// Score digit offsets relative to Pac-Man's drawn position for frames 1..10
typedef struct {
    int8_t dx;
    int8_t dy;
} score_digit_pos_t;

static const score_digit_pos_t SCORE_DIGIT_ANIM_POS[10][4] = {
    // Frame 1
    { {1, 0},   {3, 0},   {5, 0},   {7, 0}   },
    // Frame 2
    { {0, -8},  {3, -8},  {5, -8},  {8, -8}  },
    // Frame 3
    { {-2, -12},{2, -12}, {6, -12}, {10, -12}},
    // Frame 4
    { {-4, -16},{2, -16}, {6, -16}, {12, -16}},
    // Frame 5
    { {-6, -18},{1, -18}, {7, -18}, {14, -18}},
    // Frame 6
    { {-7, -20},{1, -20}, {7, -20}, {15, -20}},
    // Frame 7
    { {-7, -21},{1, -21}, {7, -21}, {15, -21}},
    // Frame 8
    { {-7, -22},{1, -22}, {7, -22}, {15, -22}},
    // Frame 9
    { {-7, -23},{1, -23}, {7, -23}, {15, -23}},
    // Frame 10
    { {-8, -24},{0, -24}, {8, -24}, {16, -24}}
};

// Eaten ghost score animation tracking structure for single score display slot
typedef struct {
    bool active;
    uint8_t ghost_index;
    uint8_t anim_frame;        // 1..120
    int16_t pacman_world_x;     // Pac-Man's world X when ghost was eaten
    int16_t pacman_world_y;     // Pac-Man's world Y when ghost was eaten
    uint8_t digits[4];         // 4 sprite frame indices (107..116 for digits 0..9)
} eaten_score_anim_t;

static eaten_score_anim_t s_single_score_anim;

// Current active eat animation state (when s_eat_pause_timer > 0)
static uint8_t s_active_eat_anim_ghost = 0;

void trigger_eaten_ghost_animation(uint8_t ghost_index, uint32_t pts) {
    s_active_eat_anim_ghost = ghost_index;

    // Use single score display slot. Any existing score display is overwritten by the new one.
    eaten_score_anim_t *sa = &s_single_score_anim;
    sa->active = true;
    sa->ghost_index = ghost_index;
    sa->anim_frame = 1;
    sa->pacman_world_x = player.world_px;
    sa->pacman_world_y = player.world_py;

    // Convert pts (e.g. 400, 800, 1200, 1600, 2000, 2400, 2800, 3200) into 4 digit sprite tile indices
    // Numbers 1..9, 0 use tile indices 107..116 (1->107, 2->108, ..., 9->115, 0->116).
    // Blank leading zeros using tile index 48 so 800 renders as ' 800' instead of '0800'.
    uint16_t thousands = (pts / 1000) % 10;
    uint16_t hundreds  = (pts / 100) % 10;
    uint16_t tens      = (pts / 10) % 10;
    uint16_t ones      = pts % 10;

    sa->digits[0] = (thousands == 0) ? 48 : (106 + thousands);
    sa->digits[1] = (thousands == 0 && hundreds == 0) ? 48 : ((hundreds == 0) ? 116 : (106 + hundreds));
    sa->digits[2] = (thousands == 0 && hundreds == 0 && tens == 0) ? 48 : ((tens == 0) ? 116 : (106 + tens));
    sa->digits[3] = (ones == 0) ? 116 : (106 + ones);
}

// Power Pellet Frightened State Tracking
static uint16_t s_frightened_timer = 0;
static uint16_t s_frightened_max_duration = 0;
static uint16_t s_ghosts_eaten_chain = 0; // Continuous combo counter across Power Pellets!

// Level-scaled Power Pellet total durations in 60 FPS frames (Level 0 Cherry: 600 frames / 10.0s -> Level 21 Crown: 240 frames / 4.0s)
static const uint16_t FRIGHTENED_DURATION_TABLE[22] = {
    600, 583, 566, 549, 531, 514, 497, 480, 463, 446,
    429, 411, 394, 377, 360, 343, 326, 309, 291, 274, 257, 240
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

                // Set 30-frame pause for all motion & trigger eat animation
                s_eat_pause_timer = 30;
                trigger_eaten_ghost_animation(i, pts);
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

        // Step 1 of eaten return: Eyes reach home entrance door tile (23, 12) => world (184, 96)
        if (g->mode == GHOST_MODE_EATEN) {
            int16_t door_x = 23 * MAZE_TILES_SIZE_PX; // 184
            int16_t door_y = 12 * MAZE_TILES_SIZE_PX; // 96

            if (g->world_px == door_x && g->world_py == door_y) {
                // Transition to GHOST_STATE_ENTERING_HOUSE to descend into house down to row 16 (128px)
                g->in_house = true;
                g->state = GHOST_STATE_ENTERING_HOUSE;
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
                // Step 1: Eaten eyes target home door tile (23, 12) from outside
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

            // Check if ghost is in vertical tunnel regions (< 32px or > 215px drawn Y)
            bool is_in_vertical_tunnel = (drawn_y < 32) || ((drawn_y + SPRITE_SIZE_PX) > 215);

            for (uint8_t d = 0; d < 4; d++) {
                int8_t test_dir = EVAL_DIRS[d];
                if (test_dir == opposite_dir) continue;

                // Ignore left/right turn evaluation when in vertical tunnel
                if (is_in_vertical_tunnel && (test_dir == DIR_LEFT || test_dir == DIR_RIGHT)) {
                    continue;
                }

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

void check_and_reset_stuck_ghosts(void) {
    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];
        if (g->in_house || g->state != GHOST_STATE_OUTSIDE) continue;

        uint16_t tx = (uint16_t)(g->world_px / MAZE_TILES_SIZE_PX);
        uint16_t ty = (uint16_t)(g->world_py / MAZE_TILES_SIZE_PX);

        if (tx >= MAZE_MAP_WIDTH || ty >= MAZE_MAP_HEIGHT) continue;

        uint16_t offset = ty * MAZE_MAP_WIDTH + tx;
        RIA.addr0 = MAZE_MAP_DATA + offset;
        RIA.step0 = 1;
        uint8_t tile_val = RIA.rw0;

        // Check if ghost is on an invalid wall tile (1..115 or 126..127)
        if ((tile_val >= 1 && tile_val <= 115) || (tile_val >= 126 && tile_val <= 127)) {
            // Reset ghost back to home base position, preserving current mode (e.g. FRIGHTENED stays FRIGHTENED)
            g->world_px = GHOST_HOME_X[i];
            g->world_py = GHOST_MAX_Y[i]; // Row 16 (128px)
            g->sub_px = g->world_px << 8;
            g->sub_py = g->world_py << 8;
            g->bounce_dist_px = 0;
            g->in_house = true;
            g->state = GHOST_STATE_HOME_BOUNCE;
            g->dir = DIR_UP;

            // Enqueue ghost into return queue so it exits home sequentially
            bool already_queued = false;
            for (uint8_t f = 0; f < s_fifo_count; f++) {
                if (s_fifo_queue[f] == i) {
                    already_queued = true;
                    break;
                }
            }
            if (!already_queued && s_fifo_count < 4) {
                s_fifo_queue[s_fifo_count++] = i;
            }
        }
    }
}

void ghost_update_motion(void) {
    if (s_eat_pause_timer > 0) {
        s_eat_pause_timer--;
        // Bypass movement and collision updates during pause, but continue to section 3 to render sparkles & scores!
        goto render_sprites;
    }

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
            s_ghosts_eaten_chain = 0;
            for (int i = 0; i < NGHOSTS; i++) {
                if (ghosts[i].mode == GHOST_MODE_FRIGHTENED) {
                    ghosts[i].mode = GHOST_MODE_CHASE;
                }
            }
        }
    }

    // Start game motion when player first inputs a direction
    if (!s_game_motion_started) {
        if (player.dir != DIR_NONE) {
            s_game_motion_started = true;
            s_exit_delay_timer = 0;
        } else {
            // Ghosts remain completely stationary until Pac-Man begins moving!
            return;
        }
    }

    // Check collisions with active ghosts
    check_pacman_ghost_collisions();

    // --- 1. Update Ghost Positions per State ---
    // Home bounce speed: 0.85x of Pac-Man's level base speed
    uint8_t speed_lvl = get_speed_level_index();
    uint16_t home_speed_fp = (SPEED_TABLE[speed_lvl] * 85) / 100;

    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];

        if (g->state == GHOST_STATE_HOME_BOUNCE) {
            // Bounce vertically between designated min_home_py (row 14) and max_home_py (row 16) at 0.85x speed
            uint16_t move_px = 0;
            if (g->dir == DIR_DOWN) {
                g->sub_py += home_speed_fp;
                move_px = g->sub_py >> 8;
                g->sub_py &= 0x00FF;
                for (uint16_t step = 0; step < move_px; step++) {
                    g->world_py++;
                    g->bounce_dist_px++;
                    if (g->world_py >= g->max_home_py) {
                        g->world_py = g->max_home_py;
                        g->dir = DIR_UP;
                        break;
                    }
                }
            } else if (g->dir == DIR_UP) {
                g->sub_py += home_speed_fp;
                move_px = g->sub_py >> 8;
                g->sub_py &= 0x00FF;
                for (uint16_t step = 0; step < move_px; step++) {
                    g->world_py--;
                    g->bounce_dist_px++;
                    if (g->world_py <= g->min_home_py) {
                        g->world_py = g->min_home_py;
                        g->dir = DIR_DOWN;
                        break;
                    }
                }
            }
            g->sub_px = g->world_px << 8;
        }
        else if (g->state == GHOST_STATE_MOVING_TO_SLOT) {
            // Move horizontally along row 14 to center exit column (23, 14) => 184px at base speed (1 px/frame)
            int16_t target_x = 23 * MAZE_TILES_SIZE_PX;

            if (g->world_px < target_x) {
                g->dir = DIR_RIGHT;
                g->world_px++;
            } else if (g->world_px > target_x) {
                g->dir = DIR_LEFT;
                g->world_px--;
            } else {
                // Reached center column (23, 14)! Transition to ascending vertically
                g->state = GHOST_STATE_EXITING_HOUSE;
                g->dir = DIR_UP;
                g->sub_py = g->world_py << 8;
            }
            g->sub_px = g->world_px << 8;
            g->sub_py = g->world_py << 8;
        }
        else if (g->state == GHOST_STATE_ENTERING_HOUSE) {
            // Eaten return Step 2: Eyes descend from (23, 12) to row 16 (23, 16) at 2.0x speed
            int16_t row16_y = 16 * MAZE_TILES_SIZE_PX; // 128px
            int16_t home_x = GHOST_HOME_X[i];
            uint16_t enter_speed_fp = SPEED_TABLE[speed_lvl] * 2; // 2.0x speed for eaten eyes

            g->dir = DIR_DOWN;
            g->sub_py += enter_speed_fp;
            g->world_py = g->sub_py >> 8;

            if (g->world_py >= row16_y) {
                g->world_py = row16_y;
                g->sub_py = row16_y << 8;

                // Eaten return Step 3: Move horizontally to designated spawn X column
                if (g->world_px < home_x) {
                    g->dir = DIR_RIGHT;
                    g->world_px++;
                    g->sub_px = g->world_px << 8;
                } else if (g->world_px > home_x) {
                    g->dir = DIR_LEFT;
                    g->world_px--;
                    g->sub_px = g->world_px << 8;
                } else {
                    // Reached home spawn position (home_x, 16)! Restore normal CHASE mode & start home bounce facing DIR_UP
                    g->mode = GHOST_MODE_CHASE;
                    g->state = GHOST_STATE_HOME_BOUNCE;
                    g->dir = DIR_UP;
                    g->bounce_dist_px = 0; // Reset 80px cooldown tracker

                    // Enqueue ghost into FIFO return queue if not already queued
                    bool already_queued = false;
                    for (uint8_t f = 0; f < s_fifo_count; f++) {
                        if (s_fifo_queue[f] == i) {
                            already_queued = true;
                            break;
                        }
                    }
                    if (!already_queued && s_fifo_count < 4) {
                        s_fifo_queue[s_fifo_count++] = i;
                    }
                }
            }
        }
        else if (g->state == GHOST_STATE_EXITING_HOUSE) {
            // Ascend vertically from (23, 14) to (23, 12) => 96px at 0.25x of Pac-Man level base speed
            int16_t target_exit_y = 12 * MAZE_TILES_SIZE_PX; // 96px
            g->dir = DIR_UP;
            uint16_t exit_speed_fp = SPEED_TABLE[speed_lvl] >> 2; // 0.25x Pac-Man level speed

            if (g->sub_py > (target_exit_y << 8)) {
                if (g->sub_py >= (target_exit_y << 8) + exit_speed_fp) {
                    g->sub_py -= exit_speed_fp;
                } else {
                    g->sub_py = target_exit_y << 8;
                }
            }
            g->world_py = g->sub_py >> 8;

            if (g->world_py <= target_exit_y) {
                g->world_py = target_exit_y;
                g->sub_px = 0;
                g->sub_py = 0;
                g->in_house = false;
                g->state = GHOST_STATE_OUTSIDE;
                g->dir = DIR_LEFT;
            }
        }
        else if (g->state == GHOST_STATE_OUTSIDE) {
            update_ghost_outside_movement(i);
        }
    }

    // --- 2. Manage Ghost Exit Eligibility & Progression ---
    if (s_game_motion_started) {
        // Check if any ghost is currently exiting or moving to center exit column
        bool exit_in_progress = false;
        for (int i = 0; i < NGHOSTS; i++) {
            if (ghosts[i].state == GHOST_STATE_MOVING_TO_SLOT || ghosts[i].state == GHOST_STATE_EXITING_HOUSE) {
                exit_in_progress = true;
                break;
            }
        }

        if (!exit_in_progress) {
            int candidate_ghost = -1;

            if (s_initial_exit_idx < 4) {
                // Initial game start progression: Blinky (0) -> Pinky (1) -> Inky (2) -> Clyde (3)
                candidate_ghost = INITIAL_EXIT_ORDER[s_initial_exit_idx];
            } else if (s_fifo_count > 0) {
                // Return FIFO queue progression
                candidate_ghost = s_fifo_queue[0];
            }

            if (candidate_ghost >= 0 && candidate_ghost < NGHOSTS) {
                ghost_struct *g = &ghosts[candidate_ghost];
                int16_t row14_y = 14 * MAZE_TILES_SIZE_PX; // 112px

                // Required bounce distance before exit: 16px for initial game start ghosts, 80px for returning eaten ghosts
                uint16_t req_bounce = (s_initial_exit_idx < 4) ? 16 : 80;

                if (g->in_house && g->state == GHOST_STATE_HOME_BOUNCE && g->world_py == row14_y && g->bounce_dist_px >= req_bounce) {
                    if (g->world_px == (23 * MAZE_TILES_SIZE_PX)) {
                        // Already on center column (23, 14) -> Start ascending to (23, 12)
                        g->state = GHOST_STATE_EXITING_HOUSE;
                        g->dir = DIR_UP;
                        g->sub_py = g->world_py << 8; // Initialize subpixel Y accumulator!
                    } else {
                        // Move horizontally along row 14 to center column (23, 14)
                        g->state = GHOST_STATE_MOVING_TO_SLOT;
                    }

                    // Advance exit progression tracker
                    if (s_initial_exit_idx < 4) {
                        s_initial_exit_idx++;
                    } else if (s_fifo_count > 0) {
                        for (uint8_t f = 0; f < s_fifo_count - 1; f++) {
                            s_fifo_queue[f] = s_fifo_queue[f + 1];
                        }
                        s_fifo_count--;
                    }
                }
            }
        }
    }

render_sprites:
    // --- 3. Sprite Frame Selection & XRAM Update ---
    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];

        if (g->mode == GHOST_MODE_FRIGHTENED) {
            // Flashing cadence stage logic:
            // Constant total flashing duration = 240 frames (Stage 1: 144 frames, Stage 2: 96 frames)
            // - s_frightened_timer <= 96: Stage 2 (last 96 frames) -> 6 frames blue (40,41), 2 frames white (42,43) [8-frame cycle]
            // - s_frightened_timer <= 240: Stage 1 (240..97 frames) -> 12 frames blue (40,41), 4 frames white (42,43) [16-frame cycle]
            // - s_frightened_timer > 240: Solid vulnerable blue (40,41)
            bool show_white = false;

            if (s_frightened_timer <= 96) {
                // Stage 2: 6 frames blue (40,41), 2 frames white (42,43)
                show_white = ((s_frightened_timer % 8) < 2);
            } else if (s_frightened_timer <= 240) {
                // Stage 1: 12 frames blue (40,41), 4 frames white (42,43)
                show_white = ((s_frightened_timer % 16) < 4);
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

        // If this ghost was just eaten and we are in the 30-frame pause, replace frame with blank tile 48 on frame 2+
        if (s_eat_pause_timer > 0 && i == s_active_eat_anim_ghost) {
            uint8_t pause_frame_step = 30 - s_eat_pause_timer + 1; // 1..30
            if (pause_frame_step >= 2) {
                g->frame = 48; // Blank tile frame
            }
        }

        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, g->x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, g->y_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (g->frame * SPRITE_FRAME_SIZE)));
    }

    // --- 4. Render Eat Animation Sparkles (NSPARKLES = 6) ---
    // Sparkles appear for the first 30 frames when s_eat_pause_timer > 0
    if (s_eat_pause_timer > 0) {
        uint8_t anim_step = 30 - s_eat_pause_timer; // 0..29
        uint8_t table_idx = (anim_step >= 10) ? 9 : anim_step;
        int16_t pm_drawn_x = player.world_px + maze_dx - 3;
        int16_t pm_drawn_y = player.world_py - 3;

        for (int k = 0; k < NSPARKLES; k++) {
            unsigned sparkle_config = PRIZE_SPARKLE_CONFIG + (k * sizeof(vga_mode5_sprite_t));
            int16_t sp_x = pm_drawn_x + SPARKLE_ANIM_POS[table_idx][k].dx;
            int16_t sp_y = pm_drawn_y + SPARKLE_ANIM_POS[table_idx][k].dy;

            xram0_struct_set(sparkle_config, vga_mode5_sprite_t, x_pos_px, sp_x);
            xram0_struct_set(sparkle_config, vga_mode5_sprite_t, y_pos_px, sp_y);
            xram0_struct_set(sparkle_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (81 * SPRITE_FRAME_SIZE))); // Sparkle tile index 81
        }
    } else {
        // Park sparkles off-screen when not pausing
        for (int k = 0; k < NSPARKLES; k++) {
            unsigned sparkle_config = PRIZE_SPARKLE_CONFIG + (k * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(sparkle_config, vga_mode5_sprite_t, x_pos_px, -32);
            xram0_struct_set(sparkle_config, vga_mode5_sprite_t, y_pos_px, -32);
            xram0_struct_set(sparkle_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE))); // Blank tile 48
        }
    }

    // --- 5. Render Eaten Ghost Score Display (1 slot of 4 digits) ---
    // Displays remain visible for 120 frames total. During scrolling, coordinates track the maze world position.
    eaten_score_anim_t *sa = &s_single_score_anim;
    if (!sa->active) {
        // Park 4 digit sprites off-screen when inactive
        for (int d = 0; d < 4; d++) {
            unsigned score_config = GHOST_SCORE_CONFIG + (d * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, -32);
            xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, -32);
            xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
        }
    } else {
        // Determine current frame step 1..10 (holds on frame 10 position from frame 10 to 120)
        uint8_t pos_step_idx = (sa->anim_frame <= 10) ? (sa->anim_frame - 1) : 9;

        // Position relative to Pac-Man's world position when eaten (so score stays stationary on the maze during scrolling)
        int16_t pm_drawn_x = sa->pacman_world_x + maze_dx - 3;
        int16_t pm_drawn_y = sa->pacman_world_y - 3;

        for (int d = 0; d < 4; d++) {
            unsigned score_config = GHOST_SCORE_CONFIG + (d * sizeof(vga_mode5_sprite_t));
            int16_t digit_x = pm_drawn_x + SCORE_DIGIT_ANIM_POS[pos_step_idx][d].dx;
            int16_t digit_y = pm_drawn_y + SCORE_DIGIT_ANIM_POS[pos_step_idx][d].dy;

            xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, digit_x);
            xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, digit_y);
            xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (sa->digits[d] * SPRITE_FRAME_SIZE)));
        }

        // Increment frame counter
        sa->anim_frame++;
        if (sa->anim_frame > 120) {
            sa->active = false;
        }
    }
}