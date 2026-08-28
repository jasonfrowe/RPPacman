#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "player.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"
#include "prizes.h"
#include "ghost.h"
#include "opl.h"
#include "hiscores.h"

static int8_t queued_dir = DIR_NONE;

// Dot-eat movement freeze. The original arcade (pacman.holenet.info)
// stalls 1 frame per regular dot and 3 per power pellet; Pac-Man CE's NES
// port visibly eases that off -- confirmed by the user against a real CE
// playthrough recording: only every *other* regular dot stalls (1 frame),
// and a power pellet is always a flat 1-frame stall, not 3. Counts down
// once per frame in player_update_motion's movement section; input and
// turn queueing keep working normally during the stall, only forward
// movement is held.
static uint8_t s_eat_stall_frames = 0;
static bool s_dot_stall_parity = false; // toggles each regular dot eaten; stalls on true

bool is_wall_tile(int16_t world_x, int16_t world_y) {
    while (world_x < 0) world_x += WORLD_WIDTH;
    while (world_x >= WORLD_WIDTH) world_x -= WORLD_WIDTH;
    while (world_y < 0) world_y += WORLD_HEIGHT;
    while (world_y >= WORLD_HEIGHT) world_y -= WORLD_HEIGHT;

    uint16_t tile_x = (uint16_t)(world_x / MAZE_TILES_SIZE_PX);
    uint16_t tile_y = (uint16_t)(world_y / MAZE_TILES_SIZE_PX);

    if (tile_x >= MAZE_MAP_WIDTH) tile_x %= MAZE_MAP_WIDTH;
    if (tile_y >= MAZE_MAP_HEIGHT) tile_y %= MAZE_MAP_HEIGHT;

    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;

    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    uint8_t tile_index = RIA.rw0;

    // Tile taxonomy: 0 = blank floor, 116-124 = pellet/prize/score-popup
    // overlays (walkable), 1-115 = real wall art, 125-127 = out-of-bounds
    // void markers -- not real wall geometry, but still "can't stand
    // here" (confirmed present in the actual map data at a handful of
    // border-seam tiles, e.g. the one non-open column in the otherwise
    // wide-open row below the top border). This function previously only
    // blocked 1-115, silently treating 125-127 as walkable. Since
    // can_step_dir() (and therefore every ghost direction decision, in
    // both the main candidate loop and every fallback branch) is gated
    // on this function, that let a ghost walk onto a void tile that
    // looks like ordinary open floor from its neighbors -- explaining
    // reports of a ghost parked exactly on an out-of-bounds tile,
    // oscillating in place because most directions out of a void tile
    // still read as "safe" by the same broken check.
    return (tile_index > 0 && tile_index < 116) || (tile_index >= 125);
}

// Is (world_x, world_y)'s tile safe to stand on -- used only to gate the
// vertical tunnel wrap jump below, which (unlike normal movement) has no
// following per-pixel wall check to catch a bad landing. Tile taxonomy:
// 0 = blank floor, 116-124 = pellet/prize/score-popup overlays (walkable),
// 1-115 = wall art, 125-127 = out-of-bounds void -- all of the latter two
// ranges are "can't stand here."
static bool is_safe_landing_tile(int16_t world_x, int16_t world_y) {
    while (world_x < 0) world_x += WORLD_WIDTH;
    while (world_x >= WORLD_WIDTH) world_x -= WORLD_WIDTH;
    while (world_y < 0) world_y += WORLD_HEIGHT;
    while (world_y >= WORLD_HEIGHT) world_y -= WORLD_HEIGHT;

    uint16_t tile_x = (uint16_t)(world_x / MAZE_TILES_SIZE_PX);
    uint16_t tile_y = (uint16_t)(world_y / MAZE_TILES_SIZE_PX);

    if (tile_x >= MAZE_MAP_WIDTH) tile_x %= MAZE_MAP_WIDTH;
    if (tile_y >= MAZE_MAP_HEIGHT) tile_y %= MAZE_MAP_HEIGHT;

    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;

    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    uint8_t tile_index = RIA.rw0;

    return tile_index == 0 || (tile_index >= 116 && tile_index <= 124);
}

void get_dir_offset(int8_t dir, int8_t *dx, int8_t *dy) {
    *dx = 0;
    *dy = 0;
    switch (dir) {
        case DIR_RIGHT: *dx = 1;  break;
        case DIR_LEFT:  *dx = -1; break;
        case DIR_DOWN:  *dy = 1;  break;
        case DIR_UP:    *dy = -1; break;
    }
}

