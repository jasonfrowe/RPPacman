#ifndef GHOST_H
#define GHOST_H

void ghost_update_motion(void);
void trigger_power_pellet_frightened(void);
void check_pacman_ghost_collisions(void);
void check_and_reset_stuck_ghosts(void);
bool is_eat_pause_active(void);
bool is_death_sequence_active(void);
void trigger_eaten_ghost_animation(uint8_t ghost_index, uint32_t pts);
void trigger_prize_score_animation(uint8_t prize_index, uint16_t pts);
void reset_ghosts_to_initial_positions(void);
void start_title_screen(void);
void start_normal_game(void);

#endif // GHOST_H