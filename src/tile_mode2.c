#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "tile_mode2.h"
#include "constants.h"
#include "ghost.h"
#include "opl.h"

unsigned MAZE_CONFIG;
unsigned TEXT_MAP_CONFIG;
unsigned TITLE_MAP_CONFIG;

// Define the data for the first line (Row 0)
const uint8_t first_line_data[TEXT_MAP_WIDTH] = {
    0,0,0,0,0,37,21,16,0,46,46,46,46,46,46,46,0,0,0,41,
    54,46,46,0,0,8,9,0,46,46,46,46,46,46,46,0,0,0,0,0
};

// Define the data for the last line (Row 13)
const uint8_t last_line_data[TEXT_MAP_WIDTH] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,55,56,53,46,39,0,0,0,0,0
};

int16_t maze_dx = -28; // Center maze horizontally (320px - 47 tiles * 8px) / 2 = 28px
int8_t maze_d1 = 0; // Change in maze_dx for this frame, used to adjust ghost positions

void tile_mode2_init(void) {

    MAZE_CONFIG = SPRITE_DATA_END; // After end of Sprite/Tile data.

    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_wrap, true);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, y_wrap, false);
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx); // Center maze horizontally (320px - 47 tiles * 8px) / 2 = 28px
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

    // --- Write the LAST line (Row 13) ---
    // Calculate the start address of Row 13: Base + ((Height - 2) * Width)
    RIA.addr0 = TEXT_MAP_DATA + ((TEXT_MAP_HEIGHT - 2) * TEXT_MAP_WIDTH); 
    RIA.step0 = 1;
    for (int i = 0; i < TEXT_MAP_WIDTH; i++) {
        RIA.rw0 = last_line_data[i];
    }
}

void tile_mode2_text_map_init(void) {

    TEXT_MAP_CONFIG = PLAYER_CONFIG + sizeof(vga_mode5_sprite_t); // After prize config

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
    // Plane 1 = text overlay layer (top 28 scanlines only: BEGIN=0, END=28)
    if (xreg_vga_mode(2, 0x08A, TEXT_MAP_CONFIG, 2, 0, 0) < 0) {
        return;
    }

}

void tile_mode2_title_map_init(void) {

    TITLE_MAP_CONFIG = TEXT_MAP_CONFIG + sizeof(vga_mode2_config_t); // After text map config

    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, x_wrap, false);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, y_wrap, false);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, x_pos_px, 0);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, y_pos_px, 100);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, width_tiles,      TITLE_MAP_WIDTH);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, height_tiles,     TITLE_MAP_HEIGHT);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, xram_data_ptr,    TITLE_MAP_DATA);     // tile ID grid
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, xram_palette_ptr, TITLE_PALETTE_ADDR);
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, xram_tile_ptr,    TITLE_TILES_DATA);   // tile bitmaps

    RIA.addr0 = TITLE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = titles_palette[i] & 0xFF;
        RIA.rw0 = titles_palette[i] >> 8;
    }

    // Mode 2 args: MODE, OPTIONS, CONFIG, PLANE, BEGIN, END
    // OPTIONS: bit3=0 (8x8 tiles), bit[2:0]=2 (8-bit color index) 
    // bit[4:7]=0000 (no trim x) bit[8:11]=0000 (no trim y ) => 0b0000 0000 0010 = 0x002
    // Plane 3 = title overlay layer (top 28 scanlines only: BEGIN=0, END=28)
    if (xreg_vga_mode(2, 0x002, TITLE_MAP_CONFIG, 1, 0, 0) < 0) {
        return;
    }

}

void write_text_to_text_map(uint8_t tx, uint8_t ty, const char *str) {
    uint16_t offset = ty * TEXT_MAP_WIDTH + tx;
    RIA.addr0 = TEXT_MAP_DATA + offset;
    RIA.step0 = 1;
    while (*str) {
        char c = *str++;
        uint8_t tile_idx = 0;
        if (c >= 'A' && c <= 'Z') {
            tile_idx = 1 + (c - 'A'); // A=1 .. Z=26
        } else if (c >= '1' && c <= '9') {
            tile_idx = 37 + (c - '1'); // 1=37 .. 9=45
        } else if (c == '0') {
            tile_idx = 46; // 0=46
        } else if (c == ' ') {
            tile_idx = 0; // blank
        }
        RIA.rw0 = tile_idx;
    }
}

