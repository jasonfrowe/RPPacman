#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "player.h"
#include "ghost.h"
#include "opl.h"

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

bool is_game_motion_started(void) {
    return s_game_motion_started;
}

void set_game_motion_started(bool started) {
    s_game_motion_started = started;
}
static uint8_t s_eat_pause_timer = 0; // Freeze movement during 30-frame eat animation pause

// Pac-Man death animation state tracking (Total: 305 frames)
// Phase 1 (1..40): Pause 40 frames
// Phase 2 (41..51): Clear ghosts, Pac-Man sprite frame 0 for 11 frames
// Phase 3 (52..171): Death rotation frames 71..80 (10 frames x 12 frames each = 120 frames)
// Phase 4 (172..206): Blank frame 48 for 35 frames (Ghosts reset to home & start bouncing)
// Phase 5 (207..305): 6 radial bouncing balls for 99 frames (3 bounces)
// End (306): Remove balls, restore Pac-Man facing last dir, allow spawn & movement
static uint16_t s_death_seq_timer = 0;
static int8_t s_death_last_dir = DIR_LEFT;

// Power Pellet Frightened State Tracking
static uint16_t s_frightened_timer = 0;
static uint16_t s_frightened_max_duration = 0;
static uint16_t s_ghosts_eaten_chain = 0; // Continuous combo counter across Power Pellets!

// Frightened outline-color cycle pacing: 8 stages spread evenly across
// the current frightened duration (s_frightened_stage_len = duration/8,
// via shift since 8 is a power of two -- no 6502 division needed). Later
// levels have a shorter FRIGHTENED_DURATION_TABLE entry, so the same 8
// stages play out faster, matching the ghosts' own flash-rate speedup.
static uint16_t s_frightened_stage_len = 0;
static uint16_t s_frightened_stage_countdown = 0;
static uint8_t s_frightened_stage = 0;

bool is_eat_pause_active(void) {
    return s_eat_pause_timer > 0;
}

bool is_death_sequence_active(void) {
    return s_death_seq_timer > 0;
}



void start_pacman_death_sequence(void) {
    if (s_death_seq_timer == 0) {
        s_death_seq_timer = 1;
        // The 305-frame death sequence freezes player_update_motion()
        // entirely, so any pellet-score popup still counting down would
        // otherwise sit frozen on screen for the whole sequence -- up to
        // 5 real seconds -- reading as a leftover, un-eaten pellet.
        clear_active_pellet_popups();
        s_death_last_dir = (player.dir != DIR_NONE) ? player.dir : DIR_LEFT;
        s_ghosts_eaten_chain = 0;   // Reset scared ghost combo chain on death
        s_frightened_timer = 0;     // Cancel active frightened power pellet state on death
        sfx_set_ambient("ROM:sfxnormal"); // Match: frightened may have been active
        set_frightened_palette(false);
        reset_player_on_death();    // Reset Pac-Dots eaten multiplier tier back to 10 points (0-59 dots)
        clear_all_active_score_popups(); // Remove any ghost or prize score popups off-screen
        sfx_play("ROM:sfxdeath", SFX_PRIORITY_TOP);
    }
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
    sfx_play("ROM:sfxghosteat", SFX_PRIORITY_TOP);

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

// Prize score animation tracking structure (2 slots for left/right prizes)
typedef struct {
    bool active;
    uint8_t anim_frame;    // 1..120
    int16_t pacman_world_x; // Pac-Man's world X when prize was eaten
    int16_t pacman_world_y; // Pac-Man's world Y when prize was eaten
    uint8_t digits[4];     // 4 sprite frame indices
} prize_score_anim_t;

static prize_score_anim_t s_prize_score_anims[NPRIZES]; // Slot 0 for left prize, Slot 1 for right prize

void trigger_prize_score_animation(uint8_t prize_index, uint16_t pts) {
    if (prize_index >= NPRIZES) return;

    prize_score_anim_t *pa = &s_prize_score_anims[prize_index];
    pa->active = true;
    pa->anim_frame = 1;
    pa->pacman_world_x = player.world_px;
    pa->pacman_world_y = player.world_py;

    uint16_t thousands = (pts / 1000) % 10;
    uint16_t hundreds  = (pts / 100) % 10;
    uint16_t tens      = (pts / 10) % 10;
    uint16_t ones      = pts % 10;

    pa->digits[0] = (thousands == 0) ? 48 : (106 + thousands);
    pa->digits[1] = (thousands == 0 && hundreds == 0) ? 48 : ((hundreds == 0) ? 116 : (106 + hundreds));
    pa->digits[2] = (thousands == 0 && hundreds == 0 && tens == 0) ? 48 : ((tens == 0) ? 116 : (106 + tens));
    pa->digits[3] = (ones == 0) ? 116 : (106 + ones);
}

void clear_all_active_score_popups(void) {
    s_single_score_anim.active = false;
    for (int d = 0; d < 4; d++) {
        unsigned score_config = GHOST_SCORE_CONFIG + (d * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, -32);
        xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, -32);
        xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
    }

    for (int p_idx = 0; p_idx < NPRIZES; p_idx++) {
        s_prize_score_anims[p_idx].active = false;
        uint8_t slot_offset = (1 + p_idx) * 4;
        for (int d = 0; d < 4; d++) {
            unsigned score_config = GHOST_SCORE_CONFIG + ((slot_offset + d) * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, -32);
            xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, -32);
            xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
        }
    }
}
// Linear ramp from 10.0s (level 0) down to 4.0s (level 21), plus a flat
// +12-frame (0.2s) buffer across every level -- the user found the
// un-padded ramp (600..240) left the ghost-chain bonus just barely out of
// reach, short by roughly a constant 0.1-0.3s regardless of level, not a
// percentage -- hence a flat frame offset rather than rescaling the ramp.
static const uint16_t FRIGHTENED_DURATION_TABLE[22] = {
    612, 595, 578, 561, 543, 526, 509, 492, 475, 458,
    441, 423, 406, 389, 372, 355, 338, 321, 303, 286, 269, 252
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
    s_frightened_stage_len = s_frightened_max_duration >> 3;
    if (s_frightened_stage_len == 0) s_frightened_stage_len = 1;
    s_frightened_stage_countdown = s_frightened_stage_len;
    s_frightened_stage = 0;
    sfx_set_ambient("ROM:sfxfrightened");
    set_frightened_palette(true);

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

// Check collision between Pac-Man and ghosts using central drawn positions & 4x4 collision box
void check_pacman_ghost_collisions(void) {
    if (s_death_seq_timer > 0) return;
    // Don't start a new eat while the previous one's 30-frame pause/
    // animation is still playing. Without this, two ghosts stacked on
    // the same tile both get eaten almost at once: everything is frozen
    // during the pause (comment below, "Set 30-frame pause for all
    // motion"), so a second overlapping ghost is still sitting right
    // there on the very next call -- and this function is called twice
    // per frame (ghost_update_motion(), before and after ghost movement/
    // screen updates), so even the per-call `break` below doesn't stop
    // the second call from eating it in the same frame. This makes
    // stacked ghosts get eaten one at a time, each with its own full
    // pause and animation, chaining the score correctly in sequence.
    if (s_eat_pause_timer > 0) return;

    // Central drawn position of Pac-Man:
    // Pac-Man sprite top-left drawn at (player.x_pos_px + VISUAL_X_OFFSET, player.world_py + VISUAL_Y_OFFSET)
    // Visual center = (drawn_x + 8, drawn_y + 8)
    int16_t pm_center_x = (player.x_pos_px + VISUAL_X_OFFSET) + 8;
    int16_t pm_center_y = (player.world_py + VISUAL_Y_OFFSET) + 8;

    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];

        if (g->in_house || g->state != GHOST_STATE_OUTSIDE) continue;

        // Central drawn position of Ghost:
        // Ghost sprite top-left drawn at (g->x_pos_px, g->y_pos_px)
        // Visual center = (drawn_x + 8, drawn_y + 8)
        int16_t g_center_x = g->x_pos_px + 8;
        int16_t g_center_y = g->y_pos_px + 8;

        int16_t dx = g_center_x - pm_center_x;
        int16_t dy = g_center_y - pm_center_y;

        // Handle vertical tunnel wrapping seam (184px loop)
        if (dy > (VERTICAL_TUNNEL_WRAP_PX / 2)) dy -= VERTICAL_TUNNEL_WRAP_PX;
        else if (dy < -(VERTICAL_TUNNEL_WRAP_PX / 2)) dy += VERTICAL_TUNNEL_WRAP_PX;

        // 4x4 collision box centered on visual drawn positions:
        // Both sprites have a 4x4 bounding box centered within the 16x16 frame (offsets +6 to +9).
        // Two 4x4 boxes overlap when center distance |dx| <= 3 and |dy| <= 3.
        bool overlap = (dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3);

        if (overlap) {
            if (g->mode == GHOST_MODE_FRIGHTENED) {
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

                add_player_score(pts, SCORE_CAT_GHOST);

                // Set 30-frame pause for all motion & trigger eat animation
                s_eat_pause_timer = 30;
                // Same reasoning as start_pacman_death_sequence(): this
                // also freezes player_update_motion(), so a pending
                // pellet-score popup would otherwise sit frozen for the
                // pause's duration.
                clear_active_pellet_popups();
                trigger_eaten_ghost_animation(i, pts);
                // Only one ghost eaten per collision check -- otherwise
                // two frightened ghosts overlapping Pac-Man on the same
                // pixel both get eaten (and scored) in the same frame.
                break;
            } else if (g->mode == GHOST_MODE_CHASE) {
                // Normal ghost catches Pac-Man! Trigger Pac-Man death sequence
                start_pacman_death_sequence();
                break;
            }
        }
    }
}

