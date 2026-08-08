#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "input.h"
#include "player.h"
#include "ghost.h"
#include "prizes.h"

static bool init_graphics(void)
{
    // 320×240 canvas
    int rc;
    rc = xreg_vga_canvas(1);
    if (rc < 0) {
        return false;
    }

    // Initialize tile mode 2 (8×8 tiles, 8-bit color index)
    tile_mode2_init();
    init_ghost_data();
    sprite_mode5_init();
    tile_mode2_text_map_init();
    init_side_pellet_counters();

    return true;
}

uint8_t vsync_last = 0;
uint8_t frame = 0;

int main(void)
{
    puts("Hello from RPPacMan!");

    input_actions_t actions;
    input_init();

    // Initialise graphics
    if (!init_graphics()) {
        return 1;
    }

    // Main loop
    while (true) {
        // 1. INPUT
        input_poll(&actions);

        // 2. Update Player
        player_update_motion(&actions);
        ghost_update_motion();
        prize_update_motion();
        update_maze_munchers_animation();

        // 3. Update animation palettes
        tile_mode2_palette_update(frame);

        // 4. SYNC (Wait for VSYNC)
        // Spin here until the hardware vsync register changes
        while (RIA.vsync == vsync_last) {
            // Do nothing, just wait
        }
        
        // Update our tracker and frame counter
        vsync_last = RIA.vsync;
        
        frame++;
        if (frame >= 60) frame = 0;
    }

    return 0;
}
