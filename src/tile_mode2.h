#ifndef TILE_MODE2_H
#define TILE_MODE2_H

#include <stdbool.h>
#include <stdint.h>

// Palette extracted from Sprites/Tiles.png
static const uint16_t maze_palette[16] = {
    0x0000,
    0x0020,
    0x30B6,
    0x623B,
    0x6D29,
    0x053F,
    0xB825,
    0x82A6,
    0xFA28,
    0x972E,
    0xE6AC,
    0x97BF,
    0xFDF4,
    0xFFFF,
    0x0020,
    0x0020,
};

static const uint16_t font_palette[16] = {
    0x0000,
    0x0A60,
    0x402D,
    0x036D,
    0xF92E,
    0xFA28,
    0xFDAC,
    0x8728,
    0x21B6,
    0x06F1,
    0x743F,
    0xAD75,
    0xFCB2,
    0x9779,
    0x973C,
    0xFFFF,
};

void tile_mode2_init(void);
void tile_mode2_text_map_init(void);
extern int16_t maze_dx;
extern int8_t maze_d1; // Change in maze_dx for this frame, used to adjust ghost positions

#endif // TILE_MODE2_H