#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "player.h"
#include "sprite_mode5.h"

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

    // Update player position based on direction
    switch (player.dir) {
        case DIR_RIGHT:
            player.x_pos_px += 2; // Move right
            break;
        case DIR_LEFT:
            player.x_pos_px -= 2; // Move left
            break;
        case DIR_DOWN:
            player.y_pos_px += 2; // Move down
            break;
        case DIR_UP:
            player.y_pos_px -= 2; // Move up
            break;
    }

    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);


}