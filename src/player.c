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

    maze_d1 = 0; // Reset maze_dx change for this frame
    // Update player position based on direction
    switch (player.dir) {
        case DIR_RIGHT:
            player.x_pos_px += 2; // Move right
            maze_dx -= 2; // Move maze left to simulate player moving right
            maze_d1 = 2; // Update maze_dx to reflect the change in position
            break;
        case DIR_LEFT:
            player.x_pos_px -= 2; // Move left
            maze_dx += 2; // Move maze right to simulate player moving left
            maze_d1 = -2; // Update maze_dx to reflect the change in position
            break;
        case DIR_DOWN:
            player.y_pos_px += 2; // Move down
            break;
        case DIR_UP:
            player.y_pos_px -= 2; // Move up
            break;
    }

    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);


}