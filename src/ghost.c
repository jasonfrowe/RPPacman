#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"

void ghost_update_motion(void) {

    for (int i = 0; i < NGHOSTS; i++) {
        // Update ghost position based on MAZE
        ghosts[i].x_pos_px -= maze_d1; // Adjust ghost position based on maze movement


        // Update the ghost's position in XRAM
        unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, ghosts[i].x_pos_px);
        // xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, ghosts[i].y_pos_px);
    }


}