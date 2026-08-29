#ifndef HISCORES_H
#define HISCORES_H

#include <stdint.h>
#include "player.h" // game_mode_t

#define HISCORE_COUNT 10

// Loads the persisted top-10 tables from disk ("RPPacMan.hiscores", a bare
// filename resolving to the default drive -- same convention as
// RPMegaFighter's own high-score file). Call exactly once, at boot,
// before anything reads hiscores_get_score()/hiscores_get_initials().
// If the file doesn't exist yet, is short/corrupt, or is still the older
// single-table format, initializes all entries in both tables to score 0
// / initials "AAA" instead.
void hiscores_load(void);

// NORMAL and EXTRA each keep their own independent top-10 table -- pass
// the mode whose table you mean. rank is 0-based (0 = 1st place .. 9 =
// 10th place). Out-of-range rank returns 0 / "AAA" rather than faulting.
uint32_t hiscores_get_score(game_mode_t mode, uint8_t rank);
const char *hiscores_get_initials(game_mode_t mode, uint8_t rank); // 3 chars, not null-terminated

// Returns the 0-based rank `score` would occupy (0 = 1st place) in
// `mode`'s table if it's strictly greater than that rank's current score,
// or -1 if it doesn't beat 10th place at all. Ties do not displace an
// existing entry.
int8_t hiscores_find_rank(game_mode_t mode, uint32_t score);

// Shifts entries at `rank` and below down by one (dropping 10th place) in
// `mode`'s table, inserts the new entry at `rank`, and immediately
// persists both tables to disk. `initials` need not be null-terminated --
// exactly 3 bytes are read.
void hiscores_insert(game_mode_t mode, int8_t rank, uint32_t score, const char *initials);

#endif // HISCORES_H
