#ifndef SPRITE_MODE5_H
#define SPRITE_MODE5_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int16_t x_pos_px;
    int16_t y_pos_px;
    int16_t world_px;
    int16_t world_py;
    int8_t frame;
} ghost_struct;

extern ghost_struct ghosts[NGHOSTS];

typedef struct {
    uint16_t tile_x;
    uint16_t tile_y;
    uint8_t timer; // frame countdown timer (30 frames max)
    bool active;
} eaten_tile_popup_t;

typedef struct {
    int16_t x_pos_px;
    int16_t y_pos_px;
    int16_t world_px;
    int16_t world_py;
    int8_t frame;
    int8_t lives;
    int8_t dir; // Direction: 0=left, 1=right, 2=up, 3=down
    uint32_t score;
    uint16_t pellets_eaten;
} player_struct;

extern player_struct player;

typedef struct {
    int16_t x_pos_px;
    int16_t y_pos_px;
    int16_t world_px;
    int16_t world_py;
    int8_t frame;
    int16_t x_sparkle_px; // position of sparkle effect animation
    int16_t y_sparkle_px;
} prize_struct;

extern prize_struct prizes[NPRIZES];

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
void init_ghost_data(void);

#endif // SPRITE_MODE5_H