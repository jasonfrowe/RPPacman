#ifndef GHOST_H
#define GHOST_H

void ghost_update_motion(void);
void trigger_power_pellet_frightened(void);
void check_pacman_ghost_collisions(void);
void check_and_reset_stuck_ghosts(void);
bool is_eat_pause_active(void);
void trigger_eaten_ghost_animation(uint8_t ghost_index, uint32_t pts);

#endif // GHOST_H