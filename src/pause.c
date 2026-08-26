#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "constants.h"
#include "pause.h"
#include "tile_mode2.h"
#include "opl.h"
#include "ghost.h" // start_game_retry()/resume_gameplay_from_pause()/start_warm_title_screen() -- defined in main.c

#define PAUSE_TITLE_ROW 4
#define PAUSE_CONTINUE_ROW 6
#define PAUSE_RETRY_ROW 8
#define PAUSE_EXIT_ROW 10

// 32-on/32-off, same cadence as the title screen's "PRESS START BUTTON" flash.
#define PAUSE_FLASH_PERIOD 64
#define PAUSE_FLASH_ON_FRAMES 32

typedef enum {
    PAUSE_SEL_CONTINUE = 0,
    PAUSE_SEL_RETRY = 1,
    PAUSE_SEL_EXIT = 2
} pause_selection_t;

static uint8_t s_selection;
static uint16_t s_flash_timer;

// Writes `text` centered on `row`, either compact or with a single space
// inserted between every letter (the "selected" look, e.g. "C O N T I N
// U E"). The full row is blanked first so switching from a wider spaced
// version down to a narrower compact one (or vice versa) never leaves
// stale letters behind.
static void draw_menu_item(uint8_t row, const char *text, bool selected) {
    char buf[20];
    uint8_t len = (uint8_t)strlen(text);
    uint8_t n = 0;

    for (uint8_t i = 0; i < len; i++) {
        buf[n++] = text[i];
        if (selected && i < len - 1) {
            buf[n++] = ' ';
        }
    }
    buf[n] = '\0';

    char blank[TEXT_MAP_WIDTH + 1];
    for (uint8_t i = 0; i < TEXT_MAP_WIDTH; i++) blank[i] = ' ';
    blank[TEXT_MAP_WIDTH] = '\0';
    write_text_to_text_map(0, row, blank);

    uint8_t start_col = (TEXT_MAP_WIDTH - n) / 2;
    write_text_to_text_map(start_col, row, buf);
}

static void draw_menu(void) {
    draw_menu_item(PAUSE_CONTINUE_ROW, "CONTINUE", s_selection == PAUSE_SEL_CONTINUE);
    draw_menu_item(PAUSE_RETRY_ROW, "RETRY", s_selection == PAUSE_SEL_RETRY);
    draw_menu_item(PAUSE_EXIT_ROW, "EXIT", s_selection == PAUSE_SEL_EXIT);
}

static void draw_pause_title(bool visible) {
    static const char *title = "PAUSE";
    uint8_t start_col = (TEXT_MAP_WIDTH - 5) / 2;
    write_text_to_text_map(start_col, PAUSE_TITLE_ROW, visible ? title : "     ");
}

static void clear_pause_text(void) {
    char blank[TEXT_MAP_WIDTH + 1];
    for (uint8_t i = 0; i < TEXT_MAP_WIDTH; i++) blank[i] = ' ';
    blank[TEXT_MAP_WIDTH] = '\0';
    write_text_to_text_map(0, PAUSE_TITLE_ROW, blank);
    write_text_to_text_map(0, PAUSE_CONTINUE_ROW, blank);
    write_text_to_text_map(0, PAUSE_RETRY_ROW, blank);
    write_text_to_text_map(0, PAUSE_EXIT_ROW, blank);
}

void pause_init(void) {
    s_selection = PAUSE_SEL_CONTINUE;
    s_flash_timer = 0;

    // Freeze everything audible in place -- music_pause()/sfx_pause()
    // silence their channels but keep each player's own position, so
    // CONTINUE picks back up exactly where it left off.
    music_pause();
    sfx_pause();

    draw_pause_title(true);
    draw_menu();
}

void pause_update(bool press_up, bool press_down, bool press_action) {
    s_flash_timer++;
    bool visible = ((s_flash_timer % PAUSE_FLASH_PERIOD) < PAUSE_FLASH_ON_FRAMES);
    draw_pause_title(visible);

    uint8_t prev_selection = s_selection;
    if (press_up) {
        s_selection = (s_selection == PAUSE_SEL_CONTINUE) ? PAUSE_SEL_EXIT : (uint8_t)(s_selection - 1);
    } else if (press_down) {
        s_selection = (s_selection == PAUSE_SEL_EXIT) ? PAUSE_SEL_CONTINUE : (uint8_t)(s_selection + 1);
    }
    if (s_selection != prev_selection) {
        draw_menu();
    }

    if (press_action) {
        clear_pause_text();
        switch (s_selection) {
            case PAUSE_SEL_CONTINUE:
                music_resume();
                sfx_resume();
                resume_gameplay_from_pause();
                break;
            case PAUSE_SEL_RETRY:
                start_game_retry();
                break;
            default: // PAUSE_SEL_EXIT
                start_warm_title_screen();
                break;
        }
    }
}
