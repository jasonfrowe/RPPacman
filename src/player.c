#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "player.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"

static int8_t queued_dir = DIR_NONE;

static bool is_wall_tile(int16_t world_x, int16_t world_y) {
    while (world_x < 0) world_x += WORLD_WIDTH;
    while (world_x >= WORLD_WIDTH) world_x -= WORLD_WIDTH;
    while (world_y < 0) world_y += WORLD_HEIGHT;
    while (world_y >= WORLD_HEIGHT) world_y -= WORLD_HEIGHT;

    uint16_t tile_x = (uint16_t)(world_x / MAZE_TILES_SIZE_PX);
    uint16_t tile_y = (uint16_t)(world_y / MAZE_TILES_SIZE_PX);

    if (tile_x >= MAZE_MAP_WIDTH) tile_x %= MAZE_MAP_WIDTH;
    if (tile_y >= MAZE_MAP_HEIGHT) tile_y %= MAZE_MAP_HEIGHT;

    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;

    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    uint8_t tile_index = RIA.rw0;

    return (tile_index > 0 && tile_index < 116);
}

static void get_dir_offset(int8_t dir, int8_t *dx, int8_t *dy) {
    *dx = 0;
    *dy = 0;
    switch (dir) {
        case DIR_RIGHT: *dx = 1;  break;
        case DIR_LEFT:  *dx = -1; break;
        case DIR_DOWN:  *dy = 1;  break;
        case DIR_UP:    *dy = -1; break;
    }
}

static bool can_step_dir(int16_t world_x, int16_t world_y, int8_t dir) {
    if (dir == DIR_NONE) return false;
    int8_t dx, dy;
    get_dir_offset(dir, &dx, &dy);
    return !is_wall_tile(world_x + dx * 8, world_y + dy * 8);
}

void player_update_motion(const input_actions_t *actions) {

    // 1. Buffer user direction input
    if (actions->right) {
        queued_dir = DIR_RIGHT;
    } else if (actions->left) {
        queued_dir = DIR_LEFT;
    } else if (actions->down) {
        queued_dir = DIR_DOWN;
    } else if (actions->up) {
        queued_dir = DIR_UP;
    }

    // 2. Process direction change
    if (queued_dir != DIR_NONE) {
        // Immediate 180-degree reversal is always allowed
        if ((player.dir == DIR_RIGHT && queued_dir == DIR_LEFT) ||
            (player.dir == DIR_LEFT && queued_dir == DIR_RIGHT) ||
            (player.dir == DIR_UP && queued_dir == DIR_DOWN) ||
            (player.dir == DIR_DOWN && queued_dir == DIR_UP)) {
            player.dir = queued_dir;
        } else {
            // Turning or starting requires Pac-Man to be aligned on the 8px tile grid in both X and Y
            bool at_intersection = (player.world_px % MAZE_TILES_SIZE_PX == 0) && 
                                   (player.world_py % MAZE_TILES_SIZE_PX == 0);

            if (at_intersection && can_step_dir(player.world_px, player.world_py, queued_dir)) {
                player.dir = queued_dir;
            }
        }
    }

    // 3. Continuous arcade movement (1px step per frame)
    if (player.dir != DIR_NONE) {
        int8_t dx, dy;
        get_dir_offset(player.dir, &dx, &dy);

        int16_t next_px = player.world_px + dx;
        int16_t next_py = player.world_py + dy;

        // Check if step forward is blocked by a wall
        bool is_blocked = false;
        if (player.world_px % MAZE_TILES_SIZE_PX == 0 && player.world_py % MAZE_TILES_SIZE_PX == 0) {
            // At tile boundary: check next full tile in direction of motion
            is_blocked = !can_step_dir(player.world_px, player.world_py, player.dir);
        }

        if (!is_blocked) {
            player.world_px = next_px;
            player.world_py = next_py;
        } else {
            player.dir = DIR_NONE; // Stop at wall intersection
        }
    }

    // 4. Endless wrap Pac-Man in world space
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

#define VISUAL_X_OFFSET (-3) // Visual horizontal draw offset (-3px)
#define VISUAL_Y_OFFSET (-3) // Visual vertical draw offset (-3px)

    // Screen Y tracks world Y with visual offset
    player.y_pos_px = player.world_py + VISUAL_Y_OFFSET;

    // Endless scrolling offset for the maze tilemap
    maze_dx = player.x_pos_px - player.world_px;

    int16_t visual_x = player.x_pos_px + VISUAL_X_OFFSET;

    // Update hardware registers
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, visual_x);
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