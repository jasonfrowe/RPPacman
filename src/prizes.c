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

static void copy_maze_half(uint8_t level, bool is_right_side) {
    // Each map in ALL_MAZE_MAPS_DATA is 47 * 30 = 1410 bytes
    uint16_t src_map_base = ALL_MAZE_MAPS_DATA + ((uint16_t)level * (MAZE_MAP_WIDTH * MAZE_MAP_HEIGHT));

    uint16_t start_x = is_right_side ? 28 : 0;
    uint16_t end_x   = is_right_side ? 46 : 18;

    for (uint16_t ty = 4; ty <= 26; ty++) {
        for (uint16_t tx = start_x; tx <= end_x; tx++) {
            uint16_t offset = ty * MAZE_MAP_WIDTH + tx;

            // Read tile byte from ALL_MAZE_MAPS_DATA map level
            RIA.addr0 = src_map_base + offset;
            RIA.step0 = 1;
            uint8_t tile_val = RIA.rw0;

            // Write tile byte to active MAZE_MAP_DATA
            RIA.addr0 = MAZE_MAP_DATA + offset;
            RIA.step0 = 1;
            RIA.rw0 = tile_val;
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
        prizes[0].frame = get_prize_sprite_index(left_side_level);

        // Update prize 0 sprite pointer in XRAM
        unsigned prize_config0 = PRIZE_CONFIG + (0 * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(prize_config0, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[0].frame * SPRITE_FRAME_SIZE)));
    }

    // 2. Check right side clear condition
    if (!right_prize_active && is_side_cleared(true)) {
        right_prize_active = true;
        prizes[1].frame = get_prize_sprite_index(right_side_level);

        // Update prize 1 sprite pointer in XRAM
        unsigned prize_config1 = PRIZE_CONFIG + (1 * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(prize_config1, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[1].frame * SPRITE_FRAME_SIZE)));
    }
}

void check_and_eat_prize(uint16_t tile_x, uint16_t tile_y) {
    // Prize 0 (spawned when left side is cleared) is centered at tile (28, 15)
    if (left_prize_active && (tile_x == 28 || tile_x == 29) && (tile_y == 15 || tile_y == 16)) {
        left_prize_active = false;
        prizes[0].frame = 48; // Blank sprite

        unsigned prize_config0 = PRIZE_CONFIG + (0 * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(prize_config0, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[0].frame * SPRITE_FRAME_SIZE)));

        // Advance left level: 0..10, then loop back to 1
        left_side_level++;
        if (left_side_level > 10) left_side_level = 1;

        copy_maze_half(left_side_level, false);
    }

    // Prize 1 (spawned when right side is cleared) is centered at tile (18, 15)
    if (right_prize_active && (tile_x == 17 || tile_x == 18) && (tile_y == 15 || tile_y == 16)) {
        right_prize_active = false;
        prizes[1].frame = 48; // Blank sprite

        unsigned prize_config1 = PRIZE_CONFIG + (1 * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(prize_config1, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[1].frame * SPRITE_FRAME_SIZE)));

        // Advance right level: 0..10, then loop back to 1
        right_side_level++;
        if (right_side_level > 10) right_side_level = 1;

        copy_maze_half(right_side_level, true);
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

        // Update the prize position in XRAM
        unsigned current_prize_config = PRIZE_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, x_pos_px, prizes[i].x_pos_px);
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, y_pos_px, prizes[i].y_pos_px);
    }
}