bool can_step_dir(int16_t world_x, int16_t world_y, int8_t dir) {
    if (dir == DIR_NONE) return false;
    int8_t dx, dy;
    get_dir_offset(dir, &dx, &dy);
    return !is_wall_tile(world_x + dx * 8, world_y + dy * 8);
}

#define MAX_SCORE_POPUPS 10
static eaten_tile_popup_t s_score_popups[MAX_SCORE_POPUPS];
static uint8_t s_popup_count = 0;

static uint8_t get_score_tile_index(uint32_t pts) {
    switch (pts) {
        case 10: return 120;
        case 20: return 121;
        case 30: return 122;
        case 40: return 123;
        case 50: return 124;
        default: return 120;
    }
}

static uint32_t get_current_dot_value(uint16_t dots_eaten) {
    if (dots_eaten < 60)   return 10;
    if (dots_eaten < 120)  return 20;
    if (dots_eaten < 180)  return 30;
    if (dots_eaten < 240)  return 40;
    return 50;
}

void reset_player_on_death(void) {
    player.pellets_eaten = 0; // Reset dot multiplier back to 10 points (0-59 dots tier)
}

void push_score_popup(uint16_t tile_x, uint16_t tile_y, uint8_t score_tile) {
    // If popups queue is full (10 items), expire the oldest entry immediately to blank (tile 0)
    if (s_popup_count >= MAX_SCORE_POPUPS) {
        uint16_t old_offset = s_score_popups[0].tile_y * MAZE_MAP_WIDTH + s_score_popups[0].tile_x;
        RIA.addr0 = MAZE_MAP_DATA + old_offset;
        RIA.step0 = 1;
        RIA.rw0 = 0;

        for (uint8_t i = 0; i < MAX_SCORE_POPUPS - 1; i++) {
            s_score_popups[i] = s_score_popups[i + 1];
        }
        s_popup_count--;
    }

    // Add new popup to array
    s_score_popups[s_popup_count].tile_x = tile_x;
    s_score_popups[s_popup_count].tile_y = tile_y;
    s_score_popups[s_popup_count].timer = 40; // 40 frames countdown
    s_score_popups[s_popup_count].active = true;
    s_popup_count++;

    // Write score tile index (120..124) into XRAM tile map
    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;
    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    RIA.rw0 = score_tile;
}

// Nothing else ever reset this queue -- a pellet-score popup (tile
// 120-124) still counting down its 40-frame timer at game-over would
// survive into the next game, and once its stale timer ran out there,
// update_score_popups() would blank whatever tile now actually sits at
// its recorded (tile_x, tile_y) -- a real wall or an uneaten dot in the
// fresh game -- reading as maze tiles randomly disappearing shortly
// after a restart. Clearing the count is enough: update_score_popups()
// only ever looks at indices below s_popup_count.
void reset_score_popups(void) {
    s_popup_count = 0;
}

// Unlike reset_score_popups(), this also blanks each active popup's
// current tile before dropping it -- for mid-game freezes (Pac-Man's
// 305-frame death sequence, a ghost's 30-frame eat pause) where the maze
// isn't about to be redrawn/re-copied, so a merely-forgotten popup would
// stay frozen showing its score digit for the entire freeze -- up to 5
// real seconds for a death -- reading as a leftover, un-eaten pellet
// sitting on the field. player_update_motion() (and therefore
// update_score_popups()) doesn't run at all while
// is_death_sequence_active()/is_eat_pause_active() are true, so nothing
// would otherwise touch these until the freeze ends.
void clear_active_pellet_popups(void) {
    for (uint8_t i = 0; i < s_popup_count; i++) {
        uint16_t offset = s_score_popups[i].tile_y * MAZE_MAP_WIDTH + s_score_popups[i].tile_x;
        RIA.addr0 = MAZE_MAP_DATA + offset;
        RIA.step0 = 1;
        RIA.rw0 = 0;
    }
    s_popup_count = 0;
}

static void update_score_popups(void) {
    for (int8_t i = 0; i < (int8_t)s_popup_count; i++) {
        if (!s_score_popups[i].active) continue;

        if (s_score_popups[i].timer > 0) {
            s_score_popups[i].timer--;
        }

        if (s_score_popups[i].timer == 0) {
            // Revert tile back to 0 (blank)
            uint16_t offset = s_score_popups[i].tile_y * MAZE_MAP_WIDTH + s_score_popups[i].tile_x;
            RIA.addr0 = MAZE_MAP_DATA + offset;
            RIA.step0 = 1;
            RIA.rw0 = 0;

            // Remove item from popup list
            for (uint8_t j = (uint8_t)i; j < s_popup_count - 1; j++) {
                s_score_popups[j] = s_score_popups[j + 1];
            }
            s_popup_count--;
            i--; // Adjust loop index after shift
        }
    }
}

