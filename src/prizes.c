#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "prizes.h"
#include "player.h"
#include "ghost.h"

uint8_t left_side_level = 0;
uint8_t right_side_level = 0;
uint8_t left_prize_count = 0;
uint8_t right_prize_count = 0;

static bool left_prize_active = false;
static bool right_prize_active = false;
static uint8_t left_prize_sprite = 49;
static uint8_t right_prize_sprite = 49;

static uint8_t get_side_prize_sprite_index(uint8_t count) {
    uint16_t idx = 49 + (uint16_t)count;
    if (idx > 70) idx = 70;
    return (uint8_t)idx;
}

#define SIDE_MAP_COLS 19

typedef struct {
    bool active;
    bool munchers_active;
    bool is_right_side;
    uint8_t target_level;
    int8_t current_col_step; // 0..18
    uint8_t frame_timer;     // counts 0, 1 -> update column every 2 frames
    uint8_t anim_frame_idx;  // muncher sprite animation index
    uint16_t elapsed_frame;  // total elapsed frames for this transition
} maze_transition_t;

static maze_transition_t s_transitions[2]; // Index 0: Left side, Index 1: Right side

void reset_prizes_and_mazes_level(void) {
    left_side_level = 0;
    right_side_level = 0;
    left_prize_count = 0;
    right_prize_count = 0;
    left_prize_active = false;
    right_prize_active = false;

    // Reset maze transitions
    memset(s_transitions, 0, sizeof(s_transitions));

    // Reset active prizes sprites offscreen
    for (int i = 0; i < NPRIZES; i++) {
        prizes[i].frame = 48;
        prizes[i].sparkle_frame = 48;
        unsigned current_prize_config = PRIZE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, x_pos_px, -32);
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, y_pos_px, -32);
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));

        unsigned current_sparkle_config = PRIZE_SPARKLE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, x_pos_px, -32);
        xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, y_pos_px, -32);
        xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
    }

    // Re-copy Level 0 map into MAZE_MAP_DATA in XRAM
    uint16_t map0_base = ALL_MAZE_MAPS_DATA;
    uint16_t size = MAZE_MAP_WIDTH * MAZE_MAP_HEIGHT;
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, xram_data_ptr, MAZE_MAP_DATA);
    
    RIA.addr0 = MAZE_MAP_DATA;
    RIA.step0 = 1;
    RIA.addr1 = map0_base;
    RIA.step1 = 1;
    for (uint16_t i = 0; i < size; i++) {
        RIA.rw0 = RIA.rw1;
    }

    init_side_pellet_counters();
}

static uint16_t left_side_pellets_remaining = 0;
static uint16_t right_side_pellets_remaining = 0;

static uint16_t count_level_side_pellets(uint8_t level, bool is_right_side) {
    uint16_t src_map_base = ALL_MAZE_MAPS_DATA + ((uint16_t)level * (MAZE_MAP_WIDTH * MAZE_MAP_HEIGHT));
    uint16_t start_x = is_right_side ? 28 : 0;
    uint16_t end_x   = is_right_side ? 46 : 18;
    uint16_t count = 0;

    for (uint16_t ty = 4; ty <= 26; ty++) {
        for (uint16_t tx = start_x; tx <= end_x; tx++) {
            uint16_t offset = ty * MAZE_MAP_WIDTH + tx;
            RIA.addr0 = src_map_base + offset;
            RIA.step0 = 1;
            uint8_t tile_val = RIA.rw0;

            if (tile_val == 116 || tile_val == 117) {
                count++;
            }
        }
    }
    return count;
}

static void copy_single_column_with_offset(uint8_t level, uint16_t tx, uint8_t offset_val) {
    uint16_t src_map_base = ALL_MAZE_MAPS_DATA + ((uint16_t)level * (MAZE_MAP_WIDTH * MAZE_MAP_HEIGHT));

    for (uint16_t ty = 4; ty <= 26; ty++) {
        uint16_t map_offset = ty * MAZE_MAP_WIDTH + tx;

        RIA.addr0 = src_map_base + map_offset;
        RIA.step0 = 1;
        uint8_t tile_val = RIA.rw0;

        // Apply wave offset only to non-blank tiles with indices 1 <= tile_val <= 114
        uint8_t final_tile = tile_val;
        if (offset_val > 0 && tile_val > 0 && tile_val <= 114) {
            final_tile += offset_val;
        }

        RIA.addr0 = MAZE_MAP_DATA + map_offset;
        RIA.step0 = 1;
        RIA.rw0 = final_tile;
    }
}

