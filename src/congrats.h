#ifndef CONGRATS_H
#define CONGRATS_H

#include <stdint.h>
#include <stdbool.h>

// Called by start_congrats_screen() (src/main.c) to reset the congrats
// screen's state machine before the first congrats_update() tick. `rank`
// is 0-based (0 = 1st place), from hiscores_find_rank().
void congrats_init(int8_t rank);

// Drives the congrats screen (rank/score banner + 3-letter initials
// entry) one real frame at a time. press_up/press_down cycle the letter
// at the current cursor position; press_action confirms it and advances
// to the next one (or, on the 3rd, finishes entry and moves on to the
// rankings screen).
void congrats_update(bool press_up, bool press_down, bool press_action);

// Called by start_rankings_screen() (src/main.c) to reset the rankings
// screen's state machine before the first rankings_update() tick.
void rankings_init(void);

// Drives the rankings (top-10 table) screen one real frame at a time.
// press_confirm is edge-triggered start-or-action; only consulted while
// waiting to return to the title screen.
void rankings_update(bool press_confirm);

#endif // CONGRATS_H
