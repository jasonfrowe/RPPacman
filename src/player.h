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

// Score source categories, tracked separately for the results screen's
// grand totals and its 10-second-interval history buckets.
#define SCORE_CAT_PELLET 0
#define SCORE_CAT_PRIZE  1
#define SCORE_CAT_GHOST  2
#define SCORE_CAT_COUNT  3

#define SCORE_HISTORY_BUCKETS 30 // 10s each, 0:00-5:00 (game's own 5:00 cap)

void push_score_popup(uint16_t tile_x, uint16_t tile_y, uint8_t score_tile);
void reset_score_popups(void);
void update_player_score_display(uint32_t score);
void add_player_score(uint32_t pts, uint8_t category);
void reset_score_history(void);
void reset_extra_life_threshold(void);
uint16_t get_score_history_bucket(uint8_t bucket, uint8_t category);
void player_update_motion(const input_actions_t *actions);
bool is_wall_tile(int16_t world_x, int16_t world_y);
void get_dir_offset(int8_t dir, int8_t *dx, int8_t *dy);
bool can_step_dir(int16_t world_x, int16_t world_y, int8_t dir);
void reset_player_on_death(void);
void clear_player_queued_dir(void);

#endif // PLAYER_H