static void trigger_maze_transition(uint8_t target_level, bool is_right_side) {
    uint8_t idx = is_right_side ? 1 : 0;
    s_transitions[idx].active = true;
    s_transitions[idx].munchers_active = true;
    s_transitions[idx].is_right_side = is_right_side;
    s_transitions[idx].target_level = target_level;
    s_transitions[idx].current_col_step = 0;
    s_transitions[idx].frame_timer = 0;
    s_transitions[idx].anim_frame_idx = 0;
    s_transitions[idx].elapsed_frame = 0;

    // Immediately count and initialize remaining pellets for the newly loaded target maze level
    if (is_right_side) {
        right_side_pellets_remaining = count_level_side_pellets(target_level, true);
    } else {
        left_side_pellets_remaining = count_level_side_pellets(target_level, false);
    }
}

void update_maze_munchers_animation(void) {
    if (is_eat_pause_active()) {
        return;
    }

    bool any_active = s_transitions[0].active || s_transitions[1].active;

    if (!any_active) {
        // Ensure munchers are parked off-screen when no transitions are active
        static bool s_munchers_were_active = false;
        if (s_munchers_were_active) {
            s_munchers_were_active = false;
            for (int i = 0; i < NMAZE_MUNCHERS; i++) {
                unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, -32);
                xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
            }
        }
        return;
    }

    static bool s_munchers_were_active = false;
    s_munchers_were_active = true;

    // Process both left side (0) and right side (1) transitions independently
    for (uint8_t s = 0; s < 2; s++) {
        if (!s_transitions[s].active) continue;

        // --- 1. MUNCHER SPRITES (Advance 1 tile column every 2 frames) ---
        if (s_transitions[s].munchers_active) {
            int8_t muncher_step = s_transitions[s].current_col_step;

            if (muncher_step >= 0 && muncher_step < SIDE_MAP_COLS) {
                uint16_t current_muncher_tx = s_transitions[s].is_right_side ? (28 + muncher_step) : (18 - muncher_step);
                int16_t world_x_px = (int16_t)(current_muncher_tx * MAZE_TILES_SIZE_PX);
                int16_t screen_x_px = world_x_px + maze_dx;
                int16_t sub_offset_x = (s_transitions[s].frame_timer == 1) ? (s_transitions[s].is_right_side ? 4 : -4) : 0;
                int16_t muncher_x = screen_x_px + sub_offset_x;

                uint8_t base_sprite_frame = s_transitions[s].is_right_side ? 90 : 98;
                uint8_t muncher_sprite_frame = base_sprite_frame + (s_transitions[s].anim_frame_idx % 8);

                for (int i = 0; i < NMAZE_MUNCHERS; i++) {
                    int16_t muncher_y = 28 + (i * 16);
                    unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, muncher_x);
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, muncher_y);
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (muncher_sprite_frame * SPRITE_FRAME_SIZE)));
                }
            } else {
                // Muncher sweep finished: park sprites off-screen
                for (int i = 0; i < NMAZE_MUNCHERS; i++) {
                    unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, -32);
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, -32);
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
                }
            }

            s_transitions[s].anim_frame_idx++;
            s_transitions[s].frame_timer++;
            if (s_transitions[s].frame_timer >= 2) {
                s_transitions[s].frame_timer = 0;
                s_transitions[s].current_col_step++;
                if (s_transitions[s].current_col_step >= SIDE_MAP_COLS) {
                    s_transitions[s].munchers_active = false;
                }
            }
        }

        // --- 2. DETERMINISTIC WAVE TRANSITION FOR THIS SIDE ---
        uint16_t cur_frame = s_transitions[s].elapsed_frame;
        s_transitions[s].elapsed_frame++;

        bool side_has_active_columns = false;

        for (uint8_t col = 0; col < SIDE_MAP_COLS; col++) {
            // Column reveal lags 2 columns behind munchers (revealed at frame 2 * col)
            uint16_t reveal_frame = (uint16_t)col * 2;

            if (cur_frame >= reveal_frame) {
                uint16_t elapsed = cur_frame - reveal_frame;

                // Offset starts at 7 and decrements by 1 every 16 frames (steps 0..7)
                uint8_t step = (uint8_t)(elapsed / 16);
                uint8_t offset_val = (step >= 7) ? 0 : (7 - step);

                if (offset_val > 0) {
                    side_has_active_columns = true;
                }

                // Update column tiles only on the exact frame the offset changes
                if (elapsed == 0 || (elapsed % 16 == 0 && step <= 7)) {
                    uint16_t tx = s_transitions[s].is_right_side ? (28 + col) : (18 - col);
                    copy_single_column_with_offset(s_transitions[s].target_level, tx, offset_val);
                    check_and_reset_stuck_ghosts();
                }
            } else {
                side_has_active_columns = true;
            }
        }

        // End transition for this side when its munchers AND all 19 column waves complete
        if (!s_transitions[s].munchers_active && !side_has_active_columns) {
            s_transitions[s].active = false;
        }
    }
}

