#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "tile_mode2.h"
#include "constants.h"

unsigned MAZE_CONFIG;

void tile_mode2_init(void) {

    MAZE_CONFIG = SPRITE_DATA_END; // After end of Sprite/Tile data.

    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_wrap, true);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, y_wrap, false);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px,-28); // Center maze horizontally (320px - 47 tiles * 8px) / 2 = 28px
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, y_pos_px, 0);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, width_tiles,      MAZE_MAP_WIDTH);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, height_tiles,     MAZE_MAP_HEIGHT);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, xram_data_ptr,    MAZE_MAP_DATA); // tile ID grid
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, xram_palette_ptr, MAZE_PALETTE_ADDR);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, xram_tile_ptr,    MAZE_TILES_DATA);        // tile bitmaps


    // Mode 2 args: MODE, OPTIONS, CONFIG, PLANE, BEGIN, END
    // OPTIONS: bit3=0 (8x8 tiles), bit[2:0]=2 (8-bit color index) => 0b0010 = 2
    // Plane 0 = background fill layer (behind sprite plane 1)
    if (xreg_vga_mode(2, 0x02, MAZE_CONFIG, 0, 24, 0) < 0) {
        return;
    }

    RIA.addr0 = MAZE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = maze_palette[i] & 0xFF;
        RIA.rw0 = maze_palette[i] >> 8;
    }

}