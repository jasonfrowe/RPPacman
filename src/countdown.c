#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include "constants.h"
#include "countdown.h"
#include "tile_mode2.h"
#include "opl.h"

// Same rows the title-screen READY/GO animation uses on TEXT_MAP_DATA.
#define CD_ROW0_TY 4
#define CD_ROW1_TY 5

#define CD_FLASH_PERIOD       8   // 4 frames visible, 4 blank
#define CD_CONVERGE_FRAMES    8   // matches update_ready_go_animation()'s own convergence window
#define CD_DIGIT_HOLD_FRAMES  60  // 1 second per digit
#define CD_FINISH_HOLD_FRAMES 180 // 3 seconds

// readygopt1/pt2/pt3 are each 52 frames of 16x16 4bpp font-format tiles
// (128 bytes/frame). READY/GO already live at FONT_TILES_DATA tile
// indices 57-108 (readygopt1 is just a saved copy of that same range) --
// the countdown reuses that same slot, temporarily overwriting it with
// readygopt2/pt3's digit/FINISH! tiles, and restores readygopt1 there
// once it's done so the next game's own READY/GO sequence is intact.
#define FONT_TILE_FRAME_SIZE 128
#define READYGO_BASE_TILE    57
#define READYGO_ASSET_SIZE   (52 * FONT_TILE_FRAME_SIZE)
#define READYGO_DEST_ADDR    (FONT_TILES_DATA + (READYGO_BASE_TILE * FONT_TILE_FRAME_SIZE))

typedef struct {
    uint8_t cols;
    uint16_t row0_base; // absolute FONT_TILES_DATA tile index
    uint16_t row1_base;
} glyph_info_t;

// readygopt2: '10','9','8',...,'3', sequential/row-major within the asset
// (row 0 = first `cols` tiles, row 1 = the next `cols` tiles), offset by
// READYGO_BASE_TILE -- verified by decoding the shipped .bin and
// reconstructing each numeral.
static const glyph_info_t PT2_GLYPHS[8] = {
    {4, READYGO_BASE_TILE + 0,  READYGO_BASE_TILE + 4},   // "10"
    {3, READYGO_BASE_TILE + 8,  READYGO_BASE_TILE + 11},  // "9"
    {3, READYGO_BASE_TILE + 14, READYGO_BASE_TILE + 17},  // "8"
    {3, READYGO_BASE_TILE + 20, READYGO_BASE_TILE + 23},  // "7"
    {3, READYGO_BASE_TILE + 26, READYGO_BASE_TILE + 29},  // "6"
    {3, READYGO_BASE_TILE + 32, READYGO_BASE_TILE + 35},  // "5"
    {3, READYGO_BASE_TILE + 38, READYGO_BASE_TILE + 41},  // "4"
    {3, READYGO_BASE_TILE + 44, READYGO_BASE_TILE + 47},  // "3"
};

// readygopt3: '2','1','FINISH!', same sequential/row-major layout.
static const glyph_info_t PT3_GLYPH_2      = {3,  READYGO_BASE_TILE + 0,  READYGO_BASE_TILE + 3};
static const glyph_info_t PT3_GLYPH_1      = {3,  READYGO_BASE_TILE + 6,  READYGO_BASE_TILE + 9};
static const glyph_info_t PT3_GLYPH_FINISH = {16, READYGO_BASE_TILE + 12, READYGO_BASE_TILE + 28};

typedef enum { CD_ASSET_NONE, CD_ASSET_PT1, CD_ASSET_PT2, CD_ASSET_PT3 } cd_asset_t;

static bool s_active;
static bool s_finish_reached;
static bool s_done;
static uint8_t s_current_value; // 10..1 while counting down; unused once s_finish_reached
static uint16_t s_value_timer;
static cd_asset_t s_loaded_asset;

static void load_rom_asset_to_xram(const char *filename, unsigned dest, unsigned size) {
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return;
    read_xram(dest, size, fd);
    close(fd);
}

