#include <rp6502.h>
#include <stdint.h>
#include <stdbool.h>
#include "constants.h"
#include "sprite_mode5.h"
#include "title_anim.h"

// Title animation state machine steps
typedef enum {
    TITLE_ANIM_WAIT_INITIAL = 0,
    TITLE_ANIM_R2L_PACMAN,
    TITLE_ANIM_WAIT_120_1,
    TITLE_ANIM_L2R_PACMAN_GHOST,
    TITLE_ANIM_L2R_FRIGHTENED_BIGPAC,
    TITLE_ANIM_WAIT_120_2,
    TITLE_ANIM_L2R_BIGPAC_2BIGGHOSTS,
    TITLE_ANIM_L2R_FRIGHTENED_FINAL,
    TITLE_ANIM_WAIT_120_FINAL
} title_anim_state_t;

static title_anim_state_t s_anim_state = TITLE_ANIM_WAIT_INITIAL;
static uint16_t s_anim_timer = 0;
static uint16_t s_frame_cnt = 0; // Cumulative frames in active sub-phase

// Helper to park an array of sprite slots offscreen (-32, -32) with blank frame 48
static void park_sprites(uint16_t base_config, int count) {
    for (int i = 0; i < count; i++) {
        unsigned cfg = base_config + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(cfg, vga_mode5_sprite_t, x_pos_px, -32);
        xram0_struct_set(cfg, vga_mode5_sprite_t, y_pos_px, -32);
        xram0_struct_set(cfg, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (48 * SPRITE_FRAME_SIZE)));
        xram0_struct_set(cfg, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);
    }
}

// Helper to update a single 16x16 sprite slot
static void set_sprite_slot(uint16_t base_config, int index, int16_t x, int16_t y, uint8_t frame) {
    unsigned cfg = base_config + (index * sizeof(vga_mode5_sprite_t));
    xram0_struct_set(cfg, vga_mode5_sprite_t, x_pos_px, x);
    xram0_struct_set(cfg, vga_mode5_sprite_t, y_pos_px, y);
    xram0_struct_set(cfg, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + ((uint32_t)frame * SPRITE_FRAME_SIZE)));
    xram0_struct_set(cfg, vga_mode5_sprite_t, palette_ptr, PLAYER_PALETTE_ADDR);
}

// Helper to render a 2x2 compound sprite (32x32) given top-left corner (x, y)
// and top-left/top-right/bottom-left/bottom-right frame indices.
static void set_2x2_compound(uint16_t base_config, int start_index, int16_t x, int16_t y,
                             uint8_t tl, uint8_t tr, uint8_t bl, uint8_t br) {
    set_sprite_slot(base_config, start_index + 0, x,      y,      tl);
    set_sprite_slot(base_config, start_index + 1, x + 16, y,      tr);
    set_sprite_slot(base_config, start_index + 2, x,      y + 16, bl);
    set_sprite_slot(base_config, start_index + 3, x + 16, y + 16, br);
}

void title_anim_reset(void) {
    s_anim_state = TITLE_ANIM_WAIT_INITIAL;
    s_anim_timer = 0;
    s_frame_cnt = 0;

    // Park all munchers (12 slots) and score displays (12 slots) offscreen
    park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
    park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);
}