static uint32_t s_next_extra_life_threshold = 20000;

// Only ever increases as extra lives are awarded within a run, so a fresh
// game must reset it explicitly -- otherwise it carries over an escalated
// threshold from a previous game in the same session, and the player can
// never earn another extra life.
void reset_extra_life_threshold(void) {
    s_next_extra_life_threshold = 20000;
}

// Points earned per 10-second interval per category, for the results
// screen's histogram. Indexed by get_game_elapsed_frames()/600, clamped
// to the last bucket -- the game's own 5-minute cap (18000 frames) means
// that clamp is never actually exercised in practice.
static uint16_t s_score_history[SCORE_HISTORY_BUCKETS][SCORE_CAT_COUNT];

void reset_score_history(void) {
    for (uint8_t b = 0; b < SCORE_HISTORY_BUCKETS; b++) {
        for (uint8_t c = 0; c < SCORE_CAT_COUNT; c++) {
            s_score_history[b][c] = 0;
        }
    }
}

uint16_t get_score_history_bucket(uint8_t bucket, uint8_t category) {
    if (bucket >= SCORE_HISTORY_BUCKETS || category >= SCORE_CAT_COUNT) return 0;
    return s_score_history[bucket][category];
}

void add_player_score(uint32_t pts, uint8_t category) {
    player.score += pts;
    player.score_by_cat[category] += pts;
    update_player_score_display(player.score);

    // Live "HI" score tracking: once the current run passes the
    // persisted all-time top score, show the running score there instead
    // -- matches classic arcade behavior where the HI digits update in
    // real time rather than waiting for game-over. The persisted table
    // itself is only updated later, via hiscores_insert() from the
    // congrats screen if this run's final score actually makes the
    // top 10.
    if (player.score > hiscores_get_score(0)) {
        update_hiscore_display(player.score);
    }

    uint16_t bucket = get_game_elapsed_frames() / 600;
    if (bucket >= SCORE_HISTORY_BUCKETS) bucket = SCORE_HISTORY_BUCKETS - 1;
    s_score_history[bucket][category] += pts;

    while (player.score >= s_next_extra_life_threshold) {
        player.lives++;
        update_player_lives_display(player.lives);
        trigger_extra_life_blink();
        sfx_play("ROM:sfxextralife", SFX_PRIORITY_TOP);
        s_next_extra_life_threshold += 20000;
    }
}

static void check_and_eat_pellet(int16_t world_x, int16_t world_y) {
    // Check center of visual sprite (drawn_x + 8, drawn_y + 8) => (world_x + 5, world_y + 5)
    int16_t check_x = world_x + 5;
    int16_t check_y = world_y + 5;

    if (check_x < 0) check_x += WORLD_WIDTH;
    else if (check_x >= WORLD_WIDTH) check_x -= WORLD_WIDTH;
    if (check_y < 0) check_y += WORLD_HEIGHT;
    else if (check_y >= WORLD_HEIGHT) check_y -= WORLD_HEIGHT;

    // Use fast 16-bit shift instead of 32-bit division (/ 8 -> >> 3)
    uint16_t tile_x = (uint16_t)check_x >> 3;
    uint16_t tile_y = (uint16_t)check_y >> 3;

    if (tile_x >= MAZE_MAP_WIDTH) tile_x %= MAZE_MAP_WIDTH;
    if (tile_y >= MAZE_MAP_HEIGHT) tile_y %= MAZE_MAP_HEIGHT;

    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;

    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    uint8_t tile_index = RIA.rw0;

    // Check if tile is a dot (116) or power pellet (117)
    if (tile_index == 116 || tile_index == 117) {
        // Clear pellet tile in XRAM map memory to 0 (blank tile)
        RIA.addr0 = MAZE_MAP_DATA + offset;
        RIA.step0 = 1;
        RIA.rw0 = 0;

        // Only a regular dot gets the chomp stinger -- eating a power
        // pellet already gets its own, bigger audible cue when the
        // frightened ambient loop takes over the SFX channel.
        if (tile_index == 116) {
            sfx_play("ROM:sfxpellet", SFX_PRIORITY_MEDIUM);
        }

        uint32_t dot_pts = get_current_dot_value(player.pellets_eaten);
        player.pellets_eaten++;
        add_player_score(dot_pts, SCORE_CAT_PELLET);

        uint8_t score_tile = get_score_tile_index(dot_pts);
        push_score_popup(tile_x, tile_y, score_tile);

        // CE's eased-off dot-eat freeze (see s_eat_stall_frames comment):
        // power pellet is always 1 frame; a regular dot only stalls on
        // every other one. Take the max rather than overwrite/add, in
        // case a stall from a previous eat this same frame is pending.
        uint8_t stall = 0;
        if (tile_index == 117) {
            stall = 1;
        } else {
            s_dot_stall_parity = !s_dot_stall_parity;
            if (s_dot_stall_parity) stall = 1;
        }
        if (stall > s_eat_stall_frames) s_eat_stall_frames = stall;

        // If a Power Pellet / Super Pellet (117) was eaten, trigger Frightened mode
        if (tile_index == 117) {
            trigger_power_pellet_frightened();
        }

        // Update side pellet counter and check if clearing this pellet triggered prize spawning
        on_pellet_eaten(tile_x);
        update_side_pellets_status();
    }

    // Check if Pacman is consuming an active prize using visual drawn coordinates
    int16_t drawn_x = world_x + VISUAL_X_OFFSET;
    int16_t drawn_y = world_y + VISUAL_Y_OFFSET;
    check_and_eat_prize(drawn_x, drawn_y);
}

