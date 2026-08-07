#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"

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