// Compute Arcade target tile for ghosts in normal CHASE mode
static void compute_ghost_target_tile(int ghost_index, int16_t *target_tx, int16_t *target_ty) {
    int16_t pac_tx = (int16_t)((player.world_px + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);
    int16_t pac_ty = (int16_t)((player.world_py + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);

    int8_t pac_dx, pac_dy;
    get_dir_offset(player.dir, &pac_dx, &pac_dy);

    switch (ghost_index) {
        case 0: // Red (Blinky): Direct Target -> Pac-Man's exact current tile
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
                int16_t blinky_tx = (int16_t)((ghosts[0].world_px + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);
                int16_t blinky_ty = (int16_t)((ghosts[0].world_py + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);

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
                int16_t clyde_tx = (int16_t)((ghosts[3].world_px + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);
                int16_t clyde_ty = (int16_t)((ghosts[3].world_py + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);

                // Tile-coordinate deltas here are bounded well within
                // int16_t (maze is 46x29 tiles), so this avoids a real
                // 32-bit multiply the 6502 has no hardware support for.
                int16_t dx = clyde_tx - pac_tx;
                int16_t dy = clyde_ty - pac_ty;

                // Fold to the shorter wrapped-around distance -- see the
                // matching comment in the per-intersection direction
                // selection below. Without this, Clyde near one tunnel
                // mouth could read Pac-Man (just past the other mouth) as
                // far away instead of within his 8-tile "shy" radius.
                if (dx > (MAZE_MAP_WIDTH / 2)) dx -= MAZE_MAP_WIDTH;
                else if (dx < -(MAZE_MAP_WIDTH / 2)) dx += MAZE_MAP_WIDTH;
                // No vertical fold here -- unlike the horizontal tunnel,
                // the vertical wrap isn't reachable from every column (see
                // suppress_lr_turns in update_ghost_outside_movement), and
                // this function has no cheap way to check whether Clyde is
                // at one of the columns where it is. Folding unconditionally
                // invents a bogus wraparound "shortcut" for ordinary
                // vertical distances most of the map away from any real
                // seam, which did more harm (misjudging the 8-tile "shy"
                // radius almost everywhere) than the rare tunnel-mouth edge
                // case it was meant to fix.
                if (dy > (MAZE_MAP_HEIGHT / 2)) dy -= MAZE_MAP_HEIGHT;
                else if (dy < -(MAZE_MAP_HEIGHT / 2)) dy += MAZE_MAP_HEIGHT;

                int16_t dist_sq = (dx * dx) + (dy * dy);

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

// 8.8 Fixed-Point Ghost Chase-Speed Table across 22 Prize Levels (Cherry to Crown).
// Always exactly 95% of Pac-Man's own SPEED_TABLE (player.c) at the same
// level -- ghosts ramp up in lockstep with Pac-Man's own front-loaded
// curve but never catch up to, let alone exceed, his speed. The previous
// table let ghosts overtake Pac-Man at high levels (matching real
// Pac-Man CE hardware), which made the late game feel unfairly hard;
// capping ghosts at a constant fraction of Pac-Man's speed removes that
// entirely by construction, at every level, not just at the endpoints.
static const uint16_t GHOST_SPEED_TABLE[22] = {
    0x00F3, // Level 0  (Cherry):            0.949 px/frame
    0x012A, // Level 1  (Strawberry):        1.164 px/frame
    0x0161, // Level 2  (Orange):            1.379 px/frame
    0x0198, // Level 3  (Apple):             1.594 px/frame
    0x01D0, // Level 4  (Melon):             1.812 px/frame
    0x0207, // Level 5  (Banana):            2.027 px/frame
    0x020A, // Level 6  (Peach):             2.039 px/frame
    0x020C, // Level 7  (Galaxian Boss):     2.047 px/frame
    0x020F, // Level 8  (Bell):              2.059 px/frame
    0x0213, // Level 9  (Key):               2.074 px/frame
    0x0216, // Level 10 (Coffee):            2.086 px/frame
    0x0219, // Level 11 (Cake):              2.098 px/frame
    0x021C, // Level 12 (Galaga):            2.109 px/frame
    0x021E, // Level 13 (Gaplus Drone):      2.117 px/frame
    0x0222, // Level 14 (Hamburger):         2.133 px/frame
    0x0225, // Level 15 (Fried Egg):         2.145 px/frame
    0x0228, // Level 16 (Candy):             2.156 px/frame
    0x022B, // Level 17 (Four-Leaf Clover):  2.168 px/frame
    0x022E, // Level 18 (Diamond):           2.180 px/frame
    0x0231, // Level 19 (Heart):             2.191 px/frame
    0x0234, // Level 20 (Samurai Helmet):    2.203 px/frame
    0x0237, // Level 21 (Crown):             2.215 px/frame (95% of SPEED_TABLE[21])
};

// EXTRA's own 22-level ghost speed ramp -- same rule as NORMAL's table
// above, always exactly 95% of EXTRA_SPEED_TABLE (player.c) at the same
// level, so EXTRA's ghosts never catch up to EXTRA's own Pac-Man speed
// either.
static const uint16_t EXTRA_GHOST_SPEED_TABLE[22] = {
    0x01BF, // Level 0:  1.746 px/frame
    0x01C5, // Level 1:  1.770 px/frame
    0x01CB, // Level 2:  1.793 px/frame
    0x01D1, // Level 3:  1.816 px/frame
    0x01D6, // Level 4:  1.836 px/frame
    0x01DC, // Level 5:  1.859 px/frame
    0x01E2, // Level 6:  1.883 px/frame
    0x01E7, // Level 7:  1.902 px/frame
    0x01ED, // Level 8:  1.926 px/frame
    0x01F3, // Level 9:  1.949 px/frame
    0x01F8, // Level 10: 1.969 px/frame
    0x01FE, // Level 11: 1.992 px/frame
    0x0204, // Level 12: 2.016 px/frame
    0x020A, // Level 13: 2.039 px/frame
    0x020F, // Level 14: 2.059 px/frame
    0x0215, // Level 15: 2.082 px/frame
    0x021B, // Level 16: 2.105 px/frame
    0x0220, // Level 17: 2.125 px/frame
    0x0226, // Level 18: 2.148 px/frame
    0x022C, // Level 19: 2.172 px/frame
    0x0231, // Level 20: 2.191 px/frame
    0x0237, // Level 21: 2.215 px/frame (matches GHOST_SPEED_TABLE[21] exactly)
};

// Picks GHOST_SPEED_TABLE vs EXTRA_GHOST_SPEED_TABLE by game mode.
static uint16_t get_ghost_base_speed_fp(uint8_t speed_lvl) {
    return (get_game_mode() == GAME_MODE_EXTRA) ? EXTRA_GHOST_SPEED_TABLE[speed_lvl] : GHOST_SPEED_TABLE[speed_lvl];
}

// Forward declarations -- defined further down, needed by the vertical-
// tunnel wrap safety check in update_ghost_outside_movement() below.
static bool is_ghost_safe_tile_value(uint8_t v);
static uint8_t read_maze_tile(uint16_t tx, uint16_t ty);

// Update ghost movement when OUTSIDE in the maze
static void update_ghost_outside_movement(int ghost_index) {
    ghost_struct *g = &ghosts[ghost_index];

    uint8_t speed_lvl = get_speed_level_index();
    uint16_t base_speed_fp = get_ghost_base_speed_fp(speed_lvl);
    uint16_t speed_fp = base_speed_fp; // Normal chase mode ghosts move at their own level speed

    if (g->mode == GHOST_MODE_FRIGHTENED) {
        // Vulnerable ghosts move at 0.25x speed (1/4 of level speed)
        speed_fp = base_speed_fp >> 2;
    } else if (g->mode == GHOST_MODE_EATEN) {
        // Eaten eyes move at 2.0x speed to return home quickly
        speed_fp = base_speed_fp * 2;
    } else if (ghost_index == 0) {
        // Blinky Cruise Elroy speed boost (+5% speed while chasing)
        speed_fp += (base_speed_fp >> 4);
    }

    int8_t dx, dy;
    int16_t move_pixels = 0;

    if (g->dir == DIR_LEFT || g->dir == DIR_RIGHT) {
        g->sub_px += speed_fp;
        move_pixels = g->sub_px >> 8;
        g->sub_px &= 0x00FF;
    } else if (g->dir == DIR_UP || g->dir == DIR_DOWN) {
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

        // Handle vertical tunnel wrapping. The 184px jump only lands on a
        // real, open tile at the small set of columns where the top and
        // bottom border rows both happen to be unwalled at the same
        // column (confirmed by direct maze-data inspection: e.g. row 2
        // near the top and row 25 near the bottom are each walled at
        // MOST columns, not just a couple like the horizontal tunnel's
        // single row). At any other column this jump would land the
        // ghost inside a wall tile -- and since this movement loop has no
        // per-pixel wall check between intersections, it would then just
        // keep walking through solid walls indefinitely. So the jump is
        // only taken when the destination tile is verified safe; otherwise
        // it's skipped and the ghost is left to run into the ordinary
        // wall-blocked path at the next intersection, exactly like any
        // other wall.
        int16_t drawn_y = g->world_py - 3;
        if (g->dir == DIR_DOWN && (drawn_y + SPRITE_SIZE_PX) >= VERTICAL_TUNNEL_TRIGGER_BOTTOM_DRAWN_Y) {
            int16_t wrapped_py = g->world_py - VERTICAL_TUNNEL_WRAP_PX;
            if (is_ghost_safe_tile_value(read_maze_tile((uint16_t)(g->world_px / MAZE_TILES_SIZE_PX), (uint16_t)(wrapped_py / MAZE_TILES_SIZE_PX)))) {
                g->world_py = wrapped_py;
            }
        } else if (g->dir == DIR_UP && drawn_y <= VERTICAL_TUNNEL_TRIGGER_TOP_DRAWN_Y) {
            int16_t wrapped_py = g->world_py + VERTICAL_TUNNEL_WRAP_PX;
            if (is_ghost_safe_tile_value(read_maze_tile((uint16_t)(g->world_px / MAZE_TILES_SIZE_PX), (uint16_t)(wrapped_py / MAZE_TILES_SIZE_PX)))) {
                g->world_py = wrapped_py;
            }
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

        // Evaluate intersection turn decisions 1 tile (8px) ahead before stepping into the tile center!
        // In original Arcade Pac-Man, ghosts select their next direction at the entry to the tile preceding an intersection.
        bool at_intersection = (g->world_px % MAZE_TILES_SIZE_PX == 0) &&
                               (g->world_py % MAZE_TILES_SIZE_PX == 0);

        if (at_intersection) {
            int16_t cur_tx = (int16_t)((g->world_px + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);
            int16_t cur_ty = (int16_t)((g->world_py + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);

            int16_t target_tx, target_ty;
            if (g->mode == GHOST_MODE_EATEN) {
                // Step 1: Eaten eyes target home door tile (23, 12) from outside
                target_tx = 23;
                target_ty = 12;
            } else if (g->mode == GHOST_MODE_FRIGHTENED) {
                // Frightened ghosts select target using pseudo-random tile based on current tile & ghost index
                int16_t pac_tx = (int16_t)((player.world_px + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);
                int16_t pac_ty = (int16_t)((player.world_py + (MAZE_TILES_SIZE_PX / 2)) / MAZE_TILES_SIZE_PX);

                // Corner scatter targets for frightened mode as fallback directions
                static const int16_t CORNER_TX[4] = { 0, 46, 0, 46 };
                static const int16_t CORNER_TY[4] = { 0, 0, 29, 29 };
                uint8_t corner_idx = (cur_tx + cur_ty + ghost_index) % 4;

                target_tx = CORNER_TX[corner_idx];
                target_ty = CORNER_TY[corner_idx];

                // If Pac-Man is close, head towards opposite corner of Pac-Man
                if (pac_tx < 23 && pac_ty < 15) {
                    target_tx = 46; target_ty = 29;
                } else if (pac_tx >= 23 && pac_ty < 15) {
                    target_tx = 0; target_ty = 29;
                } else if (pac_tx < 23 && pac_ty >= 15) {
                    target_tx = 46; target_ty = 0;
                } else {
                    target_tx = 0; target_ty = 0;
                }
            } else {
                compute_ghost_target_tile(ghost_index, &target_tx, &target_ty);
            }

            int8_t opposite_dir = get_opposite_dir(g->dir);
            int8_t best_dir = DIR_NONE;
            int16_t min_dist_sq = 0x7FFF;

            // Strict Arcade Direction Priority Order: UP, LEFT, DOWN, RIGHT
            static const int8_t EVAL_DIRS[4] = { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT };

            // Check if ghost is in vertical tunnel regions (< 40px or > 200px drawn Y)
            bool is_in_vertical_tunnel = (drawn_y < 40) || ((drawn_y + SPRITE_SIZE_PX) > 200);

            // This Y-band is wide (about 6 tile rows each end), and most of
            // it is just ordinary open maze interior with normal
            // T-intersections -- confirmed by direct maze-data inspection,
            // the real wrap-eligible columns are a narrow subset (same set
            // the movement loop's wrap-teleport guard above checks), not
            // every column in the band. Suppressing left/right turns
            // everywhere in the band (below) used to be able to trap a
            // ghost that reaches a normal T-intersection there needing to
            // turn, with continuing up/down genuinely blocked by a real
            // wall and no suppressed-axis alternative offered -- exactly
            // the "stuck oscillating up/down with only left/right actually
            // open" symptom. Only suppress where this specific column's
            // wrap landing spot is verified safe.
            bool column_supports_vertical_wrap = false;
            if (is_in_vertical_tunnel) {
                int16_t wrap_py = (drawn_y < 40) ? (g->world_py + VERTICAL_TUNNEL_WRAP_PX)
                                                  : (g->world_py - VERTICAL_TUNNEL_WRAP_PX);
                column_supports_vertical_wrap = is_ghost_safe_tile_value(read_maze_tile((uint16_t)(g->world_px / MAZE_TILES_SIZE_PX), (uint16_t)(wrap_py / MAZE_TILES_SIZE_PX)));
            }
            bool suppress_lr_turns = is_in_vertical_tunnel && column_supports_vertical_wrap;

            // The horizontal tunnel's mouth (world tile column 0/46) sits
            // just past the real playable border (column 1/45), which is
            // walled everywhere except a handful of rows -- but the
            // margin column itself (confirmed by direct maze-data
            // inspection) is blank/unwalled for nearly the whole map
            // height, since nothing was ever meant to stand there except
            // while passing straight through the tunnel. A ghost that
            // turns up/down right at the tunnel mouth can walk that
            // unwalled margin almost the full height of the map with no
            // real wall to stop it -- this margin isn't a real path for
            // anyone, so unlike the vertical-tunnel case below, eaten
            // eyes get no exemption here.
            bool is_in_horizontal_tunnel = (g->world_px < MAZE_TILES_SIZE_PX) ||
                                            (g->world_px >= WORLD_WIDTH - MAZE_TILES_SIZE_PX);

            for (uint8_t d = 0; d < 4; d++) {
                int8_t test_dir = EVAL_DIRS[d];
                if (test_dir == opposite_dir) continue;

                // Ignore left/right turn evaluation when in vertical tunnel
                // -- except for eaten eyes returning home: a real wall
                // still blocks the turn via can_step_dir() below regardless,
                // so this only matters where a genuine turn exists, and
                // suppressing it there could trap the eyes oscillating
                // up/down through the wrap seam forever (the seam sits
                // inside this same Y band), never reaching a Y position
                // where a left/right turn toward the home door is allowed.
                if (suppress_lr_turns && g->mode != GHOST_MODE_EATEN &&
                    (test_dir == DIR_LEFT || test_dir == DIR_RIGHT)) {
                    continue;
                }

                // Ignore up/down turn evaluation at the horizontal
                // tunnel's mouth -- see is_in_horizontal_tunnel above.
                if (is_in_horizontal_tunnel && (test_dir == DIR_UP || test_dir == DIR_DOWN)) {
                    continue;
                }

                if (can_step_dir(g->world_px, g->world_py, test_dir)) {
                    int8_t t_dx, t_dy;
                    get_dir_offset(test_dir, &t_dx, &t_dy);

                    int16_t next_tx = cur_tx + t_dx;
                    int16_t next_ty = cur_ty + t_dy;

                    // Same bound as the Clyde check above -- safe in
                    // int16_t, and this runs for every ghost at every
                    // intersection, so it's the hottest of these.
                    int16_t diff_x = next_tx - target_tx;
                    int16_t diff_y = next_ty - target_ty;

                    // The horizontal tunnel wraps, but target/next tile
                    // coordinates are plain unwrapped numbers -- a ghost
                    // one tile from the tunnel mouth and Pac-Man one tile
                    // past it on the far side would otherwise measure as
                    // almost the full map width apart instead of 2 tiles,
                    // which could steer a ghost away from the tunnel right
                    // as Pac-Man wraps through it. Folding to the shorter
                    // of the direct or wrapped-around distance fixes that.
                    //
                    // Must be gated the same way suppress_lr_turns gates
                    // the vertical fold below -- only when THIS ghost is
                    // actually at the tunnel mouth right now. Inky's target
                    // tile is a vector extrapolation (pivot + (pivot -
                    // Blinky)) that routinely lands far outside the actual
                    // 0..46 map -- not a real wrapped position, just a
                    // number bigger than half the map width. Folding it
                    // unconditionally "corrected" that arbitrary distance
                    // into something small and essentially random every
                    // time Inky was nowhere near either tunnel mouth,
                    // which is most of the game -- exactly why Inky read
                    // as aimless/fleeing instead of ambushing: its own
                    // distance metric was noise most of the time. Blinky/
                    // Pinky/Clyde's targets stay near Pac-Man's own
                    // always-in-bounds tile and were rarely affected.
                    if (is_in_horizontal_tunnel) {
                        if (diff_x > (MAZE_MAP_WIDTH / 2)) diff_x -= MAZE_MAP_WIDTH;
                        else if (diff_x < -(MAZE_MAP_WIDTH / 2)) diff_x += MAZE_MAP_WIDTH;
                    }
                    // Unlike the horizontal axis, the vertical "tunnel" is
                    // NOT a real wrap across the whole map -- it only
                    // exists at the handful of columns verified by
                    // suppress_lr_turns above, and only near the seam
                    // itself. Folding diff_y unconditionally (whether by
                    // MAZE_MAP_HEIGHT or any other period) invents a bogus
                    // wraparound "shortcut" for any two points more than
                    // half that period apart vertically -- which, at normal
                    // maze-height distances, is most of the map, not just
                    // near the seam. That systematically steered ghosts
                    // (including eaten eyes targeting the home door, often
                    // 15+ rows away) toward the wrong direction everywhere,
                    // not just at the tunnel. Only fold when this ghost is
                    // actually at a wrap-eligible position right now.
                    if (suppress_lr_turns) {
                        if (diff_y > (VERTICAL_TUNNEL_WRAP_TILES / 2)) diff_y -= VERTICAL_TUNNEL_WRAP_TILES;
                        else if (diff_y < -(VERTICAL_TUNNEL_WRAP_TILES / 2)) diff_y += VERTICAL_TUNNEL_WRAP_TILES;
                    }

                    int16_t dist_sq = (diff_x * diff_x) + (diff_y * diff_y);

                    if (dist_sq < min_dist_sq) {
                        min_dist_sq = dist_sq;
                        best_dir = test_dir;
                    }
                }
            }

            // Fallback: If no valid non-reversing direction was found, check if continuing current direction is valid,
            // otherwise force reversing opposite direction to prevent stepping into walls.
            //
            // Must respect the same tunnel suppression as the main loop
            // above -- otherwise this is a loophole that defeats it. If the
            // main loop finds no candidate (e.g. left/right both genuinely
            // blocked by the real border wall at most rows of the
            // horizontal tunnel's padding column), falling back to a raw,
            // unfiltered can_step_dir(g->dir)/can_step_dir(opposite_dir)
            // check happily grants up/down again right there, since the
            // padding column is open vertically almost the whole map
            // height. That's a real, confirmed mechanism for exactly the
            // reported symptom: the ghost gets shoved up/down, that axis
            // doesn't change world_px so it's still in the same suppressed
            // zone at the next intersection, and once it hits a real
            // boundary in that direction this same fallback's second
            // branch sends it right back the way it came -- a genuine
            // mechanical 180 flip, repeating forever.
            if (best_dir == DIR_NONE) {
                bool dir_is_lr = (g->dir == DIR_LEFT || g->dir == DIR_RIGHT);
                bool dir_is_ud = (g->dir == DIR_UP || g->dir == DIR_DOWN);
                bool opp_is_lr = (opposite_dir == DIR_LEFT || opposite_dir == DIR_RIGHT);
                bool opp_is_ud = (opposite_dir == DIR_UP || opposite_dir == DIR_DOWN);
                bool dir_suppressed = (suppress_lr_turns && g->mode != GHOST_MODE_EATEN && dir_is_lr) ||
                                       (is_in_horizontal_tunnel && dir_is_ud);
                bool opp_suppressed = (suppress_lr_turns && g->mode != GHOST_MODE_EATEN && opp_is_lr) ||
                                       (is_in_horizontal_tunnel && opp_is_ud);

                if (!dir_suppressed && can_step_dir(g->world_px, g->world_py, g->dir)) {
                    best_dir = g->dir;
                } else if (!opp_suppressed && can_step_dir(g->world_px, g->world_py, opposite_dir)) {
                    best_dir = opposite_dir;
                } else if (can_step_dir(g->world_px, g->world_py, g->dir)) {
                    // Truly no allowed direction at all (both real axis
                    // candidates blocked and the perpendicular one is
                    // suppressed) -- should be rare. Keep moving in the
                    // current direction rather than reintroduce the
                    // suppressed axis here too; check_and_reset_stuck_
                    // ghosts() covers the case where this leaves the ghost
                    // somewhere it shouldn't be.
                    best_dir = g->dir;
                } else {
                    best_dir = g->dir;
                }
            }

            g->dir = best_dir;
        }
    }
}

// Tile value taxonomy (from the map author): 0 = blank floor, 116-119 =
// pellet/dot variants, 120-124 = score-popup digit overlays (temporary,
// still walkable) -- all safe. 1-115 = real maze wall art. 125-127 =
// out-of-bounds markers (void beyond the maze, not real wall geometry).
// 1-115 and 125-127 are both "can't stand here."
static bool is_ghost_safe_tile_value(uint8_t v) {
    return v == 0 || (v >= 116 && v <= 124);
}

static uint8_t read_maze_tile(uint16_t tx, uint16_t ty) {
    uint16_t offset = ty * MAZE_MAP_WIDTH + tx;
    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    return RIA.rw0;
}

// Canonical "deliver this ghost home" sequence -- shared by the tile-
// value-based fallback below and the progress watchdog further down.
// Always resets mode to CHASE: reached naturally (eaten eyes arriving at
// the door, see GHOST_STATE_ENTERING_HOUSE) or forced here, a ghost
// sitting in the house has no business staying EATEN or FRIGHTENED.
static void force_ghost_home(uint8_t i) {
    ghost_struct *g = &ghosts[i];
    g->world_px = GHOST_HOME_X[i];
    g->world_py = GHOST_MAX_Y[i]; // Row 16 (128px)
    g->sub_px = g->world_px << 8;
    g->sub_py = g->world_py << 8;
    g->bounce_dist_px = 0;
    g->in_house = true;
    g->state = GHOST_STATE_HOME_BOUNCE;
    g->mode = GHOST_MODE_CHASE;
    g->dir = DIR_UP;

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

void check_and_reset_stuck_ghosts(void) {
    for (int i = 0; i < NGHOSTS; i++) {
        ghost_struct *g = &ghosts[i];
        if (g->in_house || g->state != GHOST_STATE_OUTSIDE) continue;

        // Eaten eyes already have their own self-sufficient no-wall-check
        // movement toward the home door tile (update_ghost_outside_movement's
        // GHOST_MODE_EATEN branch, same "no per-pixel wall check between
        // intersections" property as this escape walk) -- if this check
        // didn't skip them, it would overwrite g->dir with an unrelated
        // escape direction on every single frame an eaten ghost sits on an
        // invalid tile (exactly what happens while it's legitimately
        // crossing one en route home), fighting the home-return targeting
        // and preventing it from ever reaching the door.
        if (g->mode == GHOST_MODE_EATEN) continue;

        uint16_t tx = (uint16_t)(g->world_px / MAZE_TILES_SIZE_PX);
        uint16_t ty = (uint16_t)(g->world_py / MAZE_TILES_SIZE_PX);

        if (tx >= MAZE_MAP_WIDTH || ty >= MAZE_MAP_HEIGHT) continue;

        uint8_t tile_val = read_maze_tile(tx, ty);

        // Ghost is standing on a wall or out-of-bounds tile. This happens
        // when a maze-transition column reveal turns the tile a ghost is
        // standing on into a wall out from under it, not from normal
        // movement (can_step_dir already keeps ghosts from walking into a
        // wall that already exists).
        //
        // Deliberately simple and local: only the 4 immediate cardinal
        // neighbors (a 1-tile radius) are ever considered -- not a
        // long-range scan. An earlier version scanned up to 20 tiles away
        // and handed the ghost off to normal per-frame movement to walk
        // there; in practice that gave the normal chase-targeting logic
        // (which runs every intersection along the way, with no idea a
        // rescue is in progress) many tiles and many frames to fight over
        // the ghost's direction, and confirmed live to sometimes send it
        // through the horizontal tunnel wrap and out the far side of the
        // map entirely. Limiting the search to one step bounds the
        // exposure to at most a handful of frames, and if nothing safe is
        // immediately adjacent, teleporting home is the safer choice over
        // trying to navigate a long, contested path out.
        if (!is_ghost_safe_tile_value(tile_val)) {
            static const int8_t ADJ_DX[4]  = { 0, 0, -1, 1 };
            static const int8_t ADJ_DY[4]  = { -1, 1, 0, 0 };
            static const int8_t ADJ_DIR[4] = { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

            int8_t escape_dir = DIR_NONE;
            for (uint8_t d = 0; d < 4; d++) {
                int16_t nx = (int16_t)tx + ADJ_DX[d];
                int16_t ny = (int16_t)ty + ADJ_DY[d];
                if (nx < 0 || nx >= MAZE_MAP_WIDTH || ny < 0 || ny >= MAZE_MAP_HEIGHT) continue;
                if (is_ghost_safe_tile_value(read_maze_tile((uint16_t)nx, (uint16_t)ny))) {
                    escape_dir = ADJ_DIR[d];
                    break;
                }
            }

            if (escape_dir != DIR_NONE) {
                // Hand off to normal per-frame movement -- a smooth,
                // one-tile step, not a teleport. This function keeps
                // reasserting g->dir every frame the ghost is still on an
                // unsafe tile, so it reliably reaches the safe neighbor
                // even if at_intersection's own targeting briefly
                // disagrees mid-step; once there, this check stops firing
                // and normal direction selection takes back over on its
                // own.
                g->dir = escape_dir;
                continue;
            }

            // No safe tile immediately adjacent: teleport home rather
            // than risk navigating further.
            force_ghost_home((uint8_t)i);
        }
    }
}

// Forces the Pac-Man death-animation sequence off, independent of
// reset_ghosts_to_initial_positions() (which is also called mid-sequence,
// at t==172, to send ghosts home for the bounce phase -- clearing the
// timer there would abort phases 4/5 partway through). Only a genuine
// new-game start needs this: a game that ends (e.g. the 5-minute timer
// expiring) while the timer is still counting down would otherwise leave
// it non-zero, causing the next game to start with Pac-Man instantly
// replaying the tail of the previous death animation.
void reset_death_sequence(void) {
    s_death_seq_timer = 0;
}

void reset_ghosts_to_initial_positions(void) {
    init_ghost_data();
    s_initial_exit_idx = 0;
    s_fifo_count = 0;
    s_game_motion_started = false;
    s_ghosts_eaten_chain = 0;
    s_frightened_timer = 0;
    for (int i = 0; i < 4; i++) {
        s_fifo_queue[i] = -1;
    }
}

void ghost_update_motion(void) {
    if (s_death_seq_timer > 0) {
        // --- Pac-Man Death Animation Sequence (Total 305 frames) ---
        uint16_t t = s_death_seq_timer;
        s_death_seq_timer++;

        if (t <= 40) {
            // Phase 1 (1..40): Pause all movement of ghosts, Pac-Man, and animations for 40 frames.
            // Ghosts stay at current positions.
        } else if (t <= 51) {
            // Phase 2 (41..51): Clear ghosts off-screen (-32, -32), set Pac-Man to frame 0 (facing UP) for 11 frames.
            for (int i = 0; i < NGHOSTS; i++) {
                ghosts[i].x_pos_px = -32;
                ghosts[i].y_pos_px = -32;
                unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, -32);
            }
            player.frame = 0; // Facing UP
            xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (0 * SPRITE_FRAME_SIZE)));
        } else if (t <= 171) {
            // Phase 3 (52..171): Death animation frames 71 to 80 (10 frames x 12 frames each = 120 frames).
            uint16_t anim_step = (t - 52) / 12; // 0..9
            if (anim_step > 9) anim_step = 9;
            player.frame = 71 + anim_step;
            xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
        } else if (t <= 206) {
            // Phase 4 (172..206): Use blank frame (48) for 35 frames.
            // At frame 172, remove 1 life from Pac-Man, reset ghosts to initial home positions and start up-and-down motion!
            if (t == 172) {
                if (player.lives > 0) {
                    player.lives--;
                }
                update_player_lives_display(player.lives);
                reset_ghosts_to_initial_positions();
            }
            player.frame = 48; // Blank frame
            xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));

            // Ghosts begin their up-and-down bounce in home
            for (int i = 0; i < NGHOSTS; i++) {
                ghost_struct *g = &ghosts[i];
                if (g->state == GHOST_STATE_HOME_BOUNCE) {
                    uint8_t speed_lvl = get_speed_level_index();
                    uint16_t home_speed_fp = (get_player_base_speed_fp(speed_lvl) * 85) / 100;
                    uint16_t move_px = 0;
                    if (g->dir == DIR_DOWN) {
                        g->sub_py += home_speed_fp;
                        move_px = g->sub_py >> 8;
                        g->sub_py &= 0x00FF;
                        for (uint16_t step = 0; step < move_px; step++) {
                            g->world_py++;
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
                            if (g->world_py <= g->min_home_py) {
                                g->world_py = g->min_home_py;
                                g->dir = DIR_DOWN;
                                break;
                            }
                        }
                    }
                    g->x_pos_px = g->world_px + maze_dx - 3;
                    g->y_pos_px = g->world_py - 3;
                    unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                    xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, g->x_pos_px);
                    xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, g->y_pos_px);
                    xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (g->frame * SPRITE_FRAME_SIZE)));
                }
            }
        } else if (t <= 305) {
            // Phase 5 (207..305): 6 balls (sprite index 106) perform 3 radial bounces over 99 frames.
            // Pac-Man remains blank tile 48.
            player.frame = 48;
            xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));

            uint16_t b_frame = t - 207; // 0..98
            int16_t pm_drawn_x = player.world_px + maze_dx - 3;
            int16_t pm_drawn_y = player.world_py - 3;

            // 3 bounce cycles of 33 frames each (0..32, 33..65, 66..98)
            uint16_t cycle = b_frame / 33; // 0, 1, 2
            uint16_t sub = b_frame % 33;   // 0..32

            // Amplitudes: Bounce 1: 24px, Bounce 2: 12px, Bounce 3: 4px
            int16_t max_amp = (cycle == 0) ? 24 : ((cycle == 1) ? 12 : 4);
            // Parabola: r = max_amp * sin(sub * pi / 32) approximated via quadratic parabola 4*x*(32-x)/1024
            // 4*sub*(32-sub) peaks at 1024 (sub=16), times max_amp (<=24)
            // is at most 24576 -- safely within int16_t, avoiding a real
            // 32-bit multiply for this every frame during the bounce.
            int16_t height = (4 * (int16_t)sub * (32 - (int16_t)sub) * max_amp) / 1024;

            // 6 radial directions equally spaced by 60 degrees (0, 60, 120, 180, 240, 300 deg)
            // cos/sin ratios scaled by 256:
            // 0 deg:   cos=256, sin=0
            // 60 deg:  cos=128, sin=221
            // 120 deg: cos=-128, sin=221
            // 180 deg: cos=-256, sin=0
            // 240 deg: cos=-128, sin=-221
            // 300 deg: cos=128, sin=-221
            static const int16_t RADIAL_COS[6] = { 256, 128, -128, -256, -128, 128 };
            static const int16_t RADIAL_SIN[6] = { 0, 221, 221, 0, -221, -221 };

            for (int k = 0; k < NSPARKLES; k++) {
                unsigned ball_config = PRIZE_SPARKLE_CONFIG + (k * sizeof(vga_mode5_sprite_t));
                int16_t bx = pm_drawn_x + (int16_t)((height * RADIAL_COS[k]) / 256);
                int16_t by = pm_drawn_y - (int16_t)((height * RADIAL_SIN[k]) / 256);

                xram0_struct_set(ball_config, vga_mode5_sprite_t, x_pos_px, bx);
                xram0_struct_set(ball_config, vga_mode5_sprite_t, y_pos_px, by);
                xram0_struct_set(ball_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (106 * SPRITE_FRAME_SIZE))); // Sprite index 106
            }

            // Ghosts continue bouncing in home
            for (int i = 0; i < NGHOSTS; i++) {
                ghost_struct *g = &ghosts[i];
                if (g->state == GHOST_STATE_HOME_BOUNCE) {
                    uint8_t speed_lvl = get_speed_level_index();
                    uint16_t home_speed_fp = (get_player_base_speed_fp(speed_lvl) * 85) / 100;
                    uint16_t move_px = 0;
                    if (g->dir == DIR_DOWN) {
                        g->sub_py += home_speed_fp;
                        move_px = g->sub_py >> 8;
                        g->sub_py &= 0x00FF;
                        for (uint16_t step = 0; step < move_px; step++) {
                            g->world_py++;
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
                            if (g->world_py <= g->min_home_py) {
                                g->world_py = g->min_home_py;
                                g->dir = DIR_DOWN;
                                break;
                            }
                        }
                    }
                    g->x_pos_px = g->world_px + maze_dx - 3;
                    g->y_pos_px = g->world_py - 3;
                    unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                    xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, g->x_pos_px);
                    xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, g->y_pos_px);
                    xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (g->frame * SPRITE_FRAME_SIZE)));
                }
            }
        } else {
            // End of death sequence (frame 306)!
            s_death_seq_timer = 0;

            // Remove 6 radial balls off-screen
            for (int k = 0; k < NSPARKLES; k++) {
                unsigned ball_config = PRIZE_SPARKLE_CONFIG + (k * sizeof(vga_mode5_sprite_t));
                xram0_struct_set(ball_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(ball_config, vga_mode5_sprite_t, y_pos_px, -32);
                xram0_struct_set(ball_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
            }

            if (player.lives == 0) {
                start_results_screen();
            } else {
                s_game_motion_started = true;

                // Restore Pac-Man facing and moving in the last direction before death
                player.dir = s_death_last_dir;
                clear_player_queued_dir();

                uint8_t open_frame = 5;
                switch (player.dir) {
                    case DIR_UP:    open_frame = 0; break;
                    case DIR_DOWN:  open_frame = 2; break;
                    case DIR_LEFT:  open_frame = 4; break;
                    case DIR_RIGHT: open_frame = 6; break;
                }
                player.frame = open_frame;
                xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
            }
        }

        return;
    }

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
        if (s_frightened_stage < 7) {
            s_frightened_stage_countdown--;
            if (s_frightened_stage_countdown == 0) {
                s_frightened_stage++;
                set_frightened_palette_stage(s_frightened_stage);
                s_frightened_stage_countdown = s_frightened_stage_len;
            }
        }
        s_frightened_timer--;
        if (s_frightened_timer == 0) {
            s_ghosts_eaten_chain = 0;
            sfx_set_ambient("ROM:sfxnormal");
            set_frightened_palette(false);
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
    uint16_t home_speed_fp = (get_player_base_speed_fp(speed_lvl) * 85) / 100;

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
            uint16_t enter_speed_fp = get_player_base_speed_fp(speed_lvl) * 2; // 2.0x speed for eaten eyes

            g->dir = DIR_DOWN;
            g->sub_py += enter_speed_fp;
            g->world_py = g->sub_py >> 8;

            if (g->world_py >= row16_y) {
                g->world_py = row16_y;
                g->sub_py = row16_y << 8;

                // Eaten return Step 3: Move horizontally to designated spawn
                // X column, at the same 2.0x-level-speed rate as the descent
                // above (was a hardcoded 1px/frame regardless of level
                // speed, so this leg didn't get faster as the game did).
                if (g->world_px != home_x) {
                    g->dir = (g->world_px < home_x) ? DIR_RIGHT : DIR_LEFT;
                    g->sub_px += enter_speed_fp;
                    uint16_t move_px = g->sub_px >> 8;
                    g->sub_px &= 0x00FF;
                    for (uint16_t step = 0; step < move_px; step++) {
                        if (g->world_px < home_x) g->world_px++;
                        else if (g->world_px > home_x) g->world_px--;
                        if (g->world_px == home_x) break;
                    }
                }

                if (g->world_px == home_x) {
                    // Reached home spawn position (home_x, 16)! Restore normal CHASE mode & start home bounce facing DIR_UP
                    g->mode = GHOST_MODE_CHASE;
                    g->state = GHOST_STATE_HOME_BOUNCE;
                    g->dir = DIR_UP;
                    g->bounce_dist_px = 0; // Reset 80px cooldown tracker
                    g->sub_px = 0;
                    g->sub_py = 0;

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
            uint16_t exit_speed_fp = get_player_base_speed_fp(speed_lvl) >> 2; // 0.25x Pac-Man level speed

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

                // Evaluate target tile to choose best exit direction (DIR_LEFT vs DIR_RIGHT) towards Pac-Man / target
                int16_t target_tx = 0, target_ty = 0;
                if (g->mode == GHOST_MODE_FRIGHTENED) {
                    int16_t pac_tx = (int16_t)(player.world_px / MAZE_TILES_SIZE_PX);
                    target_tx = 23 + (23 - pac_tx);
                } else {
                    compute_ghost_target_tile(i, &target_tx, &target_ty);
                }

                // Tile 23 exit: test stepping left to tile 22 vs stepping right to tile 24
                int16_t diff_left = 22 - target_tx;
                int16_t diff_right = 24 - target_tx;

                g->dir = (diff_left * diff_left <= diff_right * diff_right) ? DIR_LEFT : DIR_RIGHT;
            }
        }
        else if (g->state == GHOST_STATE_OUTSIDE) {
            update_ghost_outside_movement(i);
        }
    }

    // General safety net, not just during maze transitions: catches any
    // ghost that ends up on an invalid tile for whatever reason (e.g. an
    // out-of-bounds escape near the vertical tunnel) and walks it back to
    // the nearest open tile. Only acts on ghosts already confirmed stuck,
    // so safe to run unconditionally every frame.
    check_and_reset_stuck_ghosts();

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

        // Update ghost drawn screen coordinates based on latest world positions
        while (g->world_px < 0) {
            g->world_px += WORLD_WIDTH;
        }
        while (g->world_px >= WORLD_WIDTH) {
            g->world_px -= WORLD_WIDTH;
        }

        int16_t ghost_screen_x = g->world_px + maze_dx;

        // Endless horizontal scrolling wrapping for ghost screen coordinates relative to canvas width
        while (ghost_screen_x < -16) {
            ghost_screen_x += WORLD_WIDTH;
        }
        while (ghost_screen_x > (SCREEN_WIDTH + 16)) {
            ghost_screen_x -= WORLD_WIDTH;
        }

        g->x_pos_px = ghost_screen_x + VISUAL_X_OFFSET;
        g->y_pos_px = g->world_py + VISUAL_Y_OFFSET;

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

    // Check collisions with ghosts after ghost movement & screen position updates
    check_pacman_ghost_collisions();

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

    // --- 6. Render Captured Prize Score Displays (Slots 1 & 2 of GHOST_SCORE_CONFIG) ---
    // Total duration: 120 frames. Frame 1: Frame 2: place at Pac-Man's Y with X offsets -8, 0, 8, 16. Next 7 frames: move up 1px/frame. Remaining 112 frames: hold.
    static const int8_t PRIZE_DIGIT_X_OFFSETS[4] = { -8, 0, 8, 16 };

    for (int p_idx = 0; p_idx < NPRIZES; p_idx++) {
        prize_score_anim_t *pa = &s_prize_score_anims[p_idx];
        uint8_t slot_offset = (1 + p_idx) * 4; // Slot 1 starts at 4, Slot 2 starts at 8

        if (!pa->active) {
            for (int d = 0; d < 4; d++) {
                unsigned score_config = GHOST_SCORE_CONFIG + ((slot_offset + d) * sizeof(vga_mode5_sprite_t));
                xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, -32);
                xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
            }
        } else {
            // Frame 1: prize removed, score hidden
            // Frame 2..8: rise 1px/frame (up to 7px total shift)
            // Frame 8..120: hold position (-7px shift)
            if (pa->anim_frame >= 2) {
                uint8_t rise_px = (pa->anim_frame <= 8) ? (pa->anim_frame - 1) : 7;
                int16_t pm_drawn_x = pa->pacman_world_x + maze_dx - 3;
                int16_t pm_drawn_y = (pa->pacman_world_y - 3) - rise_px;

                for (int d = 0; d < 4; d++) {
                    unsigned score_config = GHOST_SCORE_CONFIG + ((slot_offset + d) * sizeof(vga_mode5_sprite_t));
                    int16_t digit_x = pm_drawn_x + PRIZE_DIGIT_X_OFFSETS[d];
                    int16_t digit_y = pm_drawn_y;

                    xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, digit_x);
                    xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, digit_y);
                    xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (pa->digits[d] * SPRITE_FRAME_SIZE)));
                }
            } else {
                // Frame 1: park sprites off-screen
                for (int d = 0; d < 4; d++) {
                    unsigned score_config = GHOST_SCORE_CONFIG + ((slot_offset + d) * sizeof(vga_mode5_sprite_t));
                    xram0_struct_set(score_config, vga_mode5_sprite_t, x_pos_px, -32);
                    xram0_struct_set(score_config, vga_mode5_sprite_t, y_pos_px, -32);
                    xram0_struct_set(score_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
                }
            }

            pa->anim_frame++;
            if (pa->anim_frame > 120) {
                pa->active = false;
            }
        }
    }
}