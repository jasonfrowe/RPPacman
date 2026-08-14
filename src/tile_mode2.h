#ifndef TILE_MODE2_H
#define TILE_MODE2_H

#include <stdbool.h>
#include <stdint.h>

// Palette extracted from Sprites/Tiles.png
static const uint16_t maze_palette[16] = {
    0x0000,
    0x0020, // black background 
    0x30B6, // dark red
    0x623B, // pink-red
    0x6D29, // green
    0x053F, // yellow -- power pellets
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
    0x05F7,
    0xFFFF,
};

// Palette extracted from graphics/Titles_RP6502.png
static const uint16_t titles_palette[16] = {
    0x0000,
    0x0020,
    0x01A6,
    0x036D,
    0x05F7,
    0x59A0,
    0xFDAC,
    0xF5AF,
    0xFFFF,
    0x0AB6,
    0x0020,
    0x0020,
    0x0020,
    0x0020,
    0x0020,
    0x0020,
};

void tile_mode2_init(void);
void tile_mode2_text_map_init(void);
void tile_mode2_title_map_init(void);
extern int16_t maze_dx;
extern int8_t maze_d1; // Change in maze_dx for this frame, used to adjust ghost positions

void write_text_to_text_map(uint8_t tx, uint8_t ty, const char *str);
void set_maze_palette_black(void);
void restore_maze_palette(void);
void set_title_palette_black(void);
void restore_title_palette(void);
void set_title_palette_scaled(uint8_t num, uint8_t den, bool white_boost, uint8_t white_step, bool cold_start);
void set_font_palette_black(void);
void restore_font_palette(void);
void set_font_palette_scaled(uint8_t num, uint8_t den);

void update_player_score_display(uint32_t score);
void update_player_lives_display(uint8_t lives);
void trigger_extra_life_blink(void);
void update_lives_blink_animation(void);

void reset_game_timer(void);
void update_game_timer_display(void);
bool is_game_timer_expired(void);

extern void tile_mode2_palette_update(uint8_t frame);

#endif // TILE_MODE2_H