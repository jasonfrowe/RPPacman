#ifndef COUNTDOWN_H
#define COUNTDOWN_H

#include <stdbool.h>

// Call once per new game (alongside reset_game_timer()) to clear any
// state left over from a previous game.
void countdown_reset(void);

// Call every frame during STATE_GAMEPLAY, after update_game_timer_display().
// No-ops until 10 seconds remain, then drives the 10..1/FINISH! overlay on
// the title-map layer (borrowed from the title/options/results screens --
// it's pushed off-screen during normal gameplay) purely as a function of
// get_game_remaining_frames().
void countdown_update(void);

// True once the FINISH! phase has begun -- the caller should stop normal
// player/ghost/prize motion updates while this holds.
bool countdown_should_freeze(void);

// True once the 3-second FINISH! hold has completed and the title-map
// layer has been restored to its normal 8x8-tile mode -- the caller
// should start_results_screen() and stop calling countdown_update().
bool countdown_finished(void);

#endif // COUNTDOWN_H
