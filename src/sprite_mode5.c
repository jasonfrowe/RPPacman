#include <rp6502.h>
#include <stdint.h>
#include "constants.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"

// Store the player config address for updates
unsigned PLAYER_CONFIG;
unsigned GHOST_CONFIG;
unsigned PRIZE_CONFIG;
unsigned PRIZE_SPARKLE_CONFIG;


ghost_struct ghosts[NGHOSTS];
player_struct player;

prize_struct prizes[NPRIZES] = {
    {0, 0, 28 * MAZE_TILES_SIZE_PX - 5, 15 * MAZE_TILES_SIZE_PX - 3, 48, -32}, // Left clear prize -> tile (28, 15)
    {0, 0, 18 * MAZE_TILES_SIZE_PX - 5, 15 * MAZE_TILES_SIZE_PX - 3, 48, -32}  // Right clear prize -> tile (18, 15)
};

void sprite_mode5_init(void) {

    // Start at tile coordinates (23, 21) => world (184, 168)
    player.world_px = 23 * MAZE_TILES_SIZE_PX; // 184
    player.world_py = 21 * MAZE_TILES_SIZE_PX; // 168
    player.x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    player.y_pos_px = player.world_py - 3; // Visual offset to restore 165px plotted position
    player.score = 0;
    player.pellets_eaten = 0;

    player.frame = 5; // Start with the "facing left / closed" frame for the player sprite

    PRIZE_CONFIG = MAZE_CONFIG + sizeof(vga_mode2_config_t); // After maze config 
    
    for (int i = 0; i < NPRIZES; i++) {
        prizes[i].x_pos_px = prizes[i].world_px + maze_dx;
        prizes[i].y_pos_px = prizes[i].world_py;

        // Calculate the base address for the current prize once
        uint16_t current_prize_config = PRIZE_CONFIG + (i * sizeof(vga_mode5_sprite_t));

        // Set X and Y positions in XRAM
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, x_pos_px, (prizes[i].x_pos_px));
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, y_pos_px, (prizes[i].y_pos_px));
        
        // Set the sprite pointer to the first frame of the prize sprite
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (prizes[i].frame * SPRITE_FRAME_SIZE)));
        xram0_struct_set(current_prize_config, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);
    }

    PRIZE_SPARKLE_CONFIG = PRIZE_CONFIG + NPRIZES * sizeof(vga_mode5_sprite_t); // After prize config

    for (int i = 0; i < NPRIZES; i++) {
        // Calculate the base address for the current prize sparkle once
        uint16_t current_prize_sparkle_config = PRIZE_SPARKLE_CONFIG + (i * sizeof(vga_mode5_sprite_t));

        // Set X and Y positions in XRAM
        xram0_struct_set(current_prize_sparkle_config, vga_mode5_sprite_t, x_pos_px, (prizes[i].x_sparkle_px));
        xram0_struct_set(current_prize_sparkle_config, vga_mode5_sprite_t, y_pos_px, (prizes[i].y_sparkle_px));
        
        // Set the sprite pointer to the first frame of the sparkle sprite
        xram0_struct_set(current_prize_sparkle_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE))); // Sparkle frame index 48
        xram0_struct_set(current_prize_sparkle_config, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);
    }
    
    GHOST_CONFIG = PRIZE_SPARKLE_CONFIG + NPRIZES * sizeof(vga_mode5_sprite_t); // After prize sparkle config

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

    PLAYER_CONFIG = GHOST_CONFIG + NGHOSTS * sizeof(vga_mode5_sprite_t); // After ghost config

    int16_t visual_x = player.x_pos_px - 3;
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, visual_x);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);


    RIA.addr0 = PLAYER_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = player_palette[i] & 0xFF;
        RIA.rw0 = player_palette[i] >> 8;
    }

    // Mode 5 args: MODE, OPTIONS, CONFIG, LENGTH, PLANE, BEGIN, END
    if (xreg_vga_mode(5, 0x0A, PRIZE_CONFIG, 1 + NGHOSTS + NPRIZES + NPRIZES, 1, 0, 0) < 0) {
        return;
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