static bool s_maze_palette_black = false;
static uint16_t s_index6_color = 0; // set by restore_maze_palette()/set_frightened_palette()
static uint8_t s_kick_flash_timer = 0; // frames remaining for the kick-drum beat flash (index 11)

void set_maze_palette_black(void) {
    s_maze_palette_black = true;
    RIA.addr0 = MAZE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = 0x00;
        RIA.rw0 = 0x00;
    }
}

void restore_maze_palette(void) {
    s_maze_palette_black = false;
    RIA.addr0 = MAZE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = maze_palette[i] & 0xFF;
        RIA.rw0 = maze_palette[i] >> 8;
    }
    s_index6_color = maze_palette[6];
}

// The 8-stage frightened outline-color cycle (indices 6/8). Ear/eye-tuned
// values from the user; stages 0-2 repeat once before stages 6-7 finish
// the sequence. Advanced by ghost.c's frightened timer, spread evenly
// across however long the current frightened period actually lasts, so
// the cycle runs faster on later levels (shorter frightened duration)
// without needing any separate speed table here.
static const uint16_t FRIGHTENED_PALETTE_STAGES[8][2] = {
    // { index8, index6 }
    {0xB53D, 0x6274},
    {0x8D3C, 0x1AB3},
    {0xE53B, 0x9A72},
    {0xB53D, 0x6274},
    {0x8D3C, 0x1AB3},
    {0xE53B, 0x9A72},
    {0x5A35, 0x00EB},
    {0x9272, 0x58AB},
};

// Writes one stage of the frightened outline-color cycle (indices 6/8).
// s_index6_color tracks whatever index 6 currently holds -- the kick-drum
// beat flash (index 11) mirrors it, frightened or not.
void set_frightened_palette_stage(uint8_t stage) {
    if (stage > 7) stage = 7;
    uint16_t c8 = FRIGHTENED_PALETTE_STAGES[stage][0];
    uint16_t c6 = FRIGHTENED_PALETTE_STAGES[stage][1];

    RIA.addr0 = MAZE_PALETTE_ADDR + (6 * 2);
    RIA.step0 = 1;
    RIA.rw0 = c6 & 0xFF;
    RIA.rw0 = c6 >> 8;
    s_index6_color = c6;

    RIA.addr0 = MAZE_PALETTE_ADDR + (8 * 2);
    RIA.step0 = 1;
    RIA.rw0 = c8 & 0xFF;
    RIA.rw0 = c8 >> 8;
}

// Swaps the maze-outline colors (indices 6 and 8) to the frightened
// cycle's first stage while ghosts are vulnerable, or back to their
// normal values otherwise. See set_frightened_palette_stage() for the
// per-stage writes used while frightened is actually running.
void set_frightened_palette(bool active) {
    if (active) {
        set_frightened_palette_stage(0);
        return;
    }

    uint16_t c6 = maze_palette[6];
    uint16_t c8 = maze_palette[8];

    RIA.addr0 = MAZE_PALETTE_ADDR + (6 * 2);
    RIA.step0 = 1;
    RIA.rw0 = c6 & 0xFF;
    RIA.rw0 = c6 >> 8;
    s_index6_color = c6;

    RIA.addr0 = MAZE_PALETTE_ADDR + (8 * 2);
    RIA.step0 = 1;
    RIA.rw0 = c8 & 0xFF;
    RIA.rw0 = c8 >> 8;
}

void set_title_palette_black(void) {
    RIA.addr0 = TITLE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = 0x00;
        RIA.rw0 = 0x00;
    }
}

void restore_title_palette(void) {
    RIA.addr0 = TITLE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = titles_palette[i] & 0xFF;
        RIA.rw0 = titles_palette[i] >> 8;
    }
}

