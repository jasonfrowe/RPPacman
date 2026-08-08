#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "player.h"
#include "ghost.h"

// Ghost base sprite frames:
// Red (0):    Up 8..9,   Down 10..11, Left 12..13, Right 14..15  (Base = 8)
// Pink (1):   Up 16..17, Down 18..19, Left 20..21, Right 22..23  (Base = 16)
// Cyan (2):   Up 24..25, Down 26..27, Left 28..29, Right 30..31  (Base = 24)
// Orange (3): Up 32..33, Down 34..35, Left 36..37, Right 38..39  (Base = 32)
static const uint8_t GHOST_BASE_FRAMES[NGHOSTS] = { 8, 16, 24, 32 };

void ghost_update_motion(void) {
    static uint8_t anim_timer = 0;
    static uint8_t anim_cell = 0;

    // Toggle animation cell (0 or 1) every 8 frames (~7.5 Hz)
    anim_timer++;
    if (anim_timer >= 8) {
        anim_timer = 0;
        anim_cell ^= 1;
    }

    for (int i = 0; i < NGHOSTS; i++) {
        uint8_t base_frame = GHOST_BASE_FRAMES[i];
        uint8_t dir_offset = 0;

        switch (ghosts[i].dir) {
            case DIR_UP:    dir_offset = 0; break; // 0..1
            case DIR_DOWN:  dir_offset = 2; break; // 2..3
            case DIR_LEFT:  dir_offset = 4; break; // 4..5
            case DIR_RIGHT: dir_offset = 6; break; // 6..7
            default:        dir_offset = 2; break; // Default facing down
        }

        ghosts[i].frame = base_frame + dir_offset + anim_cell;

        // Update the ghost sprite XRAM configuration
        unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, ghosts[i].x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, ghosts[i].y_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (ghosts[i].frame * SPRITE_FRAME_SIZE)));
    }
}