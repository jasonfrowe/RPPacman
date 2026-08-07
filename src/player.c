#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "player.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"

void player_update_motion(const input_actions_t *actions) {

    if (actions->right) {
        player.dir = DIR_RIGHT;
    } else if (actions->left) {
        player.dir = DIR_LEFT;
    } else if (actions->down) {
        player.dir = DIR_DOWN;
    } else if (actions->up) {
        player.dir = DIR_UP;
    } else {
        player.dir = DIR_NONE; // Stop movement when no key is held
    }

    // Update player world coordinates based on direction
    switch (player.dir) {
        case DIR_RIGHT:
            player.world_px += 2;
            break;
        case DIR_LEFT:
            player.world_px -= 2;
            break;
        case DIR_DOWN:
            player.world_py += 2;
            break;
        case DIR_UP:
            player.world_py -= 2;
            break;
    }

    // Endless wrap Pac-Man in world space
    if (player.world_px < 0) {
        player.world_px += WORLD_WIDTH;
    } else if (player.world_px >= WORLD_WIDTH) {
        player.world_px -= WORLD_WIDTH;
    }

    if (player.world_py < 0) {
        player.world_py += WORLD_HEIGHT;
    } else if (player.world_py >= WORLD_HEIGHT) {
        player.world_py -= WORLD_HEIGHT;
    }

    // Pac-Man screen position stays fixed at screen center (X) and tracks world Y (Y)
    // player.x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    player.y_pos_px = player.world_py;

    // Endless scrolling offset for the maze tilemap
    maze_dx = player.x_pos_px - player.world_px;

    // Update hardware registers
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);

    // Update Ghost screen positions relative to endless maze scroll
    for (int i = 0; i < NGHOSTS; i++) {
        if (ghosts[i].world_px < 0) {
            ghosts[i].world_px += WORLD_WIDTH;
        } else if (ghosts[i].world_px >= WORLD_WIDTH) {
            ghosts[i].world_px -= WORLD_WIDTH;
        }

        ghosts[i].x_pos_px = ghosts[i].world_px + maze_dx;
        ghosts[i].y_pos_px = ghosts[i].world_py;

        uint16_t current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, ghosts[i].x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, ghosts[i].y_pos_px);
    }
}