#ifndef PRIZES_H
#define PRIZES_H

#include <stdint.h>
#include <stdbool.h>

extern uint8_t left_side_level;
extern uint8_t right_side_level;
extern uint8_t left_prize_count;
extern uint8_t right_prize_count;

extern void prize_update_motion(void);
extern void check_and_eat_prize(uint16_t tile_x, uint16_t tile_y);
extern void update_side_pellets_status(void);
extern void update_maze_munchers_animation(void);

#endif // PRIZES_H