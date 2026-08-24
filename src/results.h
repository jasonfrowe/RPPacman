#ifndef RESULTS_H
#define RESULTS_H

#include <stdbool.h>

// Called once by start_results_screen() (src/main.c) to reset the
// results-screen state machine before the first results_update() tick.
void results_init(void);

// Drives the results-screen state machine one real frame at a time.
// press_start is the same edge-triggered press as everywhere else in
// main.c's loop; only consulted while waiting to return to the title.
void results_update(bool press_start);

#endif // RESULTS_H