void init_side_pellet_counters(void) {
    left_side_pellets_remaining = count_level_side_pellets(left_side_level, false);
    right_side_pellets_remaining = count_level_side_pellets(right_side_level, true);
}

void on_pellet_eaten(uint16_t tile_x) {
    if (tile_x <= 18) {
        if (left_side_pellets_remaining > 0) {
            left_side_pellets_remaining--;
        }
    } else if (tile_x >= 28 && tile_x <= 46) {
        if (right_side_pellets_remaining > 0) {
            right_side_pellets_remaining--;
        }
    }
}

void update_side_pellets_status(void) {
    // 1. Check left side clear condition (O(1) counter check)
    if (!left_prize_active && left_side_pellets_remaining == 0) {
        left_prize_active = true;
        left_prize_sprite = get_side_prize_sprite_index(left_prize_count);
        left_prize_count++;
        prizes[0].sparkle_timer = 0;
    }

    // 2. Check right side clear condition (O(1) counter check)
    if (!right_prize_active && right_side_pellets_remaining == 0) {
        right_prize_active = true;
        right_prize_sprite = get_side_prize_sprite_index(right_prize_count);
        right_prize_count++;
        prizes[1].sparkle_timer = 0;
    }
}

static const uint16_t PRIZE_POINTS_TABLE[22] = {
    1000, // Index 49: Cherry
    1200, // Index 50: Strawberry
    1400, // Index 51: Orange
    1600, // Index 52: Apple
    1800, // Index 53: Melon
    2000, // Index 54: Banana
    2200, // Index 55: Peach
    2400, // Index 56: Galaxian Boss
    2600, // Index 57: Bell
    4000, // Index 58: Key
    4200, // Index 59: Coffee
    4400, // Index 60: Cake
    4600, // Index 61: Galaga
    4800, // Index 62: Gaplus Drone
    5000, // Index 63: Hamburger
    5200, // Index 64: Fried Egg
    5400, // Index 65: Candy
    5600, // Index 66: Four-Leaf Clover
    5800, // Index 67: Diamond
    6000, // Index 68: Heart
    6200, // Index 69: Samurai Helmet
    7650, // Index 70: Crown
};

static uint16_t get_prize_points(uint8_t sprite_frame) {
    if (sprite_frame < 49 || sprite_frame > 70) return 0;
    return PRIZE_POINTS_TABLE[sprite_frame - 49];
}

