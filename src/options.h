#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdbool.h>

// Called by start_options_screen() (src/main.c) to reset the options
// screen's state machine before the first options_update() tick.
void options_init(void);

// Drives the options screen one real frame at a time: fades from the
// title menu into the options background/tiles (pacman07 music), then
// up/down move the Pac-Man cursor between the 6 items and press_action
// selects the highlighted one.
void options_update(bool press_up, bool press_down, bool press_action);

#endif // OPTIONS_H
