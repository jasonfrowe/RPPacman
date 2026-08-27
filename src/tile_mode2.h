#ifndef TILE_MODE2_H
#define TILE_MODE2_H

#include <stdbool.h>
#include <stdint.h>


// Palette extracted from Sprites/Tiles.png
// static const uint16_t maze_palette_v1[16] = {
//     0x0000,
//     0x0020, // black background
//     0x21F8, // orange-red
//     0x7C7F, // pink
//     0x6D29, // green
//     0x053F, // yellow -- power pellets
//     0x78E7, // dark purple -- maze outline (6)
//     0x82A6, // dark grey
//     0xBAAE, // purple -- maze-outline (8)
//     0x78E7, // beat-accent flash color (9) -- see tile_mode2_palette_update();
//             // matches index 6's normal (non-frightened) color so this
//             // shipped default's tiny existing tile-125 sparkle decoration
//             // keeps flashing exactly as before.
//     0xE6AC,
//     0x0020, // index for animations. (11)
//     0xFDF4,
//     0xFFFF,
//     0x0020,
//     0x0020,
// };

// "remaster" branch experiment: alternate maze tile set
// (graphics/Maze_tiles_v2.png -> images/Maze_tiles_v2_4bpp.bin) adding a
// genuine 3rd color/accent on top of the UNCHANGED original two maze-
// outline colors (indices 6/8 keep maze_palette's exact values, including
// the frightened-mode color cycle -- an earlier version of this reskin
// moved the outline colors to a new ice-blue theme and that made the
// power-pellet/frightened palette swap far less visible, which is
// specifically what this version restores).
//   - Indices 6/8: same dark-purple/purple maze-outline colors as
//     maze_palette, still swapped by set_frightened_palette_stage() during
//     frightened mode exactly as before.
//   - Index 9: the beat-accent's dedicated flash color -- bright icy
//     white-cyan, deliberately independent of 6/8 so it reads as its own
//     3rd hue rather than the outline simply relighting.
//   - Index 11: NEW pixels added to the art (not a remap of an existing
//     line) tracing wherever the two outline lines run close together --
//     a thin center-fill on straights, and scattered highlights on
//     corners/T-junctions where their curves happen to pinch tight. Sits
//     black/invisible between beats and flashes to index 9's color for 8
//     frames after each kick/snare/cymbal hit (tile_mode2_palette_update()),
//     same as the maze's existing tile-125 sparkle decorations.
// Indices 0/1/5 keep the exact same roles/values as maze_palette
// (transparent, dot-blink black, pellet orange) -- just point the maze
// tile ROM asset load at "ROM:mazetilesv2" and use this array instead of
// maze_palette.
static const uint16_t maze_palette[16] = {
    0x0000,
    0x0020, // black background
    0x21F8,
    0x7C7F,
    0x6D29,
    0x053F, // pellet orange -- unchanged
    0x78E7, // dark purple -- maze outline (6), unchanged from maze_palette
    0x82A6,
    0xBAAE, // purple -- maze-outline (8), unchanged from maze_palette
    0x9AAE, // purple -- dedicated beat-accent flash color (9) 0x9AAE
    0xE6AC,
    0x0020, // beat-flash slot -- unchanged, dynamically overwritten (11)
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
    0x04A1,
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
    0xFEF8,
    0xEE3F,
    0xFA28,
    0xBAAE,
    0x0020,
    0x0020,
};

void tile_mode2_init(void);
void tile_mode2_text_map_init(void);
void tile_mode2_title_map_init(void);
void init_tilemap_edges(void); // Redraws the static row 0 ("1UP"/"HI" labels) and row 13 (Pac-Man/x lives marker) chrome
extern int16_t maze_dx;
extern int8_t maze_d1; // Change in maze_dx for this frame, used to adjust ghost positions

void write_text_to_text_map(uint8_t tx, uint8_t ty, const char *str);
void set_maze_palette_black(void);
void restore_maze_palette(void);
void set_frightened_palette(bool active);
void set_frightened_palette_stage(uint8_t stage);
void set_title_palette_black(void);
void restore_title_palette(void);
void set_title_palette_scaled(uint8_t num, uint8_t den, bool white_boost, uint8_t white_step, bool cold_start);
void set_font_palette_black(void);
void restore_font_palette(void);
void set_font_palette_scaled(uint8_t num, uint8_t den);

void update_player_score_display(uint32_t score);
void update_hiscore_display(uint32_t score);
void update_player_lives_display(uint8_t lives);
void trigger_extra_life_blink(void);
void update_lives_blink_animation(void);

void reset_game_timer(void);
void update_game_timer_display(void);
bool is_game_timer_expired(void);
uint16_t get_game_elapsed_frames(void);
uint16_t get_game_remaining_frames(void);

void update_ready_go_animation(uint16_t timer_frame);
void clear_ready_go_animation(void);

extern void tile_mode2_palette_update(uint8_t frame);

#endif // TILE_MODE2_H