// 8.8 Fixed-Point Speed Lookup Table across 22 Prize Levels (Cherry to Crown)
// 0x0100 = 1.000 px/frame (Cherry) -> 0x0255 = 2.332 px/frame (Crown)
// Max cap calibrated to a measured real-hardware top speed pattern of
// 3,2,2,3,2,2 px/frame (avg 2.333 px/frame).
const uint16_t SPEED_TABLE[22] = {
    0x0100, // Level 0  (Cherry):            1.000 px/frame
    0x0110, // Level 1  (Strawberry):        1.062 px/frame
    0x0120, // Level 2  (Orange):            1.125 px/frame
    0x0131, // Level 3  (Apple):             1.191 px/frame
    0x0141, // Level 4  (Melon):             1.254 px/frame
    0x0151, // Level 5  (Banana):            1.316 px/frame
    0x0161, // Level 6  (Peach):             1.379 px/frame
    0x0172, // Level 7  (Galaxian Boss):     1.445 px/frame
    0x0182, // Level 8  (Bell):              1.508 px/frame
    0x0192, // Level 9  (Key):               1.570 px/frame
    0x01A2, // Level 10 (Coffee):            1.633 px/frame
    0x01B3, // Level 11 (Cake):              1.699 px/frame
    0x01C3, // Level 12 (Galaga):            1.762 px/frame
    0x01D3, // Level 13 (Gaplus Drone):      1.824 px/frame
    0x01E3, // Level 14 (Hamburger):         1.887 px/frame
    0x01F4, // Level 15 (Fried Egg):         1.953 px/frame
    0x0204, // Level 16 (Candy):             2.016 px/frame
    0x0214, // Level 17 (Four-Leaf Clover):  2.078 px/frame
    0x0224, // Level 18 (Diamond):           2.141 px/frame
    0x0235, // Level 19 (Heart):             2.207 px/frame
    0x0245, // Level 20 (Samurai Helmet):    2.270 px/frame
    0x0255, // Level 21 (Crown):             2.332 px/frame (Max Cap)
};

// EXTRA's own 22-level Pac-Man speed ramp -- starts faster than NORMAL's
// own level 0 (~1.8 px/frame, matching a Championship-Edition-style
// harder start) but lands on the exact same level-21 cap NORMAL already
// tops out at (0x0255): a straight line in 8.8 fixed point, 0x01D7 plus a
// flat +0x06 per level, so it never invents a new top speed of its own.
const uint16_t EXTRA_SPEED_TABLE[22] = {
    0x01D7, // Level 0:  1.840 px/frame
    0x01DD, // Level 1:  1.863 px/frame
    0x01E3, // Level 2:  1.887 px/frame
    0x01E9, // Level 3:  1.910 px/frame
    0x01EF, // Level 4:  1.934 px/frame
    0x01F5, // Level 5:  1.957 px/frame
    0x01FB, // Level 6:  1.980 px/frame
    0x0201, // Level 7:  2.004 px/frame
    0x0207, // Level 8:  2.027 px/frame
    0x020D, // Level 9:  2.051 px/frame
    0x0213, // Level 10: 2.074 px/frame
    0x0219, // Level 11: 2.098 px/frame
    0x021F, // Level 12: 2.121 px/frame
    0x0225, // Level 13: 2.145 px/frame
    0x022B, // Level 14: 2.168 px/frame
    0x0231, // Level 15: 2.191 px/frame
    0x0237, // Level 16: 2.215 px/frame
    0x023D, // Level 17: 2.238 px/frame
    0x0243, // Level 18: 2.262 px/frame
    0x0249, // Level 19: 2.285 px/frame
    0x024F, // Level 20: 2.309 px/frame
    0x0255, // Level 21: 2.332 px/frame (matches SPEED_TABLE[21] exactly)
};