void check_and_eat_prize(int16_t drawn_world_x, int16_t drawn_world_y) {
    // Wrap Pac-Man drawn world coordinates
    int16_t pm_x = drawn_world_x;
    int16_t pm_y = drawn_world_y;
    while (pm_x < 0) pm_x += WORLD_WIDTH;
    while (pm_x >= WORLD_WIDTH) pm_x -= WORLD_WIDTH;
    while (pm_y < 0) pm_y += WORLD_HEIGHT;
    while (pm_y >= WORLD_HEIGHT) pm_y -= WORLD_HEIGHT;

    // Pac-Man 16x16 sprite visual center point
    int16_t pm_center_x = pm_x + 8;
    int16_t pm_center_y = pm_y + 8;

    // Prize 0 (Left side clear bonus item)
    if (left_prize_active) {
        int16_t pr_x = prizes[0].world_px;
        int16_t pr_y = prizes[0].world_py;
        // 16x16 prize sprite bounding box check: center point falls within prize bounds
        if (pm_center_x >= (pr_x + 2) && pm_center_x <= (pr_x + 14) &&
            pm_center_y >= (pr_y + 2) && pm_center_y <= (pr_y + 14)) {

            left_prize_active = false;

            // Award points for capturing left bonus item
            uint16_t pts = get_prize_points(left_prize_sprite);
            add_player_score(pts);
            trigger_prize_score_animation(0, pts);

            prizes[0].frame = 48; // Blank sprite
            prizes[0].sparkle_frame = 48;

            unsigned prize_config0 = PRIZE_CONFIG + (0 * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(prize_config0, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[0].frame * SPRITE_FRAME_SIZE)));

            unsigned prize_sparkle_config0 = PRIZE_SPARKLE_CONFIG + (0 * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(prize_sparkle_config0, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));

            // Advance left level: 0..10, then loop back to 1
            left_side_level++;
            if (left_side_level > 10) left_side_level = 1;

            // Trigger column-by-column animated maze munchers transition
            trigger_maze_transition(left_side_level, false);
        }
    }

    // Prize 1 (Right side clear bonus item)
    if (right_prize_active) {
        int16_t pr_x = prizes[1].world_px;
        int16_t pr_y = prizes[1].world_py;
        // 16x16 prize sprite bounding box check: center point falls within prize bounds
        if (pm_center_x >= (pr_x + 2) && pm_center_x <= (pr_x + 14) &&
            pm_center_y >= (pr_y + 2) && pm_center_y <= (pr_y + 14)) {

            right_prize_active = false;

            // Award points for capturing right bonus item
            uint16_t pts = get_prize_points(right_prize_sprite);
            add_player_score(pts);
            trigger_prize_score_animation(1, pts);

            prizes[1].frame = 48; // Blank sprite
            prizes[1].sparkle_frame = 48;

            unsigned prize_config1 = PRIZE_CONFIG + (1 * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(prize_config1, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[1].frame * SPRITE_FRAME_SIZE)));

            unsigned prize_sparkle_config1 = PRIZE_SPARKLE_CONFIG + (1 * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(prize_sparkle_config1, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));

            // Advance right level: 0..10, then loop back to 1
            right_side_level++;
            if (right_side_level > 10) right_side_level = 1;

            // Trigger column-by-column animated maze munchers transition
            trigger_maze_transition(right_side_level, true);
        }
    }
}