// Scale RGB555 palette color (scale_num / scale_den)
// Desaturates towards neutral gray during fade transitions to prevent color flashes
static uint16_t scale_rgb555(uint16_t color, uint8_t num, uint8_t den, bool white_boost, uint8_t white_step) {
    uint8_t r = (color >> 10) & 0x1F;
    uint8_t g = (color >> 5)  & 0x1F;
    uint8_t b =  color        & 0x1F;

    if (white_boost) {
        // white_step 1..4: scale r,g,b towards 31 (0x1F)
        r = r + (((31 - r) * white_step) / 4);
        g = g + (((31 - g) * white_step) / 4);
        b = b + (((31 - b) * white_step) / 4);
    } else {
        // Compute perceived luminance Y = 0.3R + 0.59G + 0.11B
        uint8_t gray = (uint8_t)((3 * (uint16_t)r + 6 * (uint16_t)g + 1 * (uint16_t)b) / 10);

        // Blend color towards gray as num / den gets smaller (desaturation factor)
        // factor = num / den (e.g. 1/4 -> 75% gray, 3/4 -> 25% gray, 4/4 -> 0% gray)
        r = (r * num + gray * (den - num)) / den;
        g = (g * num + gray * (den - num)) / den;
        b = (b * num + gray * (den - num)) / den;

        // Apply brightness scaling
        r = (r * num) / den;
        g = (g * num) / den;
        b = (b * num) / den;
    }

    return (uint16_t)((r << 10) | (g << 5) | b);
}

void set_title_palette_scaled(uint8_t num, uint8_t den, bool white_boost, uint8_t white_step, bool cold_start) {
    RIA.addr0 = TITLE_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        uint16_t c = titles_palette[i];
        if (cold_start && (i == 5 || i == 7)) {
            c = 0x0000;
        }
        if (num == 0 && !white_boost) {
            c = 0x0000;
        } else {
            c = scale_rgb555(c, num, den, white_boost, white_step);
        }
        RIA.rw0 = c & 0xFF;
        RIA.rw0 = c >> 8;
    }
}

void set_font_palette_black(void) {
    RIA.addr0 = FONT_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = 0x00;
        RIA.rw0 = 0x00;
    }
}

void restore_font_palette(void) {
    RIA.addr0 = FONT_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        RIA.rw0 = font_palette[i] & 0xFF;
        RIA.rw0 = font_palette[i] >> 8;
    }
}

void set_font_palette_scaled(uint8_t num, uint8_t den) {
    RIA.addr0 = FONT_PALETTE_ADDR;
    RIA.step0 = 1;
    for (int i = 0; i < 16; i++) {
        uint16_t c = font_palette[i];
        if (num == 0) {
            c = 0x0000;
        } else {
            c = scale_rgb555(c, num, den, false, 0);
        }
        RIA.rw0 = c & 0xFF;
        RIA.rw0 = c >> 8;
    }
}

void tile_mode2_palette_update(uint8_t frame){
    if (s_maze_palette_black) return;

    // 2 Hz cadence at 60 FPS: toggle every 15 frames (30 frames for a full cycle)
    int color_index = ((frame % 30) < 15) ? 5 : 1;

    // Address of the pellet color in the maze palette (2 bytes per colour, so +2 is the second color)
    RIA.addr0 = MAZE_PALETTE_ADDR + 2; 
    RIA.step0 = 1;

    // Write the chosen color
    RIA.rw0 = maze_palette[color_index] & 0xFF; // Low byte of the color
    RIA.rw0 = maze_palette[color_index] >> 8;   // High byte of the color

    // Beat flash (index 11): mirrors whatever index 6 currently holds
    // (normal or frightened, via s_index6_color) for 8 frames after each
    // real kick/snare/cymbal hit (opl_consume_kick_hit(), despite the
    // name, isn't kick-only -- see its comment), black otherwise.
    if (opl_consume_kick_hit()) {
        s_kick_flash_timer = 8;
    }
    uint16_t c11;
    if (s_kick_flash_timer > 0) {
        s_kick_flash_timer--;
        c11 = s_index6_color;
    } else {
        c11 = 0x0020;
    }
    RIA.addr0 = MAZE_PALETTE_ADDR + (11 * 2);
    RIA.step0 = 1;
    RIA.rw0 = c11 & 0xFF;
    RIA.rw0 = c11 >> 8;
}