static uint16_t s_speed_subpixel_x = 0;
static uint16_t s_speed_subpixel_y = 0;

static game_mode_t s_game_mode = GAME_MODE_NORMAL;

void set_game_mode(game_mode_t mode) {
    s_game_mode = mode;
}

game_mode_t get_game_mode(void) {
    return s_game_mode;
}

// Picks SPEED_TABLE vs EXTRA_SPEED_TABLE by game mode -- every call site
// that used to index SPEED_TABLE[speed_lvl] directly (in both this file
// and ghost.c, for the several ghost-house sub-speeds that scale off
// Pac-Man's own base rate) now goes through this instead.
uint16_t get_player_base_speed_fp(uint8_t speed_lvl) {
    return (s_game_mode == GAME_MODE_EXTRA) ? EXTRA_SPEED_TABLE[speed_lvl] : SPEED_TABLE[speed_lvl];
}

// Same prize-count-driven index for both modes -- EXTRA's 22 mazes (vs
// NORMAL's 11) still award one prize per side-clear, so this naturally
// reaches 21 right as the last EXTRA maze clears. Only the table it's
// used to index (get_player_base_speed_fp() above, get_ghost_base_speed_fp()
// in ghost.c) differs by mode.
uint8_t get_speed_level_index(void) {
    uint8_t max_count = (left_prize_count > right_prize_count) ? left_prize_count : right_prize_count;
    if (max_count > 21) max_count = 21;
    return max_count;
}

static uint8_t s_prev_dpad_mask = 0;

void clear_player_queued_dir(void) {
    queued_dir = DIR_NONE;
    s_prev_dpad_mask = 0;
}

