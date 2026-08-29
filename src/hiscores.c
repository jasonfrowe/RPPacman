#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include "hiscores.h"

// Bare filename (no drive prefix) -- resolves against whatever the
// default drive is, same convention RPMegaFighter's own high-score file
// uses ("HIGHSCOR.DAT"). Deliberately NOT under "ROM:" -- that prefix is
// reserved for read-only assets baked into the build (see
// rp6502_asset() in CMakeLists.txt); this needs to be written back.
#define HISCORE_FILE "RPPacMan.hiscores"

typedef struct {
    uint32_t score;
    char initials[3];
} hiscore_entry_t;

// Indexed [game_mode_t][rank] -- NORMAL and EXTRA are two entirely
// separate top-10 tables, saved/loaded together as one file.
static hiscore_entry_t s_hiscores[2][HISCORE_COUNT];

static void hiscores_set_defaults(void) {
    for (uint8_t m = 0; m < 2; m++) {
        for (uint8_t i = 0; i < HISCORE_COUNT; i++) {
            s_hiscores[m][i].score = 0;
            s_hiscores[m][i].initials[0] = 'A';
            s_hiscores[m][i].initials[1] = 'A';
            s_hiscores[m][i].initials[2] = 'A';
        }
    }
}

static void hiscores_save(void) {
    int fd = open(HISCORE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write(fd, s_hiscores, sizeof(s_hiscores));
    close(fd);
}

void hiscores_load(void) {
    int fd = open(HISCORE_FILE, O_RDONLY);
    if (fd < 0) {
        hiscores_set_defaults();
        return;
    }
    int n = read(fd, s_hiscores, sizeof(s_hiscores));
    close(fd);
    if (n != (int)sizeof(s_hiscores)) {
        // Missing/short/corrupt file (e.g. first-ever run, or an older
        // single-table save from before EXTRA mode) -- start clean rather
        // than trust a partial read or misinterpret the old format.
        hiscores_set_defaults();
    }
}

uint32_t hiscores_get_score(game_mode_t mode, uint8_t rank) {
    if (rank >= HISCORE_COUNT) return 0;
    return s_hiscores[mode][rank].score;
}

const char *hiscores_get_initials(game_mode_t mode, uint8_t rank) {
    static const char fallback[3] = { 'A', 'A', 'A' };
    if (rank >= HISCORE_COUNT) return fallback;
    return s_hiscores[mode][rank].initials;
}

int8_t hiscores_find_rank(game_mode_t mode, uint32_t score) {
    for (uint8_t i = 0; i < HISCORE_COUNT; i++) {
        if (score > s_hiscores[mode][i].score) return (int8_t)i;
    }
    return -1;
}

void hiscores_insert(game_mode_t mode, int8_t rank, uint32_t score, const char *initials) {
    if (rank < 0 || rank >= HISCORE_COUNT) return;

    for (int8_t i = HISCORE_COUNT - 1; i > rank; i--) {
        s_hiscores[mode][i] = s_hiscores[mode][i - 1];
    }
    s_hiscores[mode][rank].score = score;
    s_hiscores[mode][rank].initials[0] = initials[0];
    s_hiscores[mode][rank].initials[1] = initials[1];
    s_hiscores[mode][rank].initials[2] = initials[2];

    hiscores_save();
}
