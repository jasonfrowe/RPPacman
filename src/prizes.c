#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "prizes.h"

uint8_t left_side_level = 0;
uint8_t right_side_level = 0;

static bool left_prize_active = false;
static bool right_prize_active = false;

static uint8_t get_prize_sprite_index(uint8_t level) {
    uint8_t index = 49 + level;
    if (index > 70) index = 70;
    return index;
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

    uint8_t wave_offsets[SIDE_MAP_COLS];     // current tile index offset for each column (0..7)
    uint8_t wave_hold_timers[SIDE_MAP_COLS]; // frame countdown before decrementing offset (16 frames)
    bool column_revealed[SIDE_MAP_COLS];     // whether column tile values have been copied
} maze_transition_t;

static maze_transition_t s_transition;

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
    s_transition.active = true;
    s_transition.munchers_active = true;
    s_transition.is_right_side = is_right_side;
    s_transition.target_level = target_level;
    s_transition.current_col_step = 0;
    s_transition.frame_timer = 0;
    s_transition.anim_frame_idx = 0;

    for (uint8_t i = 0; i < SIDE_MAP_COLS; i++) {
        s_transition.wave_offsets[i] = 0;
        s_transition.wave_hold_timers[i] = 0;
        s_transition.column_revealed[i] = false;
    }
}

void update_maze_munchers_animation(void) {
    if (!s_transition.active) {
        // Ensure munchers are parked off-screen when inactive
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

    // --- 1. MUNCHER SPRITES & COLUMN REVEAL (Lags 2 columns behind munchers) ---
    if (s_transition.munchers_active) {
        s_munchers_were_active = true;

        // Current column being swept by munchers (0..18)
        int8_t muncher_step = s_transition.current_col_step;

        // Tile replacement lags 2 columns behind munchers
        int8_t wave_reveal_step = muncher_step - 2;
        if (wave_reveal_step >= 0 && wave_reveal_step < SIDE_MAP_COLS) {
            if (!s_transition.column_revealed[wave_reveal_step]) {
                s_transition.column_revealed[wave_reveal_step] = true;
                s_transition.wave_offsets[wave_reveal_step] = 7; // Initial +7 index offset
                s_transition.wave_hold_timers[wave_reveal_step] = 16; // 16 frames per decrement

                uint16_t tx = s_transition.is_right_side ? (28 + wave_reveal_step) : (18 - wave_reveal_step);
                copy_single_column_with_offset(s_transition.target_level, tx, 7);
            }
        }

        // Calculate muncher position and draw sprites
        uint16_t current_muncher_tx = s_transition.is_right_side ? (28 + muncher_step) : (18 - muncher_step);
        int16_t world_x_px = (int16_t)(current_muncher_tx * MAZE_TILES_SIZE_PX);
        int16_t screen_x_px = world_x_px + maze_dx;
        int16_t sub_offset_x = (s_transition.frame_timer == 1) ? (s_transition.is_right_side ? 4 : -4) : 0;
        int16_t muncher_x = screen_x_px + sub_offset_x;

        uint8_t base_sprite_frame = s_transition.is_right_side ? 90 : 98;
        uint8_t muncher_sprite_frame = base_sprite_frame + (s_transition.anim_frame_idx % 8);

        for (int i = 0; i < NMAZE_MUNCHERS; i++) {
            int16_t muncher_y = 28 + (i * 16);
            unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
            xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, muncher_x);
            xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, muncher_y);
            xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (muncher_sprite_frame * SPRITE_FRAME_SIZE)));
        }

        s_transition.anim_frame_idx++;
        s_transition.frame_timer++;
        if (s_transition.frame_timer >= 2) {
            s_transition.frame_timer = 0;
            s_transition.current_col_step++;

            if (s_transition.current_col_step > (SIDE_MAP_COLS + 2)) {
                s_transition.munchers_active = false;

                // Park muncher sprites off-screen
                for (int i = 0; i < NMAZE_MUNCHERS; i++) {
                    unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, -32);
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, -32);
                    xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
                }
            }
        }
    }

    // --- 2. WAVE OFFSET DECAY ANIMATION (Decrements by 1 every 16 frames down to 0) ---
    bool any_wave_active = false;
    for (uint8_t step = 0; step < SIDE_MAP_COLS; step++) {
        if (!s_transition.column_revealed[step]) continue;

        if (s_transition.wave_offsets[step] > 0) {
            any_wave_active = true;

            if (s_transition.wave_hold_timers[step] > 0) {
                s_transition.wave_hold_timers[step]--;
            }

            if (s_transition.wave_hold_timers[step] == 0) {
                s_transition.wave_offsets[step]--;
                s_transition.wave_hold_timers[step] = 16; // Reset 16 frame timer for next decrement

                uint16_t tx = s_transition.is_right_side ? (28 + step) : (18 - step);
                copy_single_column_with_offset(s_transition.target_level, tx, s_transition.wave_offsets[step]);
            }
        }
    }

    // End full transition when munchers and all column waves finish
    if (!s_transition.munchers_active && !any_wave_active) {
        s_transition.active = false;
    }
}

static bool is_side_cleared(bool is_right_side) {
    uint16_t start_x = is_right_side ? 28 : 0;
    uint16_t end_x   = is_right_side ? 46 : 18;

    for (uint16_t ty = 4; ty <= 26; ty++) {
        for (uint16_t tx = start_x; tx <= end_x; tx++) {
            uint16_t offset = ty * MAZE_MAP_WIDTH + tx;
            RIA.addr0 = MAZE_MAP_DATA + offset;
            RIA.step0 = 1;
            uint8_t tile_val = RIA.rw0;

            if (tile_val == 116 || tile_val == 117) {
                return false; // Found a remaining pellet or super pellet
            }
        }
    }
    return true; // All pellets cleared on this side
}

void update_side_pellets_status(void) {
    // 1. Check left side clear condition
    if (!left_prize_active && is_side_cleared(false)) {
        left_prize_active = true;
        prizes[0].sparkle_timer = 0;
    }

    // 2. Check right side clear condition
    if (!right_prize_active && is_side_cleared(true)) {
        right_prize_active = true;
        prizes[1].sparkle_timer = 0;
    }
}

void check_and_eat_prize(uint16_t tile_x, uint16_t tile_y) {
    // Prize 0 (spawned when left side is cleared) is centered at tile (28, 15)
    if (left_prize_active && (tile_x == 28 || tile_x == 29) && (tile_y == 15 || tile_y == 16)) {
        left_prize_active = false;
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

    // Prize 1 (spawned when right side is cleared) is centered at tile (18, 15)
    if (right_prize_active && (tile_x == 17 || tile_x == 18) && (tile_y == 15 || tile_y == 16)) {
        right_prize_active = false;
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
        uint8_t current_level = (i == 0) ? left_side_level : right_side_level;

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
                prizes[i].frame = get_prize_sprite_index(current_level); // Draw prize sprite
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
                prizes[i].frame = get_prize_sprite_index(current_level);
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