// Converge-then-flash, same idiom as update_ready_go_animation(): the two
// halves of the glyph slide in from outside the screen over the first 8
// frames, then the whole glyph blinks on/off for as long as it's shown.
static void draw_glyph(const glyph_info_t *g, uint16_t timer) {
    clear_ready_go_animation();

    bool visible = ((timer % CD_FLASH_PERIOD) < (CD_FLASH_PERIOD / 2));
    if (!visible) return;

    int16_t offset_x = 0;
    if (timer < CD_CONVERGE_FRAMES) {
        offset_x = (int16_t)((CD_CONVERGE_FRAMES - 1 - timer) * 2);
    }

    uint8_t left_col = (uint8_t)((TEXT_MAP_WIDTH - g->cols) / 2);
    uint8_t half = (uint8_t)(g->cols / 2);

    for (uint8_t row = 0; row < 2; row++) {
        uint8_t target_ty = (row == 0) ? CD_ROW0_TY : CD_ROW1_TY;
        uint16_t base_tile = (row == 0) ? g->row0_base : g->row1_base;

        for (uint8_t col = 0; col < g->cols; col++) {
            int16_t dest_tx = (int16_t)(left_col + col);
            dest_tx += (col < half) ? -offset_x : offset_x;
            if (dest_tx < 0 || dest_tx >= TEXT_MAP_WIDTH) continue;

            RIA.addr0 = TEXT_MAP_DATA + (target_ty * TEXT_MAP_WIDTH) + dest_tx;
            RIA.step0 = 1;
            RIA.rw0 = (uint8_t)(base_tile + col);
        }
    }
}

static void ensure_asset(cd_asset_t which) {
    if (s_loaded_asset == which) return;
    const char *filename;
    switch (which) {
        case CD_ASSET_PT1: filename = "ROM:readygopt1"; break;
        case CD_ASSET_PT2: filename = "ROM:readygopt2"; break;
        case CD_ASSET_PT3: filename = "ROM:readygopt3"; break;
        default: return;
    }
    load_rom_asset_to_xram(filename, READYGO_DEST_ADDR, READYGO_ASSET_SIZE);
    s_loaded_asset = which;
}

void countdown_reset(void) {
    s_active = false;
    s_finish_reached = false;
    s_done = false;
    s_current_value = 0xFF;
    s_value_timer = 0;
    // Not CD_ASSET_NONE -- READY/GO's own tiles are already sitting in
    // FONT_TILES_DATA from the last time they were shown (or from boot),
    // so there's nothing to load until the countdown actually needs pt2/pt3.
    s_loaded_asset = CD_ASSET_PT1;
}

bool countdown_should_freeze(void) {
    return s_finish_reached;
}

bool countdown_finished(void) {
    return s_done;
}

void countdown_update(void) {
    if (s_done) return;

    uint16_t remaining = get_game_remaining_frames();

    if (!s_active) {
        if (remaining == 0 || remaining > (CD_DIGIT_HOLD_FRAMES * 10)) return;
        s_active = true;
    }

    if (!s_finish_reached) {
        if (remaining == 0) {
            s_finish_reached = true;
            s_value_timer = 0;
            ensure_asset(CD_ASSET_PT3);
            music_stop();
            sfx_play("ROM:sfxghosteat", SFX_PRIORITY_TOP);
            draw_glyph(&PT3_GLYPH_FINISH, 0);
            return;
        }

        uint8_t value = (uint8_t)((remaining + CD_DIGIT_HOLD_FRAMES - 1) / CD_DIGIT_HOLD_FRAMES);
        if (value > 10) value = 10;

        if (value != s_current_value) {
            s_current_value = value;
            s_value_timer = 0;
            ensure_asset((value >= 3) ? CD_ASSET_PT2 : CD_ASSET_PT3);
        } else {
            s_value_timer++;
        }

        const glyph_info_t *g;
        if (value >= 3) {
            g = &PT2_GLYPHS[10 - value];
        } else if (value == 2) {
            g = &PT3_GLYPH_2;
        } else {
            g = &PT3_GLYPH_1;
        }
        draw_glyph(g, s_value_timer);
        return;
    }

    // FINISH! hold.
    s_value_timer++;
    draw_glyph(&PT3_GLYPH_FINISH, s_value_timer);
    if (s_value_timer >= CD_FINISH_HOLD_FRAMES) {
        // Restore READY/GO's own tiles before the next game needs them.
        ensure_asset(CD_ASSET_PT1);
        clear_ready_go_animation();
        s_done = true;
    }
}
