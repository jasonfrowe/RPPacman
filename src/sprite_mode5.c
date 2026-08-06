#include <rp6502.h>
#include <stdint.h>
#include "constants.h"
#include "sprite_mode5.h"

// Store the player config address for updates
unsigned PLAYER_CONFIG;

static uint8_t player_frame = 0; // Current frame index for the player sprite (0-7)

void sprite_mode5_init(void) {

    int16_t center_x = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    int16_t center_y = (int16_t)((SCREEN_HEIGHT - SPRITE_SIZE_PX) * 2 / 3 + 16); // Start slightly lower than center for better composition

    player_frame = 5; // Start with the "facing left / closed" frame for the player sprite

    PLAYER_CONFIG = MAZE_CONFIG + sizeof(vga_mode2_config_t); // After maze config

    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, center_x);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, center_y);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player_frame * SPRITE_FRAME_SIZE)));
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);

    // Mode 5 args: MODE, OPTIONS, CONFIG, LENGTH, PLANE, BEGIN, END
    if (xreg_vga_mode(5, 0x0A, PLAYER_CONFIG, 1, 1, 0, 0) < 0) {
        return;
    }


    RIA.addr0 = PLAYER_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = player_palette[i] & 0xFF;
        RIA.rw0 = player_palette[i] >> 8;
    }


}