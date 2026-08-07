#include <rp6502.h>
#include <stdint.h>
#include "constants.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"

// Store the player config address for updates
unsigned PLAYER_CONFIG;
unsigned GHOST_CONFIG;


ghost_struct ghosts[NGHOSTS];
player_struct player;

void sprite_mode5_init(void) {

    // Start at tile coordinates (23, 21) => world (184, 168)
    player.world_px = 23 * MAZE_TILES_SIZE_PX; // 184
    player.world_py = 21 * MAZE_TILES_SIZE_PX; // 168
    player.x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    player.y_pos_px = player.world_py - 3; // Visual offset to restore 165px plotted position

    player.frame = 5; // Start with the "facing left / closed" frame for the player sprite

    PLAYER_CONFIG = MAZE_CONFIG + sizeof(vga_mode2_config_t); // After maze config

    int16_t visual_x = player.x_pos_px - 3;
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, visual_x);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);

    GHOST_CONFIG = PLAYER_CONFIG + sizeof(vga_mode5_sprite_t); // After player config

    for (int i = 0; i < NGHOSTS; i++) {
        // Calculate the base address for the current ghost once
        uint16_t current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));

        // Set X and Y positions from the struct
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, ghosts[i].x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, ghosts[i].y_pos_px);
        
        // Calculate the sprite pointer using the frame from the struct (with parentheses for safety)
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (ghosts[i].frame * SPRITE_FRAME_SIZE)));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);
    }

    // Mode 5 args: MODE, OPTIONS, CONFIG, LENGTH, PLANE, BEGIN, END
    if (xreg_vga_mode(5, 0x0A, PLAYER_CONFIG, 1 + NGHOSTS, 1, 0, 0) < 0) {
        return;
    }

    RIA.addr0 = PLAYER_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = player_palette[i] & 0xFF;
        RIA.rw0 = player_palette[i] >> 8;
    }

}

// Call this before calling sprite_mode5_init_ghosts()
void init_ghost_data(void) {
    // Ghost 0 (Blinky / Red) -Inside top
    ghosts[0].x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    ghosts[0].y_pos_px = (int16_t)((SCREEN_HEIGHT - SPRITE_SIZE_PX) * 2 / 3 - 40); // Start slightly above center for better composition
    ghosts[0].world_px = ghosts[0].x_pos_px - maze_dx;
    ghosts[0].world_py = ghosts[0].y_pos_px;
    ghosts[0].frame = 10;

    // Ghost 1 (Pinky / Pink) - Inside, bottom
    ghosts[1].x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    ghosts[1].y_pos_px = (int16_t)((SCREEN_HEIGHT - SPRITE_SIZE_PX) * 2 / 3 - 24);
    ghosts[1].world_px = ghosts[1].x_pos_px - maze_dx;
    ghosts[1].world_py = ghosts[1].y_pos_px;
    ghosts[1].frame = 16;

    // Ghost 2 (Inky / Cyan) - Inside, left
    ghosts[2].x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2 - 16);
    ghosts[2].y_pos_px = (int16_t)((SCREEN_HEIGHT - SPRITE_SIZE_PX) * 2 / 3 - 32);
    ghosts[2].world_px = ghosts[2].x_pos_px - maze_dx;
    ghosts[2].world_py = ghosts[2].y_pos_px;
    ghosts[2].frame = 26;

    // Ghost 3 (Clyde / Orange) - Inside, right
    ghosts[3].x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2 + 16);
    ghosts[3].y_pos_px = (int16_t)((SCREEN_HEIGHT - SPRITE_SIZE_PX) * 2 / 3 - 32);
    ghosts[3].world_px = ghosts[3].x_pos_px - maze_dx;
    ghosts[3].world_py = ghosts[3].y_pos_px;
    ghosts[3].frame = 34;
}