void title_anim_update(void) {
    s_frame_cnt++;

    switch (s_anim_state) {
        case TITLE_ANIM_WAIT_INITIAL: {
            park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);
            s_anim_timer++;
            if (s_anim_timer >= 30) {
                s_anim_state = TITLE_ANIM_R2L_PACMAN;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_R2L_PACMAN: {
            // Row 152, starting at column 319 -> moves right to left at 1 px/frame
            // Frame cadence: alternate frames 4 and 5 every 4 frames
            int16_t pac_x = 319 - (int16_t)s_frame_cnt;
            int16_t pac_y = 152;

            uint8_t pac_frame = ((s_frame_cnt / 4) % 2 == 0) ? 4 : 5;
            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 0, pac_x, pac_y, pac_frame);

            // Hide remaining unused slots
            park_sprites(MAZE_MUNCHERS_CONFIG + 1 * sizeof(vga_mode5_sprite_t), NMAZE_MUNCHERS - 1);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);

            // Position < -16 means fully off screen on left
            if (pac_x < -16) {
                park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
                s_anim_state = TITLE_ANIM_WAIT_120_1;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_WAIT_120_1: {
            park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);
            s_anim_timer++;
            if (s_anim_timer >= 120) {
                s_anim_state = TITLE_ANIM_L2R_PACMAN_GHOST;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_L2R_PACMAN_GHOST: {
            // Pac-Man starts at -16, moves L->R at 1 px/frame
            int16_t pac_x = -16 + (int16_t)s_frame_cnt;
            int16_t pac_y = 152;
            uint8_t pac_frame = ((s_frame_cnt / 4) % 2 == 0) ? 6 : 7;
            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 0, pac_x, pac_y, pac_frame);

            // Trailing pellet/dot at offset -16, -8 relative to Pac-Man -> (pac_x - 16, pac_y - 8) = (pac_x - 16, 144)
            // Alternate frames 153 and 154 every 4 frames
            uint8_t dot_frame = ((s_frame_cnt / 4) % 2 == 0) ? 153 : 154;
            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 1, pac_x - 16, pac_y - 8, dot_frame);

            // Blue Ghost chases Pac-Man at 1 px/frame, plus 1 extra px every 12th frame (1.083 px/frame average)
            // Ghost starts trailing behind Pac-Man at -64 (48px behind Pac-Man's starting -16)
            int16_t ghost_extra_px = (int16_t)(s_frame_cnt / 12);
            int16_t ghost_x = -64 + (int16_t)s_frame_cnt + ghost_extra_px;
            int16_t ghost_y = 152;
            uint8_t ghost_frame = ((s_frame_cnt / 4) % 2 == 0) ? 30 : 31; // Blue Ghost frames 30 and 31
            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 2, ghost_x, ghost_y, ghost_frame);

            // Clear remaining slots
            park_sprites(MAZE_MUNCHERS_CONFIG + 3 * sizeof(vga_mode5_sprite_t), NMAZE_MUNCHERS - 3);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);

            // Wait until blue ghost has fully left the screen (> 320)
            if (ghost_x > 320) {
                park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
                s_anim_state = TITLE_ANIM_L2R_FRIGHTENED_BIGPAC;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_L2R_FRIGHTENED_BIGPAC: {
            // Frightened ghost goes R->L at 1 px/frame starting at 320, row 152
            int16_t ghost_x = 320 - (int16_t)s_frame_cnt;
            int16_t ghost_y = 152;
            uint8_t ghost_frame = ((s_frame_cnt / 4) % 2 == 0) ? 40 : 41; // Frightened ghost frames 40 and 41
            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 0, ghost_x, ghost_y, ghost_frame);

            // Trailing dot at position +16, -8 relative to ghost -> (ghost_x + 16, ghost_y - 8)
            // Alternate frames 153 and 154 every 4 frames
            uint8_t dot_frame = ((s_frame_cnt / 4) % 2 == 0) ? 153 : 154;
            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 1, ghost_x + 16, ghost_y - 8, dot_frame);

            // Large Pac-Man trails ghost with initial separation +32px (total separation 80px: bigpac_x = ghost_x + 80)
            // Large Pac-Man moves slightly faster: 2px every 12 frames (1.167 px/frame average)
            int16_t bigpac_extra_px = (int16_t)((s_frame_cnt / 12) * 2);
            int16_t bigpac_x = 400 - (int16_t)s_frame_cnt - bigpac_extra_px;

            // Bottom edge of Large Pac-Man (height 32px) aligns with 16px ghost (y=152..168) -> bigpac_y = 136
            int16_t bigpac_y = 136;

            // Large Pac-Man mouth animation facing LEFT (2 frames cadence)
            // Frames 117..120 base (+4 steps: 121..124, 125..128, 121..124)
            static const uint8_t BIGPAC_R_STEPS[4][4] = {
                { 117, 118, 119, 120 },
                { 121, 122, 123, 124 },
                { 125, 126, 127, 128 },
                { 121, 122, 123, 124 }
            };
            uint8_t step_idx = (s_frame_cnt / 2) % 4;
            set_2x2_compound(MAZE_MUNCHERS_CONFIG, 2, bigpac_x, bigpac_y,
                             BIGPAC_R_STEPS[step_idx][0], BIGPAC_R_STEPS[step_idx][1],
                             BIGPAC_R_STEPS[step_idx][2], BIGPAC_R_STEPS[step_idx][3]);

            // Hide remaining slots (slots 6..11 in MAZE_MUNCHERS_CONFIG, all GHOST_SCORE_CONFIG)
            park_sprites(MAZE_MUNCHERS_CONFIG + 6 * sizeof(vga_mode5_sprite_t), NMAZE_MUNCHERS - 6);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);

            // Wait until Large Pac-Man has left the screen on the left (< -32)
            if (bigpac_x < -32) {
                park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
                s_anim_state = TITLE_ANIM_WAIT_120_2;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_WAIT_120_2: {
            park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);
            s_anim_timer++;
            if (s_anim_timer >= 120) {
                s_anim_state = TITLE_ANIM_L2R_BIGPAC_2BIGGHOSTS;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_L2R_BIGPAC_2BIGGHOSTS: {
            // Large Pac-Man goes L->R at 1 px/frame starting at -32
            int16_t bigpac_x = -32 + (int16_t)s_frame_cnt;
            int16_t bigpac_y = 136;

            // Animation sequence for L->R Big Pac-Man:
            // Frames 129, 130, 131, 132 (+4 steps: 133,134,135,136 and 125,126,127,128)
            static const uint8_t BIGPAC_R2_STEPS[4][4] = {
                { 129, 130, 131, 132 },
                { 133, 134, 135, 136 },
                { 125, 126, 127, 128 },
                { 133, 134, 135, 136 }
            };
            uint8_t pac_step = (s_frame_cnt / 2) % 4;
            set_2x2_compound(MAZE_MUNCHERS_CONFIG, 0, bigpac_x, bigpac_y,
                             BIGPAC_R2_STEPS[pac_step][0], BIGPAC_R2_STEPS[pac_step][1],
                             BIGPAC_R2_STEPS[pac_step][2], BIGPAC_R2_STEPS[pac_step][3]);

            // Big Ghosts move slightly faster (+2px every 12 frames)
            int16_t ghost_extra_px = (int16_t)((s_frame_cnt / 12) * 2);

            // Big Ghost 1: starting at -120 (trails Big Pac-Man by 88px)
            int16_t bg1_x = -120 + (int16_t)s_frame_cnt + ghost_extra_px;
            int16_t bg1_y = 136;

            // Big Ghost 2: trails Big Ghost 1 by 64px (starting at -184)
            int16_t bg2_x = -184 + (int16_t)s_frame_cnt + ghost_extra_px;
            int16_t bg2_y = 136;

            // Big Ghost animation: base frames 137,138,139,140 and 145,146,147,148
            // Every 4th frame update to 141,142,143,144 and 149,150,151,152
            bool ghost_alt = ((s_frame_cnt / 4) % 2 == 1);

            uint8_t bg1_tl = ghost_alt ? 141 : 137;
            uint8_t bg1_tr = ghost_alt ? 142 : 138;
            uint8_t bg1_bl = ghost_alt ? 143 : 139;
            uint8_t bg1_br = ghost_alt ? 144 : 140;

            uint8_t bg2_tl = ghost_alt ? 149 : 145;
            uint8_t bg2_tr = ghost_alt ? 150 : 146;
            uint8_t bg2_bl = ghost_alt ? 151 : 147;
            uint8_t bg2_br = ghost_alt ? 152 : 148;

            set_2x2_compound(MAZE_MUNCHERS_CONFIG, 4, bg1_x, bg1_y, bg1_tl, bg1_tr, bg1_bl, bg1_br);
            set_2x2_compound(MAZE_MUNCHERS_CONFIG, 8, bg2_x, bg2_y, bg2_tl, bg2_tr, bg2_bl, bg2_br);

            // Hide remaining slots
            park_sprites(MAZE_MUNCHERS_CONFIG + 12 * sizeof(vga_mode5_sprite_t), 0);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);

            // After last big ghost (bg2) has left the screen (> 320)
            if (bg2_x > 320) {
                park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
                s_anim_state = TITLE_ANIM_L2R_FRIGHTENED_FINAL;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_L2R_FRIGHTENED_FINAL: {
            // Frightened ghost moves L->R across the screen at 1 px/frame starting at -16
            int16_t ghost_x = -16 + (int16_t)s_frame_cnt;
            int16_t ghost_y = 152;
            uint8_t ghost_frame = ((s_frame_cnt / 4) % 2 == 0) ? 40 : 41;

            set_sprite_slot(MAZE_MUNCHERS_CONFIG, 0, ghost_x, ghost_y, ghost_frame);

            // Hide all remaining slots
            park_sprites(MAZE_MUNCHERS_CONFIG + 1 * sizeof(vga_mode5_sprite_t), NMAZE_MUNCHERS - 1);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);

            if (ghost_x > 320) {
                park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
                s_anim_state = TITLE_ANIM_WAIT_120_FINAL;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }

        case TITLE_ANIM_WAIT_120_FINAL: {
            park_sprites(MAZE_MUNCHERS_CONFIG, NMAZE_MUNCHERS);
            park_sprites(GHOST_SCORE_CONFIG, NGHOST_SCORE_DISPLAYS);
            s_anim_timer++;
            // 120 frames delay before repeating entire sequence
            if (s_anim_timer >= 120) {
                s_anim_state = TITLE_ANIM_WAIT_INITIAL;
                s_anim_timer = 0;
                s_frame_cnt = 0;
            }
            break;
        }
    }
}