void update_player_score_display(uint32_t score) {
    // 7-digit score at indices 9 through 15 on row 0 of TEXT_MAP_DATA
    // Digit to tile index mapping: 0 -> 46, 1 -> 37, 2 -> 38, ..., 9 -> 45
    static const uint8_t digit_tile_map[10] = {
        46, 37, 38, 39, 40, 41, 42, 43, 44, 45
    };

    uint8_t digits[7];
    uint32_t val = score;

    for (int8_t i = 6; i >= 0; i--) {
        uint32_t next_val = val / 10;
        uint8_t rem = (uint8_t)(val - (next_val * 10));
        digits[i] = digit_tile_map[rem];
        val = next_val;
    }

    RIA.addr0 = TEXT_MAP_DATA + 9;
    RIA.step0 = 1;
    for (uint8_t i = 0; i < 7; i++) {
        RIA.rw0 = digits[i];
    }
}

// Player Lives display management on Row 13 of TEXT_MAP_DATA
// Indices 32 ('x', tile index 53), 33 (tens digit tile index 37..46), and 34 (ones digit tile index 37..46)
static uint8_t s_current_lives = 3;
static uint16_t s_lives_blink_timer = 0; // Total 128 frames for 8 blinks (16 frames per blink: 8 off, 8 on)

void update_player_lives_display(uint8_t lives) {
    if (lives > 99) lives = 99; // Cap at 99
    s_current_lives = lives;

    static const uint8_t digit_tile_map[10] = {
        46, 37, 38, 39, 40, 41, 42, 43, 44, 45
    };

    uint8_t tens_digit = lives / 10;
    uint8_t ones_digit = lives % 10;

    uint8_t tens_tile = digit_tile_map[tens_digit];
    uint8_t ones_tile = digit_tile_map[ones_digit];

    // Row 13 base address: TEXT_MAP_DATA + (13 * 40) = TEXT_MAP_DATA + 520
    uint16_t row13_addr = TEXT_MAP_DATA + (13 * TEXT_MAP_WIDTH);

    RIA.addr0 = row13_addr + 32; // Index 32 -> 'x' (tile 53)
    RIA.step0 = 1;
    RIA.rw0 = 53;

    RIA.addr0 = row13_addr + 33; // Index 33 -> Tens digit (e.g. '0' for 03, '1' for 12, '9' for 99)
    RIA.step0 = 1;
    RIA.rw0 = tens_tile;

    RIA.addr0 = row13_addr + 34; // Index 34 -> Ones digit
    RIA.step0 = 1;
    RIA.rw0 = ones_tile;
}

void trigger_extra_life_blink(void) {
    // 8 blinks: each blink has 8 frames off, 8 frames on => 16 frames per blink * 8 = 128 frames total
    // Step 1: Blank out 'x', tens digit, and ones digit on frame 1
    uint16_t row13_addr = TEXT_MAP_DATA + (13 * TEXT_MAP_WIDTH);
    RIA.addr0 = row13_addr + 32; // Index 32 ('x')
    RIA.step0 = 1;
    RIA.rw0 = 0; // Blank tile 0

    RIA.addr0 = row13_addr + 33; // Index 33 (tens digit)
    RIA.step0 = 1;
    RIA.rw0 = 0; // Blank tile 0

    RIA.addr0 = row13_addr + 34; // Index 34 (ones digit)
    RIA.step0 = 1;
    RIA.rw0 = 0; // Blank tile 0

    s_lives_blink_timer = 128; // Start 128-frame blink sequence
}

