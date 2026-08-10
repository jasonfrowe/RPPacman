#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>
#include "input.h"

// Movement Directions
#define DIR_NONE  0
#define DIR_LEFT  1
#define DIR_RIGHT 2
#define DIR_UP    3
#define DIR_DOWN  4

extern const uint16_t SPEED_TABLE[22];
uint8_t get_speed_level_index(void);

void push_score_popup(uint16_t tile_x, uint16_t tile_y, uint8_t score_tile);
void update_player_score_display(uint32_t score);
void add_player_score(uint32_t pts);
void player_update_motion(const input_actions_t *actions);
bool is_wall_tile(int16_t world_x, int16_t world_y);
void get_dir_offset(int8_t dir, int8_t *dx, int8_t *dy);
bool can_step_dir(int16_t world_x, int16_t world_y, int8_t dir);
void reset_player_on_death(void);

#endif // PLAYER_H
