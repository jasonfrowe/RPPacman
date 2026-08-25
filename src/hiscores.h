#ifndef HISCORES_H
#define HISCORES_H

#include <stdint.h>

#define HISCORE_COUNT 10

// Loads the persisted top-10 table from disk ("RPPacMan.hiscores", a bare
// filename resolving to the default drive -- same convention as
// RPMegaFighter's own high-score file). Call exactly once, at boot,
// before anything reads hiscores_get_score()/hiscores_get_initials().
// If the file doesn't exist yet or is short/corrupt, initializes all 10
// entries to score 0 / initials "AAA" instead.
void hiscores_load(void);

// rank is 0-based (0 = 1st place .. 9 = 10th place). Out-of-range rank
// returns 0 / "AAA" rather than faulting.
uint32_t hiscores_get_score(uint8_t rank);
const char *hiscores_get_initials(uint8_t rank); // 3 chars, not null-terminated

// Returns the 0-based rank `score` would occupy (0 = 1st place) if it's
// strictly greater than that rank's current score, or -1 if it doesn't
// beat 10th place at all. Ties do not displace an existing entry.
int8_t hiscores_find_rank(uint32_t score);

// Shifts entries at `rank` and below down by one (dropping 10th place),
// inserts the new entry at `rank`, and immediately persists the table to
// disk. `initials` need not be null-terminated -- exactly 3 bytes are
// read.
void hiscores_insert(int8_t rank, uint32_t score, const char *initials);

#endif // HISCORES_H
