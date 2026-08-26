#ifndef RESULTS_H
#define RESULTS_H

#include <stdbool.h>

// Called once by start_results_screen() (src/main.c) to reset the
// results-screen state machine before the first results_update() tick.
void results_init(void);

// Drives the results-screen state machine one real frame at a time.
// press_start/press_action are the same edge-triggered presses as
// everywhere else in main.c's loop; only consulted while waiting to
// continue past the results screen -- either one advances it.
void results_update(bool press_start, bool press_action);

// Clears every tile in TEXT_MAP_DATA to blank. Exposed for congrats.c: the
// results screen's own score-totals digits (drawn on this same layer,
// rows 3-4) are still sitting there when a qualifying score hands off to
// the congrats screen instead of returning to the title, and would
// otherwise bleed through on top of it.
void blank_text_map_all(void);

// Reloads the original title map/tiles ROM assets into TITLE_MAP_DATA/
// TITLE_TILES_DATA, restores the row 0/13 chrome, and hands off to
// start_warm_title_screen(). Shared by results.c's own return-to-title
// path and congrats.c's rankings screen, since both leave TITLE_MAP_DATA
// holding a different asset (resultsmap/rankingsmap) that needs restoring
// the same way.
void return_to_title_from_post_game(void);

#endif // RESULTS_H
