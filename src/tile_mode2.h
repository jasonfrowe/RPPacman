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

void tile_mode2_init(void);

#endif // TILE_MODE2_H