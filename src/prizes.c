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

typedef struct {
    bool active;
    bool is_right_side;
    uint8_t target_level;
    int8_t current_col_step; // 0..18
    uint8_t frame_timer;     // counts 0, 1 -> update column every 2 frames
    uint8_t anim_frame_idx;  // muncher sprite animation index
} maze_transition_t;

static maze_transition_t s_transition = { false, false, 0, 0, 0, 0 };

static void copy_single_column(uint8_t level, uint16_t tx) {
    uint16_t src_map_base = ALL_MAZE_MAPS_DATA + ((uint16_t)level * (MAZE_MAP_WIDTH * MAZE_MAP_HEIGHT));

    for (uint16_t ty = 4; ty <= 26; ty++) {
        uint16_t offset = ty * MAZE_MAP_WIDTH + tx;

        RIA.addr0 = src_map_base + offset;
        RIA.step0 = 1;
        uint8_t tile_val = RIA.rw0;

        RIA.addr0 = MAZE_MAP_DATA + offset;
        RIA.step0 = 1;
        RIA.rw0 = tile_val;
    }
}

static void trigger_maze_transition(uint8_t target_level, bool is_right_side) {
    s_transition.active = true;
    s_transition.is_right_side = is_right_side;
    s_transition.target_level = target_level;
    s_transition.current_col_step = 0;
    s_transition.frame_timer = 0;
    s_transition.anim_frame_idx = 0;
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
    s_munchers_were_active = true;

    // Determine current tile column being replaced
    uint16_t current_tx;
    if (s_transition.is_right_side) {
        // Right side: start at tile 28 moving right to 46 (steps 0..18)
        current_tx = 28 + s_transition.current_col_step;
    } else {
        // Left side: start at tile 18 moving left to 0 (steps 0..18)
        current_tx = 18 - s_transition.current_col_step;
    }

    // Every 2 frames, copy new maze column from ALL_MAZE_MAPS_DATA
    if (s_transition.frame_timer == 0) {
        copy_single_column(s_transition.target_level, current_tx);
    }

    // Calculate muncher X position in screen space (world tile X * 8 + maze_dx)
    int16_t world_x_px = (int16_t)(current_tx * MAZE_TILES_SIZE_PX);
    int16_t screen_x_px = world_x_px + maze_dx;

    // Movement: 4px per frame offset (sub-tile alignment)
    int16_t sub_offset_x = (s_transition.frame_timer == 1) ? (s_transition.is_right_side ? 4 : -4) : 0;
    int16_t muncher_x = screen_x_px + sub_offset_x;

    // Frame selection: Left replacement (98..105), Right replacement (90..97)
    uint8_t base_sprite_frame = s_transition.is_right_side ? 90 : 98;
    uint8_t muncher_sprite_frame = base_sprite_frame + (s_transition.anim_frame_idx % 8);

    // Update 12 vertically stacked muncher sprites starting at y = 28px
    for (int i = 0; i < NMAZE_MUNCHERS; i++) {
        int16_t muncher_y = 28 + (i * 16);

        unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, muncher_x);
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, muncher_y);
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (muncher_sprite_frame * SPRITE_FRAME_SIZE)));
    }

    // Advance animation frame counter
    s_transition.anim_frame_idx++;

    // Advance frame step timer (2 frames per column)
    s_transition.frame_timer++;
    if (s_transition.frame_timer >= 2) {
        s_transition.frame_timer = 0;
        s_transition.current_col_step++;

        // Check if all 19 columns (0..18) have been replaced
        if (s_transition.current_col_step > 18) {
            s_transition.active = false;
        }
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