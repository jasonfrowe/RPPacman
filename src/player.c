#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "player.h"
#include "sprite_mode5.h"
#include "tile_mode2.h"
#include "prizes.h"

#define VISUAL_X_OFFSET (-3) // Visual horizontal draw offset (-3px)
#define VISUAL_Y_OFFSET (-3) // Visual vertical draw offset (-3px)

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

static void push_score_popup(uint16_t tile_x, uint16_t tile_y, uint8_t score_tile) {
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

static void check_and_eat_pellet(int16_t world_x, int16_t world_y) {
    // Pickup bounding box aligned with Pac-Man's visual drawn position:
    // Drawn top-left = (world_x + VISUAL_X_OFFSET, world_y + VISUAL_Y_OFFSET)
    // 16x16 sprite visual center = (+7px .. +8px)
    int16_t drawn_x = world_x + VISUAL_X_OFFSET;
    int16_t drawn_y = world_y + VISUAL_Y_OFFSET;

    int16_t check_points_x[2] = { drawn_x + 7, drawn_x + 8 };
    int16_t check_points_y[2] = { drawn_y + 7, drawn_y + 8 };

    for (uint8_t ix = 0; ix < 2; ix++) {
        for (uint8_t iy = 0; iy < 2; iy++) {
            int16_t check_x = check_points_x[ix];
            int16_t check_y = check_points_y[iy];

            while (check_x < 0) check_x += WORLD_WIDTH;
            while (check_x >= WORLD_WIDTH) check_x -= WORLD_WIDTH;
            while (check_y < 0) check_y += WORLD_HEIGHT;
            while (check_y >= WORLD_HEIGHT) check_y -= WORLD_HEIGHT;

            uint16_t tile_x = (uint16_t)(check_x / MAZE_TILES_SIZE_PX);
            uint16_t tile_y = (uint16_t)(check_y / MAZE_TILES_SIZE_PX);

            if (tile_x >= MAZE_MAP_WIDTH) tile_x %= MAZE_MAP_WIDTH;
            if (tile_y >= MAZE_MAP_HEIGHT) tile_y %= MAZE_MAP_HEIGHT;

            uint16_t offset = tile_y * MAZE_MAP_WIDTH + tile_x;

            RIA.addr0 = MAZE_MAP_DATA + offset;
            RIA.step0 = 1;
            uint8_t tile_index = RIA.rw0;

            // Check if tile is a dot (116) or power pellet (117)
            if (tile_index == 116 || tile_index == 117) {
                uint32_t dot_pts = get_current_dot_value(player.pellets_eaten);
                player.score += dot_pts;
                player.pellets_eaten++;

                update_player_score_display(player.score);

                uint8_t score_tile = get_score_tile_index(dot_pts);
                push_score_popup(tile_x, tile_y, score_tile);

                // Check if clearing this pellet triggered side prize spawning
                update_side_pellets_status();
            }

            // Check if Pacman is consuming an active prize using visual drawn coordinates
            check_and_eat_prize(drawn_x, drawn_y);
        }
    }
}

// 8.8 Fixed-Point Speed Lookup Table (High Byte = Pixels, Low Byte = Sub-pixel fraction)
const uint16_t SPEED_TABLE[13] = {
    0x0100, // Level 0: 1.00 px/frame
    0x0120, // Level 1: 1.12 px/frame
    0x0140, // Level 2: 1.25 px/frame
    0x0160, // Level 3: 1.37 px/frame
    0x0180, // Level 4: 1.50 px/frame
    0x01A0, // Level 5: 1.62 px/frame
    0x01C0, // Level 6: 1.75 px/frame
    0x01E0, // Level 7: 1.87 px/frame
    0x0200, // Level 8: 2.00 px/frame
    0x0220, // Level 9: 2.12 px/frame
    0x0240, // Level 10: 2.25 px/frame
    0x0260, // Level 11: 2.37 px/frame
    0x0280, // Level 12: 2.50 px/frame (Max Cap)
};

static uint16_t s_speed_subpixel_x = 0;
static uint16_t s_speed_subpixel_y = 0;

static uint8_t get_speed_level_index(uint32_t score) {
    uint32_t lvl = score / 20000;
    if (lvl > 12) lvl = 12;
    return (uint8_t)lvl;
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

    // 3. Continuous arcade movement using 8.8 fixed-point speed table
    if (player.dir != DIR_NONE) {
        int8_t dx, dy;
        get_dir_offset(player.dir, &dx, &dy);

        uint8_t speed_lvl = get_speed_level_index(player.score);
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
        player.world_py -= 184; // Moving down: bottom of drawn sprite hits >= 216 -> shift up 200px
    } else if (player.dir == DIR_UP && drawn_y < 24) {
        player.world_py += 184; // Moving up: top of drawn sprite hits < 24 -> shift down 200px
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