void prize_update_motion(void) {

    for (int i = 0; i < NPRIZES; i++) {
        // Wrap prize world coordinates within WORLD_WIDTH
        if (prizes[i].world_px < 0) {
            prizes[i].world_px += WORLD_WIDTH;
        } else if (prizes[i].world_px >= WORLD_WIDTH) {
            prizes[i].world_px -= WORLD_WIDTH;
        }

        // Calculate screen position from world position and map scroll
        prizes[i].x_pos_px = prizes[i].world_px + maze_dx;
        prizes[i].y_pos_px = prizes[i].world_py;

        bool is_active = (i == 0) ? left_prize_active : right_prize_active;
        uint8_t current_prize_sprite = (i == 0) ? left_prize_sprite : right_prize_sprite;

        static bool s_prize_was_active[NPRIZES] = {false, false};

        if (is_active) {
            s_prize_was_active[i] = true;

            int16_t dx = 0, dy = 0;
            uint8_t s_frame = 81; // Sparkle frame index
            uint8_t t = prizes[i].sparkle_timer;

            // Sparkle sequence step evaluation across 27 frames
            // a, 2p, S b, 3p, c, 7p, d, 3p, e, 3p, f, 3p, g, 3p, h, 3p
            if (t == 0) {
                // a = (-8, 2)
                dx = -8; dy = 2;
                prizes[i].frame = 48; // Blank prize sprite before 'S'
            } else if (t == 1 || t == 2) {
                // 2p wait
                dx = -8; dy = 2;
                prizes[i].frame = 48;
            } else if (t == 3) {
                // S, b = (-8, 4)
                prizes[i].frame = current_prize_sprite; // Draw prize sprite
                dx = -8; dy = 4;
            } else if (t >= 4 && t <= 6) {
                // 3p wait
                dx = -8; dy = 4;
            } else if (t == 7) {
                // c = (3, -2)
                dx = 3; dy = -2;
            } else if (t >= 8 && t <= 14) {
                // 7p wait
                dx = 3; dy = -2;
            } else if (t == 15) {
                // d = (-5, -2)
                dx = -5; dy = -2;
            } else if (t >= 16 && t <= 18) {
                // 3p wait
                dx = -5; dy = -2;
            } else if (t == 19) {
                // e = (-5, -6)
                dx = -5; dy = -6;
            } else if (t >= 20 && t <= 22) {
                // 3p wait
                dx = -5; dy = -6;
            } else if (t == 23) {
                // f = (+5, -6)
                dx = 5; dy = -6;
            } else if (t >= 24 && t <= 26) {
                // 3p wait
                dx = 5; dy = -6;
            } else if (t == 27) {
                // g = (+5, -10)
                dx = 5; dy = -10;
            } else if (t >= 28 && t <= 30) {
                // 3p wait
                dx = 5; dy = -10;
            } else if (t == 31) {
                // h = (-2, -10)
                dx = -2; dy = -10;
            } else if (t >= 32 && t <= 34) {
                // 3p wait
                dx = -2; dy = -10;
            }

            if (t <= 34) {
                prizes[i].sparkle_frame = s_frame;
                prizes[i].x_sparkle_px = prizes[i].x_pos_px + dx;
                prizes[i].y_sparkle_px = prizes[i].y_pos_px + dy;
                prizes[i].sparkle_timer++;
            } else {
                // Sequence completed: hide sparkle sprite and hold prize visible
                prizes[i].frame = current_prize_sprite;
                prizes[i].sparkle_frame = 48; // Blank sparkle
                prizes[i].x_sparkle_px = -32;
                prizes[i].y_sparkle_px = -32;
            }

            // Update prize sprite in XRAM while active
            unsigned current_prize_config = PRIZE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
            if (prizes[i].frame == 48) {
                // When prize frame is blank (during 'a' and '2p'), park off-screen (-32, -32)
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, y_pos_px, -32);
            } else {
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, x_pos_px, prizes[i].x_pos_px);
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, y_pos_px, prizes[i].y_pos_px);
            }
            xram0_struct_set(current_prize_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[i].frame * SPRITE_FRAME_SIZE)));

            // Update sparkle sprite in XRAM while active
            unsigned current_sparkle_config = PRIZE_SPARKLE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
            if (prizes[i].sparkle_frame == 48) {
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, y_pos_px, -32);
            } else {
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, x_pos_px, prizes[i].x_sparkle_px);
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, y_pos_px, prizes[i].y_sparkle_px);
            }
            xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[i].sparkle_frame * SPRITE_FRAME_SIZE)));

        } else {
            // Prize is inactive: if it transitioned to inactive this frame, park off-screen and stop updating
            if (s_prize_was_active[i]) {
                s_prize_was_active[i] = false;
                prizes[i].frame = 48;
                prizes[i].sparkle_frame = 48;

                unsigned current_prize_config = PRIZE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, y_pos_px, -32);
                xram0_struct_set(current_prize_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));

                unsigned current_sparkle_config = PRIZE_SPARKLE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, x_pos_px, -32);
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, y_pos_px, -32);
                xram0_struct_set(current_sparkle_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
            }
        }
    }
}