#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "player.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"
#include "prizes.h"
#include "ghost.h"

static int8_t queued_dir = DIR_NONE;

bool is_wall_tile(int16_t world_x, int16_t world_y) {
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

void get_dir_offset(int8_t dir, int8_t *dx, int8_t *dy) {
    *dx = 0;
    *dy = 0;
    switch (dir) {
        case DIR_RIGHT: *dx = 1;  break;
        case DIR_LEFT:  *dx = -1; break;
        case DIR_DOWN:  *dy = 1;  break;
        case DIR_UP:    *dy = -1; break;
    }
}

bool can_step_dir(int16_t world_x, int16_t world_y, int8_t dir) {
    if (dir == DIR_NONE) return false;
    int8_t dx, dy;
    get_dir_offset(dir, &dx, &dy);
    return !is_wall_tile(world_x + dx * 8, world_y + dy * 8);
}

#define MAX_SCORE_POPUPS 10
static eaten_tile_popup_t s_score_popups[MAX_SCORE_POPUPS];
static uint8_t s_popup_count = 0;

static uint8_t get_score_tile_index(uint32_t pts) {
    switch (pts) {
        case 10: return 120;
        case 20: return 121;
        case 30: return 122;
        case 40: return 123;
        case 50: return 124;
        default: return 120;
    }
}

static uint32_t get_current_dot_value(uint16_t dots_eaten) {
    if (dots_eaten < 60)   return 10;
    if (dots_eaten < 120)  return 20;
    if (dots_eaten < 180)  return 30;
    if (dots_eaten < 240)  return 40;
    return 50;
}

void reset_player_on_death(void) {
    player.pellets_eaten = 0; // Reset dot multiplier back to 10 points (0-59 dots tier)
}

void push_score_popup(uint16_t tile_x, uint16_t tile_y, uint8_t score_tile) {
    // If popups queue is full (10 items), expire the oldest entry immediately to blank (tile 0)
    if (s_popup_count >= MAX_SCORE_POPUPS) {
        uint16_t old_offset = s_score_popups[0].tile_y * MAZE_MAP_WIDTH + s_score_popups[0].tile_x;
        RIA.addr0 = MAZE_MAP_DATA + old_offset;
        RIA.step0 = 1;
        RIA.rw0 = 0;

        for (uint8_t i = 0; i < MAX_SCORE_POPUPS - 1; i++) {
            s_score_popups[i] = s_score_popups[i + 1];
        }
        s_popup_count--;
    }

    // Add new popup to array
    s_score_popups[s_popup_count].tile_x = tile_x;
    s_score_popups[s_popup_count].tile_y = tile_y;
    s_score_popups[s_popup_count].timer = 40; // 40 frames countdown
    s_score_popups[s_popup_count].active = true;
    s_popup_count++;

    // Write score tile index (120..124) into XRAM tile map
    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;
    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    RIA.rw0 = score_tile;
}

static void update_score_popups(void) {
    for (int8_t i = 0; i < (int8_t)s_popup_count; i++) {
        if (!s_score_popups[i].active) continue;

        if (s_score_popups[i].timer > 0) {
            s_score_popups[i].timer--;
        }

        if (s_score_popups[i].timer == 0) {
            // Revert tile back to 0 (blank)
            uint16_t offset = s_score_popups[i].tile_y * MAZE_MAP_WIDTH + s_score_popups[i].tile_x;
            RIA.addr0 = MAZE_MAP_DATA + offset;
            RIA.step0 = 1;
            RIA.rw0 = 0;

            // Remove item from popup list
            for (uint8_t j = (uint8_t)i; j < s_popup_count - 1; j++) {
                s_score_popups[j] = s_score_popups[j + 1];
            }
            s_popup_count--;
            i--; // Adjust loop index after shift
        }
    }
}

static uint32_t s_next_extra_life_threshold = 20000;

void add_player_score(uint32_t pts) {
    player.score += pts;
    update_player_score_display(player.score);

    while (player.score >= s_next_extra_life_threshold) {
        player.lives++;
        update_player_lives_display(player.lives);
        trigger_extra_life_blink();
        s_next_extra_life_threshold += 20000;
    }
}

static void check_and_eat_pellet(int16_t world_x, int16_t world_y) {
    // Check center of visual sprite (drawn_x + 8, drawn_y + 8) => (world_x + 5, world_y + 5)
    int16_t check_x = world_x + 5;
    int16_t check_y = world_y + 5;

    if (check_x < 0) check_x += WORLD_WIDTH;
    else if (check_x >= WORLD_WIDTH) check_x -= WORLD_WIDTH;
    if (check_y < 0) check_y += WORLD_HEIGHT;
    else if (check_y >= WORLD_HEIGHT) check_y -= WORLD_HEIGHT;

    // Use fast 16-bit shift instead of 32-bit division (/ 8 -> >> 3)
    uint16_t tile_x = (uint16_t)check_x >> 3;
    uint16_t tile_y = (uint16_t)check_y >> 3;

    if (tile_x >= MAZE_MAP_WIDTH) tile_x %= MAZE_MAP_WIDTH;
    if (tile_y >= MAZE_MAP_HEIGHT) tile_y %= MAZE_MAP_HEIGHT;

    uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;

    RIA.addr0 = MAZE_MAP_DATA + offset;
    RIA.step0 = 1;
    uint8_t tile_index = RIA.rw0;

    // Check if tile is a dot (116) or power pellet (117)
    if (tile_index == 116 || tile_index == 117) {
        // Clear pellet tile in XRAM map memory to 0 (blank tile)
        RIA.addr0 = MAZE_MAP_DATA + offset;
        RIA.step0 = 1;
        RIA.rw0 = 0;

        uint32_t dot_pts = get_current_dot_value(player.pellets_eaten);
        player.pellets_eaten++;
        add_player_score(dot_pts);

        uint8_t score_tile = get_score_tile_index(dot_pts);
        push_score_popup(tile_x, tile_y, score_tile);

        // If a Power Pellet / Super Pellet (117) was eaten, trigger Frightened mode
        if (tile_index == 117) {
            trigger_power_pellet_frightened();
        }

        // Update side pellet counter and check if clearing this pellet triggered prize spawning
        on_pellet_eaten(tile_x);
        update_side_pellets_status();
    }

    // Check if Pacman is consuming an active prize using visual drawn coordinates
    int16_t drawn_x = world_x + VISUAL_X_OFFSET;
    int16_t drawn_y = world_y + VISUAL_Y_OFFSET;
    check_and_eat_prize(drawn_x, drawn_y);
}

// 8.8 Fixed-Point Speed Lookup Table across 22 Prize Levels (Cherry to Crown)
// 0x0100 = 1.00 px/frame (Cherry) -> 0x0280 = 2.50 px/frame (Crown)
const uint16_t SPEED_TABLE[22] = {
    0x0100, // Level 0  (Cherry):            1.000 px/frame
    0x0112, // Level 1  (Strawberry):        1.071 px/frame
    0x0124, // Level 2  (Orange):            1.142 px/frame
    0x0136, // Level 3  (Apple):             1.214 px/frame
    0x0149, // Level 4  (Melon):             1.285 px/frame
    0x015B, // Level 5  (Banana):            1.357 px/frame
    0x016D, // Level 6  (Peach):             1.428 px/frame
    0x0180, // Level 7  (Galaxian Boss):     1.500 px/frame
    0x0192, // Level 8  (Bell):              1.571 px/frame
    0x01A4, // Level 9  (Key):               1.642 px/frame
    0x01B6, // Level 10 (Coffee):            1.714 px/frame
    0x01C9, // Level 11 (Cake):              1.785 px/frame
    0x01DB, // Level 12 (Galaga):            1.857 px/frame
    0x01ED, // Level 13 (Gaplus Drone):      1.928 px/frame
    0x0200, // Level 14 (Hamburger):         2.000 px/frame
    0x0212, // Level 15 (Fried Egg):         2.071 px/frame
    0x0224, // Level 16 (Candy):             2.142 px/frame
    0x0236, // Level 17 (Four-Leaf Clover):  2.214 px/frame
    0x0249, // Level 18 (Diamond):           2.285 px/frame
    0x025B, // Level 19 (Heart):             2.357 px/frame
    0x026D, // Level 20 (Samurai Helmet):    2.428 px/frame
    0x0280, // Level 21 (Crown):             2.500 px/frame (Max Cap)
};

static uint16_t s_speed_subpixel_x = 0;
static uint16_t s_speed_subpixel_y = 0;

uint8_t get_speed_level_index(void) {
    uint8_t max_count = (left_prize_count > right_prize_count) ? left_prize_count : right_prize_count;
    if (max_count > 21) max_count = 21;
    return max_count;
}

static uint8_t s_prev_dpad_mask = 0;

void clear_player_queued_dir(void) {
    queued_dir = DIR_NONE;
    s_prev_dpad_mask = 0;
}

void player_update_motion(const input_actions_t *actions) {
    if (is_eat_pause_active() || is_death_sequence_active()) {
        return;
    }

    // 1. Process D-pad input transitions
    uint8_t dpad_mask = 0;
    if (actions->up)    dpad_mask |= 1;
    if (actions->down)  dpad_mask |= 2;
    if (actions->left)  dpad_mask |= 4;
    if (actions->right) dpad_mask |= 8;

    // Only evaluate and queue a new direction when the D-pad button combination CHANGES
    if (dpad_mask != s_prev_dpad_mask) {
        uint8_t newly_pressed = dpad_mask & ~s_prev_dpad_mask;
        s_prev_dpad_mask = dpad_mask;

        int8_t new_dir = DIR_NONE;

        if (newly_pressed != 0) {
            // A new button (or diagonal combination) was newly pressed
            if (player.dir == DIR_LEFT || player.dir == DIR_RIGHT) {
                // Moving horizontally: prioritize newly pressed perpendicular (vertical) direction
                if (newly_pressed & 1)      new_dir = DIR_UP;
                else if (newly_pressed & 2) new_dir = DIR_DOWN;
                else if (newly_pressed & 4) new_dir = DIR_LEFT;
                else if (newly_pressed & 8) new_dir = DIR_RIGHT;
            } else if (player.dir == DIR_UP || player.dir == DIR_DOWN) {
                // Moving vertically: prioritize newly pressed perpendicular (horizontal) direction
                if (newly_pressed & 4)      new_dir = DIR_LEFT;
                else if (newly_pressed & 8) new_dir = DIR_RIGHT;
                else if (newly_pressed & 1) new_dir = DIR_UP;
                else if (newly_pressed & 2) new_dir = DIR_DOWN;
            } else {
                // Stationary: pick any newly pressed direction
                if (newly_pressed & 1)      new_dir = DIR_UP;
                else if (newly_pressed & 2) new_dir = DIR_DOWN;
                else if (newly_pressed & 4) new_dir = DIR_LEFT;
                else if (newly_pressed & 8) new_dir = DIR_RIGHT;
            }
        } else if (dpad_mask != 0) {
            // A button was released, but other D-pad buttons remain held
            if (player.dir == DIR_LEFT || player.dir == DIR_RIGHT) {
                if (dpad_mask & 1)      new_dir = DIR_UP;
                else if (dpad_mask & 2) new_dir = DIR_DOWN;
                else if (dpad_mask & 4) new_dir = DIR_LEFT;
                else if (dpad_mask & 8) new_dir = DIR_RIGHT;
            } else if (player.dir == DIR_UP || player.dir == DIR_DOWN) {
                if (dpad_mask & 4)      new_dir = DIR_LEFT;
                else if (dpad_mask & 8) new_dir = DIR_RIGHT;
                else if (dpad_mask & 1) new_dir = DIR_UP;
                else if (dpad_mask & 2) new_dir = DIR_DOWN;
            } else {
                if (dpad_mask & 1)      new_dir = DIR_UP;
                else if (dpad_mask & 2) new_dir = DIR_DOWN;
                else if (dpad_mask & 4) new_dir = DIR_LEFT;
                else if (dpad_mask & 8) new_dir = DIR_RIGHT;
            }
        }

        if (new_dir != DIR_NONE && new_dir != player.dir) {
            queued_dir = new_dir;
        }
    }

    // If Pac-Man is stationary against a wall, check if any currently held D-pad direction is open
    if (player.dir == DIR_NONE && dpad_mask != 0) {
        if ((dpad_mask & 1) && can_step_dir(player.world_px, player.world_py, DIR_UP)) {
            player.dir = DIR_UP;
            queued_dir = DIR_NONE;
        } else if ((dpad_mask & 2) && can_step_dir(player.world_px, player.world_py, DIR_DOWN)) {
            player.dir = DIR_DOWN;
            queued_dir = DIR_NONE;
        } else if ((dpad_mask & 4) && can_step_dir(player.world_px, player.world_py, DIR_LEFT)) {
            player.dir = DIR_LEFT;
            queued_dir = DIR_NONE;
        } else if ((dpad_mask & 8) && can_step_dir(player.world_px, player.world_py, DIR_RIGHT)) {
            player.dir = DIR_RIGHT;
            queued_dir = DIR_NONE;
        }
    }

    // 2. Process direction change
    if (queued_dir != DIR_NONE) {
        int16_t current_drawn_y = player.world_py + VISUAL_Y_OFFSET;

        // Ignore left/right inputs when Pac-Man is in vertical tunnel regions (< 32px or > 215px)
        bool is_in_vertical_tunnel = (current_drawn_y < 32) || ((current_drawn_y + SPRITE_SIZE_PX) > 215);
        if (is_in_vertical_tunnel && (queued_dir == DIR_LEFT || queued_dir == DIR_RIGHT)) {
            queued_dir = DIR_NONE;
        }

        if (queued_dir != DIR_NONE) {
            // Immediate 180-degree reversal is always allowed
            if ((player.dir == DIR_RIGHT && queued_dir == DIR_LEFT) ||
                (player.dir == DIR_LEFT && queued_dir == DIR_RIGHT) ||
                (player.dir == DIR_UP && queued_dir == DIR_DOWN) ||
                (player.dir == DIR_DOWN && queued_dir == DIR_UP)) {
                player.dir = queued_dir;
                queued_dir = DIR_NONE;
            } else if (player.dir == DIR_NONE) {
                if (can_step_dir(player.world_px, player.world_py, queued_dir)) {
                    player.dir = queued_dir;
                    queued_dir = DIR_NONE;
                }
            } else {
                // 90-degree turning with pre-turn & post-turn cornering window (within 3px of 8px tile intersection)
                int16_t mod_x = player.world_px % MAZE_TILES_SIZE_PX;
                if (mod_x < 0) mod_x += MAZE_TILES_SIZE_PX;
                int16_t mod_y = player.world_py % MAZE_TILES_SIZE_PX;
                if (mod_y < 0) mod_y += MAZE_TILES_SIZE_PX;

                if ((player.dir == DIR_LEFT || player.dir == DIR_RIGHT) &&
                    (queued_dir == DIR_UP || queued_dir == DIR_DOWN)) {
                    if (mod_x <= 3 || mod_x >= 5) {
                        int16_t target_grid_x = (mod_x <= 3) ? (player.world_px - mod_x) : (player.world_px + (8 - mod_x));
                        if (can_step_dir(target_grid_x, player.world_py, queued_dir)) {
                            player.world_px = target_grid_x;
                            player.dir = queued_dir;
                            queued_dir = DIR_NONE;
                        }
                    }
                } else if ((player.dir == DIR_UP || player.dir == DIR_DOWN) &&
                           (queued_dir == DIR_LEFT || queued_dir == DIR_RIGHT)) {
                    if (mod_y <= 3 || mod_y >= 5) {
                        int16_t target_grid_y = (mod_y <= 3) ? (player.world_py - mod_y) : (player.world_py + (8 - mod_y));
                        if (can_step_dir(player.world_px, target_grid_y, queued_dir)) {
                            player.world_py = target_grid_y;
                            player.dir = queued_dir;
                            queued_dir = DIR_NONE;
                        }
                    }
                }
            }
        }
    }

    // 3. Continuous arcade movement using 8.8 fixed-point speed table
    if (player.dir != DIR_NONE) {
        int8_t dx, dy;
        get_dir_offset(player.dir, &dx, &dy);

        uint8_t speed_lvl = get_speed_level_index();
        uint16_t speed_fp = SPEED_TABLE[speed_lvl];

        int16_t move_pixels = 0;
        if (dx != 0) {
            s_speed_subpixel_x += speed_fp;
            move_pixels = s_speed_subpixel_x >> 8;
            s_speed_subpixel_x &= 0x00FF;
        } else if (dy != 0) {
            s_speed_subpixel_y += speed_fp;
            move_pixels = s_speed_subpixel_y >> 8;
            s_speed_subpixel_y &= 0x00FF;
        }

        for (int16_t step = 0; step < move_pixels; step++) {
            // Check direction change at every 8px grid intersection on every 1px step
            if (player.world_px % MAZE_TILES_SIZE_PX == 0 && player.world_py % MAZE_TILES_SIZE_PX == 0) {
                if (queued_dir != DIR_NONE) {
                    if ((player.dir == DIR_RIGHT && queued_dir == DIR_LEFT) ||
                        (player.dir == DIR_LEFT && queued_dir == DIR_RIGHT) ||
                        (player.dir == DIR_UP && queued_dir == DIR_DOWN) ||
                        (player.dir == DIR_DOWN && queued_dir == DIR_UP)) {
                        player.dir = queued_dir;
                        queued_dir = DIR_NONE;
                    } else if (can_step_dir(player.world_px, player.world_py, queued_dir)) {
                        player.dir = queued_dir;
                        queued_dir = DIR_NONE;
                    }
                }
                get_dir_offset(player.dir, &dx, &dy);
            }

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

                // Check and eat pellets/prizes on every 1px step to ensure no pickups are skipped
                check_and_eat_pellet(player.world_px, player.world_py);
            } else {
                player.dir = DIR_NONE; // Stop at wall intersection
                s_speed_subpixel_x = 0;
                s_speed_subpixel_y = 0;
                break;
            }
        }
    } else {
        s_speed_subpixel_x = 0;
        s_speed_subpixel_y = 0;
    }

    // Check if Pacman ate a pellet on current tile
    check_and_eat_pellet(player.world_px, player.world_py);

    // Update active score popups (30 frame timer & queue decay)
    update_score_popups();

    // 4. Pac-Man mouth chomping animation
    static uint8_t anim_timer = 0;
    static bool mouth_open = false;
    static int8_t last_dir = DIR_LEFT;

    if (player.dir != DIR_NONE) {
        last_dir = player.dir; // Track facing direction while moving

        anim_timer++;
        if (anim_timer >= 6) { // Toggle frame every 6 ticks (~10Hz animation)
            anim_timer = 0;
            mouth_open = !mouth_open;
        }

        uint8_t base_frame = 4;
        switch (player.dir) {
            case DIR_UP:    base_frame = 0; break;
            case DIR_DOWN:  base_frame = 2; break;
            case DIR_LEFT:  base_frame = 4; break;
            case DIR_RIGHT: base_frame = 6; break;
        }

        player.frame = base_frame + (mouth_open ? 0 : 1);
    } else {
        // When stationary/idle, show open mouth frame for last facing direction
        uint8_t open_frame = 4;
        switch (last_dir) {
            case DIR_UP:    open_frame = 0; break;
            case DIR_DOWN:  open_frame = 2; break;
            case DIR_LEFT:  open_frame = 4; break;
            case DIR_RIGHT: open_frame = 6; break;
        }
        player.frame = open_frame;
    }

    // 5. Endless wrap Pac-Man in world space
    if (player.world_px < 0) {
        player.world_px += WORLD_WIDTH;
    } else if (player.world_px >= WORLD_WIDTH) {
        player.world_px -= WORLD_WIDTH;
    }

    // Vertical tunnel wrapping using Pac-Man's drawn screen position (world_py + VISUAL_Y_OFFSET)
    int16_t drawn_y = player.world_py + VISUAL_Y_OFFSET;

    if (player.dir == DIR_DOWN && (drawn_y + SPRITE_SIZE_PX) >= 216) {
        player.world_py -= 184; // Moving down: bottom of drawn sprite hits >= 216 -> shift up
    } else if (player.dir == DIR_UP && drawn_y <= 28) {
        player.world_py += 184; // Moving up: top of drawn sprite hits <= 28 -> shift down
    }

    // Screen Y tracks world Y with visual offset
    player.y_pos_px = player.world_py + VISUAL_Y_OFFSET;

    // Endless scrolling offset for the maze tilemap
    maze_dx = player.x_pos_px - player.world_px;

    int16_t visual_x = player.x_pos_px + VISUAL_X_OFFSET;

    // Update hardware registers
    xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, visual_x);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));

    // Update Ghost screen positions relative to endless maze scroll (with visual offsets)
    for (int i = 0; i < NGHOSTS; i++) {
        while (ghosts[i].world_px < 0) {
            ghosts[i].world_px += WORLD_WIDTH;
        }
        while (ghosts[i].world_px >= WORLD_WIDTH) {
            ghosts[i].world_px -= WORLD_WIDTH;
        }

        int16_t ghost_screen_x = ghosts[i].world_px + maze_dx;

        // Endless horizontal scrolling wrapping for ghost screen coordinates relative to canvas width
        while (ghost_screen_x < -16) {
            ghost_screen_x += WORLD_WIDTH;
        }
        while (ghost_screen_x > (SCREEN_WIDTH + 16)) {
            ghost_screen_x -= WORLD_WIDTH;
        }

        ghosts[i].x_pos_px = ghost_screen_x + VISUAL_X_OFFSET;
        ghosts[i].y_pos_px = ghosts[i].world_py + VISUAL_Y_OFFSET;

        uint16_t current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, ghosts[i].x_pos_px);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, ghosts[i].y_pos_px);
    }

    // Check collisions with ghosts after Pac-Man movement update
    check_pacman_ghost_collisions();
}