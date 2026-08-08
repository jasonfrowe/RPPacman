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
unsigned MAZE_MUNCHERS_CONFIG;


ghost_struct ghosts[NGHOSTS];
player_struct player;

prize_struct prizes[NPRIZES] = {
    {0, 0, 28 * MAZE_TILES_SIZE_PX - 5, 15 * MAZE_TILES_SIZE_PX - 3, 48, -32, -32, 0, 48}, // Left clear prize -> tile (28, 15)
    {0, 0, 18 * MAZE_TILES_SIZE_PX - 5, 15 * MAZE_TILES_SIZE_PX - 3, 48, -32, -32, 0, 48}  // Right clear prize -> tile (18, 15)
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

    MAZE_MUNCHERS_CONFIG = MAZE_CONFIG + sizeof(vga_mode2_config_t); // After maze config

    for (int i = 0; i < NMAZE_MUNCHERS; i++) {
        unsigned muncher_config = MAZE_MUNCHERS_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, x_pos_px, -32);  // Park off-screen (-32, -32)
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, y_pos_px, -32);
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE))); // Set to blank sprite frame (48)
        xram0_struct_set(muncher_config, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);
    }


    PRIZE_CONFIG = MAZE_MUNCHERS_CONFIG + NMAZE_MUNCHERS * sizeof(vga_mode5_sprite_t); // After maze transition config
    
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
    if (xreg_vga_mode(5, 0x0A, MAZE_MUNCHERS_CONFIG, 1 + NGHOSTS + NPRIZES + NPRIZES + NMAZE_MUNCHERS, 1, 0, 0) < 0) {
        return;
    }

}

// Call this before calling sprite_mode5_init()
void init_ghost_data(void) {
    // Red (Blinky): tile (23, 14) => world (184, 112) [Already at exit slot]
    ghosts[0].world_px = 23 * MAZE_TILES_SIZE_PX; // 184
    ghosts[0].world_py = 14 * MAZE_TILES_SIZE_PX; // 112
    ghosts[0].x_pos_px = ghosts[0].world_px + maze_dx;
    ghosts[0].y_pos_px = ghosts[0].world_py - 3;
    ghosts[0].dir = 4; // DIR_DOWN
    ghosts[0].frame = 10;
    ghosts[0].in_house = true;
    ghosts[0].min_home_py = 14 * MAZE_TILES_SIZE_PX;
    ghosts[0].max_home_py = 16 * MAZE_TILES_SIZE_PX;
    ghosts[0].state = 0; // GHOST_STATE_HOME_BOUNCE
    ghosts[0].sub_px = ghosts[0].world_px << 8;
    ghosts[0].sub_py = ghosts[0].world_py << 8;

    // Cyan (Inky): tile (21, 15) => world (168, 120) [Inside left]
    ghosts[1].world_px = 21 * MAZE_TILES_SIZE_PX; // 168
    ghosts[1].world_py = 15 * MAZE_TILES_SIZE_PX; // 120
    ghosts[1].x_pos_px = ghosts[1].world_px + maze_dx;
    ghosts[1].y_pos_px = ghosts[1].world_py - 3;
    ghosts[1].dir = 4; // DIR_DOWN
    ghosts[1].frame = 26;
    ghosts[1].in_house = true;
    ghosts[1].min_home_py = 14 * MAZE_TILES_SIZE_PX;
    ghosts[1].max_home_py = 16 * MAZE_TILES_SIZE_PX;
    ghosts[1].state = 0; // GHOST_STATE_HOME_BOUNCE
    ghosts[1].sub_px = ghosts[1].world_px << 8;
    ghosts[1].sub_py = ghosts[1].world_py << 8;

    // Pink (Pinky): tile (23, 16) => world (184, 128) [Inside center bottom]
    ghosts[2].world_px = 23 * MAZE_TILES_SIZE_PX; // 184
    ghosts[2].world_py = 16 * MAZE_TILES_SIZE_PX; // 128
    ghosts[2].x_pos_px = ghosts[2].world_px + maze_dx;
    ghosts[2].y_pos_px = ghosts[2].world_py - 3;
    ghosts[2].dir = 3; // DIR_UP
    ghosts[2].frame = 16;
    ghosts[2].in_house = true;
    ghosts[2].min_home_py = 14 * MAZE_TILES_SIZE_PX;
    ghosts[2].max_home_py = 16 * MAZE_TILES_SIZE_PX;
    ghosts[2].state = 0; // GHOST_STATE_HOME_BOUNCE
    ghosts[2].sub_px = ghosts[2].world_px << 8;
    ghosts[2].sub_py = ghosts[2].world_py << 8;

    // Orange (Clyde): tile (25, 15) => world (200, 120) [Inside right]
    ghosts[3].world_px = 25 * MAZE_TILES_SIZE_PX; // 200
    ghosts[3].world_py = 15 * MAZE_TILES_SIZE_PX; // 120
    ghosts[3].x_pos_px = ghosts[3].world_px + maze_dx;
    ghosts[3].y_pos_px = ghosts[3].world_py - 3;
    ghosts[3].dir = 4; // DIR_DOWN
    ghosts[3].frame = 34;
    ghosts[3].in_house = true;
    ghosts[3].min_home_py = 14 * MAZE_TILES_SIZE_PX;
    ghosts[3].max_home_py = 16 * MAZE_TILES_SIZE_PX;
    ghosts[3].state = 0; // GHOST_STATE_HOME_BOUNCE
    ghosts[3].sub_px = ghosts[3].world_px << 8;
    ghosts[3].sub_py = ghosts[3].world_py << 8;
}
