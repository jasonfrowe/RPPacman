#ifndef PAUSE_H
#define PAUSE_H

#include <stdbool.h>

// Called once, right when the player presses start during gameplay, to
// draw the pause banner/menu and freeze music/sfx in place.
void pause_init(void);

// Drives the pause screen one real frame at a time: flashes "PAUSE",
// handles up/down navigation between CONTINUE/RETRY/EXIT (spacing out
// the letters of whichever one is currently selected), and acts on
// press_action.
void pause_update(bool press_up, bool press_down, bool press_action);

#endif // PAUSE_H