void update_lives_blink_animation(void) {
    if (s_lives_blink_timer == 0) return;

    s_lives_blink_timer--;

    uint16_t row13_addr = TEXT_MAP_DATA + (13 * TEXT_MAP_WIDTH);
    static const uint8_t digit_tile_map[10] = {
        46, 37, 38, 39, 40, 41, 42, 43, 44, 45
    };
    uint8_t lives = (s_current_lives > 99) ? 99 : s_current_lives;
    uint8_t tens_tile = digit_tile_map[lives / 10];
    uint8_t ones_tile = digit_tile_map[lives % 10];

    if (s_lives_blink_timer == 127) {
        // Frame 2 (frame 127 counting down): Put 'x', tens digit, and ones digit back
        RIA.addr0 = row13_addr + 32;
        RIA.step0 = 1;
        RIA.rw0 = 53;

        RIA.addr0 = row13_addr + 33;
        RIA.step0 = 1;
        RIA.rw0 = tens_tile;

        RIA.addr0 = row13_addr + 34;
        RIA.step0 = 1;
        RIA.rw0 = ones_tile;
    } else if (s_lives_blink_timer > 0) {
        // Blink remaining frames: 16 frames per cycle (8 off, 8 on)
        // (s_lives_blink_timer % 16) < 8 -> OFF (blank 0), >= 8 -> ON (show tiles)
        uint8_t cycle_step = s_lives_blink_timer % 16;
        bool show_visible = (cycle_step >= 8);

        RIA.addr0 = row13_addr + 32;
        RIA.step0 = 1;
        RIA.rw0 = show_visible ? 53 : 0;

        RIA.addr0 = row13_addr + 33;
        RIA.step0 = 1;
        RIA.rw0 = show_visible ? tens_tile : 0;

        RIA.addr0 = row13_addr + 34;
        RIA.step0 = 1;
        RIA.rw0 = show_visible ? ones_tile : 0;
    } else {
        // Blink sequence complete (timer == 0): ensure final state is visible ON
        RIA.addr0 = row13_addr + 32;
        RIA.step0 = 1;
        RIA.rw0 = 53;

        RIA.addr0 = row13_addr + 33;
        RIA.step0 = 1;
        RIA.rw0 = tens_tile;

        RIA.addr0 = row13_addr + 34;
        RIA.step0 = 1;
        RIA.rw0 = ones_tile;
    }
}

// 5-minute countdown timer (5:00 -> 300 seconds -> 18000 frames at 60 FPS)
static uint16_t s_game_timer_frames = 18000;

void reset_game_timer(void) {
    s_game_timer_frames = 18000; // 5:00 (300 seconds * 60 FPS)
    update_game_timer_display();
}

bool is_game_timer_expired(void) {
    return (s_game_timer_frames == 0);
}

// Elapsed gameplay frames since reset_game_timer(), for the results
// screen's 10-second history buckets -- the 5-minute cap means this
// never exceeds 18000, which lines up exactly with 30 buckets of 600
// frames (10s) each.
uint16_t get_game_elapsed_frames(void) {
    return 18000 - s_game_timer_frames;
}

void update_game_timer_display(void) {
    if (s_game_timer_frames > 0 && is_game_motion_started()) {
        s_game_timer_frames--;
    }

    static const uint8_t digit_tile_map[10] = {
        46, 37, 38, 39, 40, 41, 42, 43, 44, 45
    };

    uint16_t total_seconds = (s_game_timer_frames + 59) / 60; // Round up so 300s shows 5'00
    if (total_seconds > 300) total_seconds = 300;

    uint8_t minutes = total_seconds / 60;
    uint8_t seconds = total_seconds % 60;

    uint8_t min_tile = digit_tile_map[minutes % 10];
    uint8_t sec_tens_tile = digit_tile_map[seconds / 10];
    uint8_t sec_ones_tile = digit_tile_map[seconds % 10];

    // Indices 19, 20, 21, 22 on Row 0:
    // Index 19 -> Minute digit (e.g., 41 for '5')
    // Index 20 -> Apostrophe '\'' (tile 54)
    // Index 21 -> Tens of seconds digit (e.g., 46 for '0')
    // Index 22 -> Ones of seconds digit (e.g., 46 for '0')
    RIA.addr0 = TEXT_MAP_DATA + 19;
    RIA.step0 = 1;
    RIA.rw0 = min_tile;
    RIA.rw0 = 54; // '\''
    RIA.rw0 = sec_tens_tile;
    RIA.rw0 = sec_ones_tile;
}

