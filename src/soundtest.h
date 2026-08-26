#ifndef SOUNDTEST_H
#define SOUNDTEST_H

#include <stdbool.h>

// Called by start_soundtest_screen() (src/main.c) to reset the sound
// test screen's state machine before the first soundtest_update() tick.
void soundtest_init(void);

// Drives the sound test screen one real frame at a time. Up/down move
// the Pac-Man cursor between OPL2/SE/EXIT; while on OPL2 or SE,
// left/right cycle the 00-07 track index (shown at tile (22,5)-(23,5)).
// press_action ('A') starts playback on OPL2/SE, or confirms EXIT;
// press_fire ('B') stops playback on OPL2/SE.
void soundtest_update(bool press_up, bool press_down, bool press_left, bool press_right, bool press_action, bool press_fire);

#endif // SOUNDTEST_H
