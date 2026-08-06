#ifndef SPRITE_MODE5_H
#define SPRITE_MODE5_H

#include <stdint.h>
#include <stdbool.h>

// Palette extracted from Sprites/Player.png
static const uint16_t player_palette[16] = {
    0x0000,
    0x0020,
    0xDE69,
    0xFFFF,
    0x05F7,
    0x24FD,
    0x21B6,
    0xEE3F,
    0xFA28,
    0x04A1,
    0x39E7,
    0x05F7,
    0x0020,
    0x0020,
    0x0020,
    0x0020,
};

void sprite_mode5_init(void);

#endif // SPRITE_MODE5_H