void player_update_motion(const input_actions_t *actions) {
    if (is_eat_pause_active() || is_death_sequence_active()) {
        return;
    }

    // 1. Process D-pad input transitions
    uint8_t dpad_mask = 0;
    if (actions->up)    dpad_mask |= 1;
    if (actions->down)  dpad_mask |= 2;
    if (actions->left)  dpad_mask |= 4;
    if (actions->right) dpad_mask |= 8;

    // Only evaluate and queue a new direction when the D-pad button combination CHANGES
    if (dpad_mask != s_prev_dpad_mask) {
        uint8_t newly_pressed = dpad_mask & ~s_prev_dpad_mask;
        s_prev_dpad_mask = dpad_mask;

        int8_t new_dir = DIR_NONE;

        if (newly_pressed != 0) {
            // A new button (or diagonal combination) was newly pressed
            if (player.dir == DIR_LEFT || player.dir == DIR_RIGHT) {
                // Moving horizontally: prioritize newly pressed perpendicular (vertical) direction
                if (newly_pressed & 1)      new_dir = DIR_UP;
                else if (newly_pressed & 2) new_dir = DIR_DOWN;
                else if (newly_pressed & 4) new_dir = DIR_LEFT;
                else if (newly_pressed & 8) new_dir = DIR_RIGHT;
            } else if (player.dir == DIR_UP || player.dir == DIR_DOWN) {
                // Moving vertically: prioritize newly pressed perpendicular (horizontal) direction
                if (newly_pressed & 4)      new_dir = DIR_LEFT;
                else if (newly_pressed & 8) new_dir = DIR_RIGHT;
                else if (newly_pressed & 1) new_dir = DIR_UP;
                else if (newly_pressed & 2) new_dir = DIR_DOWN;
            } else {
                // Stationary: pick any newly pressed direction
                if (newly_pressed & 1)      new_dir = DIR_UP;
                else if (newly_pressed & 2) new_dir = DIR_DOWN;
                else if (newly_pressed & 4) new_dir = DIR_LEFT;
                else if (newly_pressed & 8) new_dir = DIR_RIGHT;
            }
        } else if (dpad_mask != 0) {
            // A button was released, but other D-pad buttons remain held
            if (player.dir == DIR_LEFT || player.dir == DIR_RIGHT) {
                if (dpad_mask & 1)      new_dir = DIR_UP;
                else if (dpad_mask & 2) new_dir = DIR_DOWN;
                else if (dpad_mask & 4) new_dir = DIR_LEFT;
                else if (dpad_mask & 8) new_dir = DIR_RIGHT;
            } else if (player.dir == DIR_UP || player.dir == DIR_DOWN) {
                if (dpad_mask & 4)      new_dir = DIR_LEFT;
                else if (dpad_mask & 8) new_dir = DIR_RIGHT;
                else if (dpad_mask & 1) new_dir = DIR_UP;
                else if (dpad_mask & 2) new_dir = DIR_DOWN;
            } else {
                if (dpad_mask & 1)      new_dir = DIR_UP;
                else if (dpad_mask & 2) new_dir = DIR_DOWN;
                else if (dpad_mask & 4) new_dir = DIR_LEFT;
                else if (dpad_mask & 8) new_dir = DIR_RIGHT;
            }
        }

        if (new_dir != DIR_NONE && new_dir != player.dir) {
            queued_dir = new_dir;
        }
    }

    // If Pac-Man is stationary against a wall, check if any currently held D-pad direction is open
    if (player.dir == DIR_NONE && dpad_mask != 0) {
        if ((dpad_mask & 1) && can_step_dir(player.world_px, player.world_py, DIR_UP)) {
            player.dir = DIR_UP;
            queued_dir = DIR_NONE;
        } else if ((dpad_mask & 2) && can_step_dir(player.world_px, player.world_py, DIR_DOWN)) {
            player.dir = DIR_DOWN;
            queued_dir = DIR_NONE;
        } else if ((dpad_mask & 4) && can_step_dir(player.world_px, player.world_py, DIR_LEFT)) {
            player.dir = DIR_LEFT;
            queued_dir = DIR_NONE;
        } else if ((dpad_mask & 8) && can_step_dir(player.world_px, player.world_py, DIR_RIGHT)) {
            player.dir = DIR_RIGHT;
            queued_dir = DIR_NONE;
        }
    }

    // 2. Process direction change
    if (queued_dir != DIR_NONE) {
        int16_t current_drawn_y = player.world_py + VISUAL_Y_OFFSET;

        // Ignore left/right inputs when Pac-Man is in vertical tunnel regions (< 32px or > 215px)
        bool is_in_vertical_tunnel = (current_drawn_y < 32) || ((current_drawn_y + SPRITE_SIZE_PX) > 215);
        if (is_in_vertical_tunnel && (queued_dir == DIR_LEFT || queued_dir == DIR_RIGHT)) {
            queued_dir = DIR_NONE;
        }

        if (queued_dir != DIR_NONE) {
            // Immediate 180-degree reversal is always allowed
            if ((player.dir == DIR_RIGHT && queued_dir == DIR_LEFT) ||
                (player.dir == DIR_LEFT && queued_dir == DIR_RIGHT) ||
                (player.dir == DIR_UP && queued_dir == DIR_DOWN) ||
                (player.dir == DIR_DOWN && queued_dir == DIR_UP)) {
                player.dir = queued_dir;
                queued_dir = DIR_NONE;
            } else if (player.dir == DIR_NONE) {
                if (can_step_dir(player.world_px, player.world_py, queued_dir)) {
                    player.dir = queued_dir;
                    queued_dir = DIR_NONE;
                }
            } else {
                // 90-degree turning with pre-turn & post-turn cornering window (within 3px of 8px tile intersection)
                int16_t mod_x = player.world_px % MAZE_TILES_SIZE_PX;
                if (mod_x < 0) mod_x += MAZE_TILES_SIZE_PX;
                int16_t mod_y = player.world_py % MAZE_TILES_SIZE_PX;
                if (mod_y < 0) mod_y += MAZE_TILES_SIZE_PX;

                if ((player.dir == DIR_LEFT || player.dir == DIR_RIGHT) &&
                    (queued_dir == DIR_UP || queued_dir == DIR_DOWN)) {
                    if (mod_x <= 3 || mod_x >= 5) {
                        int16_t target_grid_x = (mod_x <= 3) ? (player.world_px - mod_x) : (player.world_px + (8 - mod_x));
                        if (can_step_dir(target_grid_x, player.world_py, queued_dir)) {
                            player.world_px = target_grid_x;
                            player.dir = queued_dir;
                            queued_dir = DIR_NONE;
                        }
                    }
                } else if ((player.dir == DIR_UP || player.dir == DIR_DOWN) &&
                           (queued_dir == DIR_LEFT || queued_dir == DIR_RIGHT)) {
                    if (mod_y <= 3 || mod_y >= 5) {
                        int16_t target_grid_y = (mod_y <= 3) ? (player.world_py - mod_y) : (player.world_py + (8 - mod_y));
                        if (can_step_dir(player.world_px, target_grid_y, queued_dir)) {
                            player.world_py = target_grid_y;
                            player.dir = queued_dir;
                            queued_dir = DIR_NONE;
                        }
                    }
                }
            }
        }
    }

    // 3. Continuous arcade movement using 8.8 fixed-point speed table
    if (s_eat_stall_frames > 0) {
        // Frozen this frame from a dot/power-pellet eat: no subpixel
        // accumulation, no movement, and no reset -- resumes exactly
        // where it left off once the stall ends.
        s_eat_stall_frames--;
    } else if (player.dir != DIR_NONE) {
        int8_t dx, dy;
        get_dir_offset(player.dir, &dx, &dy);

        uint8_t speed_lvl = get_speed_level_index();
        uint16_t speed_fp = get_player_base_speed_fp(speed_lvl);

        int16_t move_pixels = 0;
        if (dx != 0) {
            s_speed_subpixel_x += speed_fp;
            move_pixels = s_speed_subpixel_x >> 8;
            s_speed_subpixel_x &= 0x00FF;
        } else if (dy != 0) {
            s_speed_subpixel_y += speed_fp;
            move_pixels = s_speed_subpixel_y >> 8;
            s_speed_subpixel_y &= 0x00FF;
        }

        for (int16_t step = 0; step < move_pixels; step++) {
            // Check direction change at every 8px grid intersection on every 1px step
            if (player.world_px % MAZE_TILES_SIZE_PX == 0 && player.world_py % MAZE_TILES_SIZE_PX == 0) {
                if (queued_dir != DIR_NONE) {
                    if ((player.dir == DIR_RIGHT && queued_dir == DIR_LEFT) ||
                        (player.dir == DIR_LEFT && queued_dir == DIR_RIGHT) ||
                        (player.dir == DIR_UP && queued_dir == DIR_DOWN) ||
                        (player.dir == DIR_DOWN && queued_dir == DIR_UP)) {
                        player.dir = queued_dir;
                        queued_dir = DIR_NONE;
                    } else if (can_step_dir(player.world_px, player.world_py, queued_dir)) {
                        player.dir = queued_dir;
                        queued_dir = DIR_NONE;
                    }
                }
                get_dir_offset(player.dir, &dx, &dy);
            }

            int16_t next_px = player.world_px + dx;
            int16_t next_py = player.world_py + dy;

            // Check if step forward is blocked by a wall
            bool is_blocked = false;
            if (player.world_px % MAZE_TILES_SIZE_PX == 0 && player.world_py % MAZE_TILES_SIZE_PX == 0) {
                // At tile boundary: check next full tile in direction of motion
                is_blocked = !can_step_dir(player.world_px, player.world_py, player.dir);
            }

            if (!is_blocked) {
                player.world_px = next_px;
                player.world_py = next_py;

                // Vertical tunnel wrapping using Pac-Man's drawn screen
                // position. Checked after every single pixel step (not
                // once per frame after this loop) -- at higher speeds
                // (multiple pixels per frame), Pac-Man could reach the
                // real wall just past the wrap-eligible zone and get
                // blocked (is_blocked above, which sets player.dir to
                // DIR_NONE and breaks out of this loop) within the SAME
                // frame the wrap should have fired. A once-per-frame
                // check running only after the loop would then see
                // player.dir already reset to DIR_NONE and never fire,
                // leaving Pac-Man stopped dead at the tunnel mouth
                // instead of wrapping through -- reproducible only at
                // higher levels' speeds, matching reports of the tunnel
                // occasionally not working.
                //
                // Only taken when the destination tile is verified safe
                // -- the jump only lands on open floor at the handful of
                // columns where the top and bottom border rows are both
                // unwalled at that column (see the matching guard in
                // ghost.c's update_ghost_outside_movement for the full
                // explanation). At any other column, skip the jump and
                // let the normal per-pixel wall check (is_blocked, above)
                // stop Pac-Man at the real border wall instead.
                int16_t drawn_y_step = player.world_py + VISUAL_Y_OFFSET;
                if (player.dir == DIR_DOWN && (drawn_y_step + SPRITE_SIZE_PX) >= VERTICAL_TUNNEL_TRIGGER_BOTTOM_DRAWN_Y) {
                    int16_t wrapped_py = player.world_py - VERTICAL_TUNNEL_WRAP_PX;
                    if (is_safe_landing_tile(player.world_px, wrapped_py)) {
                        player.world_py = wrapped_py;
                    }
                } else if (player.dir == DIR_UP && drawn_y_step <= VERTICAL_TUNNEL_TRIGGER_TOP_DRAWN_Y) {
                    int16_t wrapped_py = player.world_py + VERTICAL_TUNNEL_WRAP_PX;
                    if (is_safe_landing_tile(player.world_px, wrapped_py)) {
                        player.world_py = wrapped_py;
                    }
                }

                // Check and eat pellets/prizes on every 1px step to ensure no pickups are skipped
                check_and_eat_pellet(player.world_px, player.world_py);
            } else {
                player.dir = DIR_NONE; // Stop at wall intersection
                s_speed_subpixel_x = 0;
                s_speed_subpixel_y = 0;
                break;
            }
        }
    } else {
        s_speed_subpixel_x = 0;
        s_speed_subpixel_y = 0;
    }

    // Check if Pacman ate a pellet on current tile
    check_and_eat_pellet(player.world_px, player.world_py);

    // Update active score popups (30 frame timer & queue decay)
    update_score_popups();

    // 4. Pac-Man mouth chomping animation
    static uint8_t anim_timer = 0;
    static bool mouth_open = false;
    static int8_t last_dir = DIR_LEFT;

    if (player.dir != DIR_NONE) {
        last_dir = player.dir; // Track facing direction while moving

        anim_timer++;
        if (anim_timer >= 6) { // Toggle frame every 6 ticks (~10Hz animation)
            anim_timer = 0;
            mouth_open = !mouth_open;
        }

        uint8_t base_frame = 4;
        switch (player.dir) {
            case DIR_UP:    base_frame = 0; break;
            case DIR_DOWN:  base_frame = 2; break;
            case DIR_LEFT:  base_frame = 4; break;
            case DIR_RIGHT: base_frame = 6; break;
        }

        player.frame = base_frame + (mouth_open ? 0 : 1);
    } else {
        // When stationary/idle, show open mouth frame for last facing direction
        uint8_t open_frame = 4;
        switch (last_dir) {
            case DIR_UP:    open_frame = 0; break;
            case DIR_DOWN:  open_frame = 2; break;
            case DIR_LEFT:  open_frame = 4; break;
            case DIR_RIGHT: open_frame = 6; break;
        }
        player.frame = open_frame;
    }

    // 5. Endless wrap Pac-Man in world space
    if (player.world_px < 0) {
        player.world_px += WORLD_WIDTH;
    } else if (player.world_px >= WORLD_WIDTH) {
        player.world_px -= WORLD_WIDTH;
    }

    // Screen Y tracks world Y with visual offset
    player.y_pos_px = player.world_py + VISUAL_Y_OFFSET;

    // Endless scrolling offset for the maze tilemap
    maze_dx = player.x_pos_px - player.world_px;

    int16_t visual_x = player.x_pos_px + VISUAL_X_OFFSET;

    // Update hardware registers
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, visual_x);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));

    // Update Ghost screen positions relative to endless maze scroll (with visual offsets)
    for (int i = 0; i < NGHOSTS; i++) {
        while (ghosts[i].world_px < 0) {
            ghosts[i].world_px += WORLD_WIDTH;
        }
        while (ghosts[i].world_px >= WORLD_WIDTH) {
            ghosts[i].world_px -= WORLD_WIDTH;
        }

        int16_t ghost_screen_x = ghosts[i].world_px + maze_dx;

        // Endless horizontal scrolling wrapping for ghost screen coordinates relative to canvas width
        while (ghost_screen_x < -16) {
            ghost_screen_x += WORLD_WIDTH;
        }
        while (ghost_screen_x > (SCREEN_WIDTH + 16)) {
            ghost_screen_x -= WORLD_WIDTH;
        }

        ghosts[i].x_pos_px = ghost_screen_x + VISUAL_X_OFFSET;
        ghosts[i].y_pos_px = ghosts[i].world_py + VISUAL_Y_OFFSET;

        uint16_t current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, ghosts[i].x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, ghosts[i].y_pos_px);
    }

    // Check collisions with ghosts after Pac-Man movement update
    check_pacman_ghost_collisions();
}