// Clear READY / GO region on TEXT_MAP_DATA (rows 4 and 5)
void clear_ready_go_animation(void) {
    for (uint8_t ty = 4; ty <= 5; ty++) {
        uint16_t row_addr = TEXT_MAP_DATA + (ty * TEXT_MAP_WIDTH);
        RIA.addr0 = row_addr;
        RIA.step0 = 1;
        for (uint8_t tx = 0; tx < TEXT_MAP_WIDTH; tx++) {
            RIA.rw0 = 0;
        }
    }
}

// READY / GO intro animation during the 236-frame PACMAN01.BIN intro
// READY phase: timer_frame 0..92 (first 93 frames of intro)
// GO phase: timer_frame 93..235 (remaining 143 frames)
void update_ready_go_animation(uint16_t timer_frame) {
    // 2-frame continuous flash loop toggle (0: odd visible, 1: even visible)
    bool odd_visible = ((timer_frame % 2) == 0);

    if (timer_frame < 93) {
        // --- READY MESSAGE ---
        // 16x2 map starting at base tile index 57. Target: tile coord (12, 4)
        // Row 0: 57..72 (16 tiles), Row 1: 73..88 (16 tiles)
        // Convergence over first 8 frames (timer_frame 0..7)
        int16_t offset_x = 0;
        if (timer_frame < 8) {
            // Step shrinks linearly from 14 tiles down to 0 tiles
            offset_x = (int16_t)((7 - timer_frame) * 2);
        }

        clear_ready_go_animation();

        for (uint8_t r = 0; r < 2; r++) {
            uint8_t target_ty = 4 + r;
            uint16_t base_tile = (r == 0) ? 57 : 73;

            for (uint8_t col = 0; col < 16; col++) {
                uint16_t raw_tile = base_tile + col;

                // Odd-number tiles vs Even-number tiles flashing
                bool tile_is_odd = ((raw_tile % 2) != 0);
                uint8_t draw_tile = (tile_is_odd == odd_visible) ? (uint8_t)raw_tile : 0;

                if (draw_tile != 0) {
                    // Spread tiles out symmetrically: left half shifts left (-offset_x), right half shifts right (+offset_x)
                    int16_t dest_tx = (int16_t)(12 + col);
                    if (col < 8) {
                        dest_tx -= offset_x;
                    } else {
                        dest_tx += offset_x;
                    }

                    if (dest_tx >= 0 && dest_tx < TEXT_MAP_WIDTH) {
                        RIA.addr0 = TEXT_MAP_DATA + (target_ty * TEXT_MAP_WIDTH) + dest_tx;
                        RIA.step0 = 1;
                        RIA.rw0 = draw_tile;
                    }
                }
            }
        }
    } else {
        // --- GO MESSAGE ---
        // 10x2 grid starting at base tile index 89. Target: tile coord (15, 4)
        // Row 0: 89..98 (10 tiles), Row 1: 99..108 (10 tiles)
        clear_ready_go_animation();

        for (uint8_t r = 0; r < 2; r++) {
            uint8_t target_ty = 4 + r;
            uint16_t base_tile = (r == 0) ? 89 : 99;

            for (uint8_t col = 0; col < 10; col++) {
                uint16_t raw_tile = base_tile + col;

                bool tile_is_odd = ((raw_tile % 2) != 0);
                uint8_t draw_tile = (tile_is_odd == odd_visible) ? (uint8_t)raw_tile : 0;

                if (draw_tile != 0) {
                    uint8_t dest_tx = 15 + col;
                    if (dest_tx < TEXT_MAP_WIDTH) {
                        RIA.addr0 = TEXT_MAP_DATA + (target_ty * TEXT_MAP_WIDTH) + dest_tx;
                        RIA.step0 = 1;
                        RIA.rw0 = draw_tile;
                    }
                }
            }
        }
    }
}