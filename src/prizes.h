#ifndef PRIZES_H
#define PRIZES_H

#include <stdint.h>
#include <stdbool.h>

extern uint8_t left_side_level;
extern uint8_t right_side_level;
extern uint8_t left_prize_count;
extern uint8_t right_prize_count;

extern void prize_update_motion(void);
extern void check_and_eat_prize(int16_t drawn_world_x, int16_t drawn_world_y);
extern void update_side_pellets_status(void);
extern void update_maze_munchers_animation(void);
extern void init_side_pellet_counters(void);
extern void reset_prizes_and_mazes_level(void);
extern void on_pellet_eaten(uint16_t tile_x);

#endif // PRIZES_H