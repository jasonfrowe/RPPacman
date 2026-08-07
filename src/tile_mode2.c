#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "tile_mode2.h"
#include "constants.h"

unsigned MAZE_CONFIG;
unsigned TEXT_MAP_CONFIG;

// Define the data for the first line (Row 0)
const uint8_t first_line_data[TEXT_MAP_WIDTH] = {
    0,0,0,0,0,37,21,16,0,46,46,46,46,46,46,46,0,0,0,41,
    54,46,46,0,0,8,9,0,46,46,46,46,46,46,46,0,0,0,0,0
};

// Define the data for the last line (Row 14)
const uint8_t last_line_data[TEXT_MAP_WIDTH] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,53,46,39,0,0,0,0,0
};

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

void init_tilemap_edges(void) {
    // --- Write the FIRST line (Row 0) ---
    RIA.addr0 = TEXT_MAP_DATA; // Start at the very beginning of the tilemap
    RIA.step0 = 1;
    for (int i = 0; i < TEXT_MAP_WIDTH; i++) {
        RIA.rw0 = first_line_data[i];
    }

    // --- Write the LAST line (Row 14) ---
    // Calculate the start address of the last row: Base + ((Height - 1) * Width)
    RIA.addr0 = TEXT_MAP_DATA + ((TEXT_MAP_HEIGHT - 1) * TEXT_MAP_WIDTH); 
    RIA.step0 = 1;
    for (int i = 0; i < TEXT_MAP_WIDTH; i++) {
        RIA.rw0 = last_line_data[i];
    }
}

void tile_mode2_text_map_init(void) {

    TEXT_MAP_CONFIG = GHOST_CONFIG + NGHOSTS * sizeof(vga_mode5_sprite_t); // After ghost config

    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, x_wrap, false);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, y_wrap, false);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, x_pos_px, 0);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, y_pos_px, 0);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, width_tiles,      TEXT_MAP_WIDTH);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, height_tiles,     TEXT_MAP_HEIGHT);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, xram_data_ptr,    TEXT_MAP_DATA);     // tile ID grid
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, xram_palette_ptr, FONT_PALETTE_ADDR);
    xram0_struct_set(TEXT_MAP_CONFIG, vga_mode2_config_t, xram_tile_ptr,    FONT_TILES_DATA);   // tile bitmaps

    RIA.addr0 = FONT_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = font_palette[i] & 0xFF;
        RIA.rw0 = font_palette[i] >> 8;
    }

    init_tilemap_edges();

    // Mode 2 args: MODE, OPTIONS, CONFIG, PLANE, BEGIN, END
    // OPTIONS: bit3=1 (16x16 tiles), bit[2:0]=2 (8-bit color index) 
    // bit[4:7]=1000 (trim 8 pixels x) bit[8:11]=0000 (no trim y ) => 0b0000 1000 1010 = 0x08A
    // Plane 1 = background fill layer 
    if (xreg_vga_mode(2, 0x08A, TEXT_MAP_CONFIG, 1, 0, 0) < 0) {
        return;
    }

}