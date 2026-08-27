#include <rp6502.h>
#include <stdbool.h>
#include "constants.h"
#include "tile_mode2.h"
#include "sprite_mode5.h"
#include "input.h"
#include "player.h"
#include "ghost.h"
#include "prizes.h"
#include "opl.h"
#include "title_anim.h"
#include "results.h"
#include "hiscores.h"
#include "congrats.h"
#include "pause.h"
#include "options.h"
#include "soundtest.h"
#include "countdown.h"

typedef enum {
    STATE_TITLE,
    STATE_GAMEPLAY,
    STATE_PAUSED,
    STATE_RESULTS,
    STATE_CONGRATS,
    STATE_RANKINGS,
    STATE_OPTIONS,
    STATE_SOUNDTEST
} game_state_t;

typedef enum {
    TITLE_SUBSTATE_COLD_SLIDE,            // 240 frames sliding title map y_pos_px from 240 down to 0
    TITLE_SUBSTATE_PRESS_START,           // Flashing "PRESS START BUTTON" every 32 frames until START
    TITLE_SUBSTATE_MENU_READY,            // Normal menu ("NORMAL", "EXTRA", "OPTIONS" with Pac-Man cursor)
    TITLE_SUBSTATE_GAME_START_WAIT_40,    // 40 frames wait after selecting NORMAL (cursor removed, music stopped)
    TITLE_SUBSTATE_GAME_START_FADE_OUT,   // 8 frames fade out title screen to black
    TITLE_SUBSTATE_GAME_START_BLACK_18,   // 18 frames black screen wait
    TITLE_SUBSTATE_GAME_START_FADE_IN,    // 8 frames fade in maze with Pac-Man & ghosts at start positions
    TITLE_SUBSTATE_GAME_START_WAIT_20,    // 20 frames wait with maze revealed
    TITLE_SUBSTATE_GAME_START_INTRO_MUSIC,// 246 frames wait while playing PACMAN01.BIN intro theme
    TITLE_SUBSTATE_WARM_FADE_OUT,         // 8 frames fade to black from gameplay
    TITLE_SUBSTATE_WARM_BLACK,            // 16 frames black screen
    TITLE_SUBSTATE_WARM_FADE_IN            // 8 frames fade in title screen
} title_substate_t;

static game_state_t s_game_state = STATE_TITLE;
static title_substate_t s_title_substate = TITLE_SUBSTATE_COLD_SLIDE;
static uint16_t s_title_timer = 0;

static uint8_t s_menu_selection = 0; // 0: NORMAL, 1: EXTRAS, 2: OPTION
static uint8_t s_pacman_anim_timer = 0;
static uint8_t s_pacman_anim_frame = 6; // 6 or 7
static bool s_game_bgm_playing = false;

void hide_all_ghosts(void) {
    for (int i = 0; i < NGHOSTS; i++) {
        ghosts[i].x_pos_px = -32;
        ghosts[i].y_pos_px = -32;
        unsigned current_ghost_config = GHOST_CONFIG + (i * sizeof(vga_mode5_sprite_t));
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, x_pos_px, -32);
        xram0_struct_set(current_ghost_config, vga_mode5_sprite_t, y_pos_px, -32);
    }
}

void set_pacman_cursor_hidden(void) {
    player.x_pos_px = -32;
    player.y_pos_px = -32;
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, -32);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, -32);
}

void start_title_screen(void) {
    s_game_state = STATE_TITLE;
    s_title_substate = TITLE_SUBSTATE_COLD_SLIDE;
    s_title_timer = 0;
    s_menu_selection = 0;
    s_game_bgm_playing = false;

    music_stop();
    sfx_stop();
    title_anim_reset();

    // 1. Hide maze by setting maze palette to all black
    set_maze_palette_black();

    // 2. Cold start title palette setup:
    // Restore normal title palette first, then override indices 5 and 7 to 0x0000
    restore_title_palette();
    RIA.addr0 = TITLE_PALETTE_ADDR + (5 * 2);
    RIA.step0 = 1;
    RIA.rw0 = 0x00;
    RIA.rw0 = 0x00;

    RIA.addr0 = TITLE_PALETTE_ADDR + (7 * 2);
    RIA.step0 = 1;
    RIA.rw0 = 0x00;
    RIA.rw0 = 0x00;

    // Set font palette to black initially
    set_font_palette_black();

    // 3. Set y_pos_px for TITLE_MAP_CONFIG to 240
    xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, y_pos_px, 240);

    // 4. Hide ghost and pacman cursor sprites, clear menu text
    hide_all_ghosts();
    set_pacman_cursor_hidden();
    reset_prizes_and_mazes_level();

    write_text_to_text_map(11, 6,  "                  ");
    write_text_to_text_map(11, 12, "                  ");
    write_text_to_text_map(17, 8,  "       ");
    write_text_to_text_map(17, 9,  "       ");
    write_text_to_text_map(17, 10, "       ");
}

void start_warm_title_screen(void) {
    s_game_state = STATE_TITLE;
    s_title_substate = TITLE_SUBSTATE_WARM_FADE_OUT;
    s_title_timer = 0;
    s_menu_selection = 0;
    s_game_bgm_playing = false;

    music_stop();
    sfx_stop();
    title_anim_reset();
    hide_all_ghosts();
    set_pacman_cursor_hidden();
    reset_prizes_and_mazes_level();
}

void start_results_screen(void) {
    s_game_state = STATE_RESULTS;
    s_game_bgm_playing = false;

    music_stop();
    sfx_stop();
    hide_all_ghosts();
    set_pacman_cursor_hidden();
    reset_prizes_and_mazes_level();

    results_init();
}

void start_congrats_screen(int8_t rank) {
    s_game_state = STATE_CONGRATS;
    congrats_init(rank);
}

void start_rankings_screen(bool return_to_options) {
    s_game_state = STATE_RANKINGS;
    rankings_init(return_to_options);
}

void start_options_screen(void) {
    s_game_state = STATE_OPTIONS;
    options_init();
}

void start_soundtest_screen(void) {
    s_game_state = STATE_SOUNDTEST;
    soundtest_init();
}

// CONTINUE, from the pause screen: gameplay was never actually touched
// while paused (no update function for it ran), so resuming is just
// switching the dispatch target back -- ghosts, player, timers, and the
// maze are exactly where they were.
void resume_gameplay_from_pause(void) {
    s_game_state = STATE_GAMEPLAY;
}

// RETRY, from the pause screen: the same "NORMAL selected" transition
// the title menu uses (TITLE_SUBSTATE_GAME_START_WAIT_40 through
// GAME_START_BLACK_18 does the actual score/lives/ghost/maze reset), so
// a retry ends up identical to starting fresh from the title. Since this
// fires mid-gameplay rather than from the title screen, the maze itself
// (never touched by that sequence's own fade, which only fades the
// title/font layers) needs to be explicitly blacked out here too, or
// the screen would hard-cut instead of fading.
void start_game_retry(void) {
    set_pacman_cursor_hidden();
    hide_all_ghosts();
    music_stop();
    sfx_stop();
    title_anim_reset();
    set_maze_palette_black();
    s_game_state = STATE_TITLE;
    s_title_substate = TITLE_SUBSTATE_GAME_START_WAIT_40;
    s_title_timer = 0;
}

void start_normal_game(void) {
    s_game_state = STATE_GAMEPLAY;
    s_game_bgm_playing = false;

    music_stop();
    sfx_stop();

    // 1. Clear text map & hide title screen
    write_text_to_text_map(11, 6,  "                  ");
    write_text_to_text_map(11, 12, "                  ");
    write_text_to_text_map(17, 8,  "       ");
    write_text_to_text_map(17, 9,  "       ");
    write_text_to_text_map(17, 10, "       ");
    set_title_palette_black();

    // 2. Restore maze and font palettes
    restore_maze_palette();
    restore_font_palette();

    // 3. Reset ghosts to initial positions and restore sprite visibility
    reset_ghosts_to_initial_positions();
    reset_prizes_and_mazes_level();

    // 4. Reset player attributes and position to initial start (23, 21)
    player.world_px = 23 * MAZE_TILES_SIZE_PX; // 184
    player.world_py = 21 * MAZE_TILES_SIZE_PX; // 168
    player.x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
    player.y_pos_px = player.world_py - 3;
    player.score = 0;
    player.score_by_cat[SCORE_CAT_PELLET] = 0;
    player.score_by_cat[SCORE_CAT_PRIZE] = 0;
    player.score_by_cat[SCORE_CAT_GHOST] = 0;
    reset_score_history();
    player.pellets_eaten = 0;
    player.lives = 3;
    player.dir = DIR_NONE;
    clear_player_queued_dir();
    player.frame = 5; // Facing left / closed

    update_player_score_display(player.score);
    update_player_lives_display(player.lives);
    reset_game_timer();
    countdown_reset();

    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
    xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
}

static bool init_graphics(void)
{
    // 320×240 canvas
    int rc;
    rc = xreg_vga_canvas(1);
    if (rc < 0) {
        return false;
    }

    // Initialize tile mode 2 (8×8 tiles, 8-bit color index)
    tile_mode2_init();
    init_ghost_data();
    sprite_mode5_init();
    tile_mode2_text_map_init();
    tile_mode2_title_map_init();
    init_side_pellet_counters();

    return true;
}

uint8_t vsync_last = 0;
uint8_t frame = 0;

int main(void)
{
    input_actions_t actions;
    input_init();

    // Must run before init_graphics() -- tile_mode2_text_map_init() ->
    // init_tilemap_edges() draws the persisted top score into the "HI"
    // slot immediately.
    hiscores_load();

    // Initialise audio hardware
    OPL_Config(1, OPL_ADDR);
    opl_init();

    // Initialise graphics
    if (!init_graphics()) {
        return 1;
    }

    // Start on Title Screen (Cold Start)
    start_title_screen();

    static bool prev_up = false;
    static bool prev_down = false;
    static bool prev_left = false;
    static bool prev_right = false;
    static bool prev_action = false;
    static bool prev_start = false;
    static bool prev_fire = false;

    // Main loop
    while (true) {
        // 1. SYNC (wait for real vsync tick)
        if (RIA.vsync == vsync_last) continue;
        vsync_last = RIA.vsync;

        // Advance OPL2 music sequencer -- always exactly 1 tick per real
        // vsync tick, matching RPDemo's proven pattern: a single sync
        // variable and no ticks-delta math, so there's nothing for a
        // loop reorder to desync.
        update_music_advance(1);
        // Reserved SFX channel (5): fully independent of the above, same
        // once-per-real-tick contract.
        update_sfx_advance(1);

        // 2. INPUT
        input_poll(&actions);

        bool press_up = (actions.up && !prev_up);
        bool press_down = (actions.down && !prev_down);
        bool press_left = (actions.left && !prev_left);
        bool press_right = (actions.right && !prev_right);
        bool press_action = (actions.bomb && !prev_action);
        bool press_start = (actions.start && !prev_start);
        bool press_fire = (actions.fire && !prev_fire);

        prev_up = actions.up;
        prev_down = actions.down;
        prev_left = actions.left;
        prev_right = actions.right;
        prev_action = actions.bomb;
        prev_start = actions.start;
        prev_fire = actions.fire;

        if (s_game_state == STATE_TITLE) {
            switch (s_title_substate) {
                case TITLE_SUBSTATE_COLD_SLIDE: {
                    // Over 240 frames, slide y_pos_px from 240 down to 0
                    if (s_title_timer < 240) {
                        int16_t current_y = 240 - (int16_t)s_title_timer;
                        xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, y_pos_px, current_y);
                        s_title_timer++;
                    } else {
                        xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, y_pos_px, 0);

                        // Restore normal title and font palettes
                        restore_title_palette();
                        restore_font_palette();

                        // Write "PICOCOMPUTER  6502" at (11, 6) and "PRESS START BUTTON" at (11, 12)
                        write_text_to_text_map(11, 6,  "PICOCOMPUTER  6502");
                        write_text_to_text_map(11, 12, "PRESS START BUTTON");

                        // Start Title Music (PACMAN02.BIN)
                        music_init("ROM:pacman02");
                        title_anim_reset();

                        s_title_substate = TITLE_SUBSTATE_PRESS_START;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_PRESS_START: {
                    s_title_timer++;

                    // Flashes on/off every 32 frames (0..31 ON, 32..63 OFF)
                    bool text_on = ((s_title_timer % 64) < 32);
                    if (text_on) {
                        write_text_to_text_map(11, 12, "PRESS START BUTTON");
                    } else {
                        write_text_to_text_map(11, 12, "                  ");
                    }

                    // Update title screen cutscene animation sequence!
                    title_anim_update();

                    // Check for Start button
                    if (press_start || press_action) {
                        write_text_to_text_map(11, 12, "                  ");
                        write_text_to_text_map(11, 6,  "PICOCOMPUTER  6502");
                        write_text_to_text_map(17, 8,  "NORMAL");
                        write_text_to_text_map(17, 9,  "EXTRA ");
                        write_text_to_text_map(17, 10, "OPTIONS");

                        // Park all cutscene sprites off-screen when entering menu
                        title_anim_reset();

                        s_menu_selection = 0;
                        s_title_substate = TITLE_SUBSTATE_MENU_READY;
                    }
                    break;
                }

                case TITLE_SUBSTATE_MENU_READY: {
                    // Menu Navigation
                    if (press_up) {
                        if (s_menu_selection > 0) {
                            s_menu_selection--;
                        } else {
                            s_menu_selection = 2;
                        }
                    } else if (press_down) {
                        if (s_menu_selection < 2) {
                            s_menu_selection++;
                        } else {
                            s_menu_selection = 0;
                        }
                    }

                    // Select Menu Option (Gamepad 'A' button)
                    if (press_action) {
                        if (s_menu_selection == 0) {
                            // "NORMAL" selected -> Begin game start transition sequence!
                            set_pacman_cursor_hidden();
                            music_stop();
                            title_anim_reset();
                            s_title_substate = TITLE_SUBSTATE_GAME_START_WAIT_40;
                            s_title_timer = 0;
                        } else if (s_menu_selection == 2) {
                            // "OPTIONS" selected -> fade out and swap to the
                            // options screen; options.c's own fade-in swaps
                            // the title music for pacman07.
                            set_pacman_cursor_hidden();
                            start_options_screen();
                        }
                    }

                    if (s_game_state == STATE_TITLE && s_title_substate == TITLE_SUBSTATE_MENU_READY) {
                        // Animate Pac-Man cursor sprite (tiles 6 and 7, 4 frames each)
                        s_pacman_anim_timer++;
                        if (s_pacman_anim_timer >= 4) {
                            s_pacman_anim_timer = 0;
                            s_pacman_anim_frame = (s_pacman_anim_frame == 6) ? 7 : 6;
                        }

                        // Position Pac-Man cursor to the left of the selected menu item (117px, y offset +1)
                        player.x_pos_px = 117;
                        player.y_pos_px = (8 + s_menu_selection) * 16 + 1;
                        player.frame = s_pacman_anim_frame;

                        xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
                        xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
                        xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));
                    }
                    break;
                }

                case TITLE_SUBSTATE_GAME_START_WAIT_40: {
                    // 40 frames wait after selecting NORMAL
                    if (s_title_timer < 40) {
                        s_title_timer++;
                    } else {
                        s_title_substate = TITLE_SUBSTATE_GAME_START_FADE_OUT;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_GAME_START_FADE_OUT: {
                    // 8 frames: fade title & font palettes out to black (every 2 frames)
                    if (s_title_timer < 8) {
                        uint8_t step = 4 - (s_title_timer / 2); // 4, 3, 2, 1
                        set_title_palette_scaled(step - 1, 4, false, 0, false);
                        set_font_palette_scaled(step - 1, 4);
                        s_title_timer++;
                    } else {
                        set_title_palette_black();
                        set_font_palette_black();
                        s_title_substate = TITLE_SUBSTATE_GAME_START_BLACK_18;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_GAME_START_BLACK_18: {
                    // 18 frames black screen wait
                    if (s_title_timer < 18) {
                        s_title_timer++;
                    } else {
                        // Clear title text & restore maze map
                        write_text_to_text_map(11, 6,  "                  ");
                        write_text_to_text_map(11, 12, "                  ");
                        write_text_to_text_map(17, 8,  "       ");
                        write_text_to_text_map(17, 9,  "       ");
                        write_text_to_text_map(17, 10, "       ");

                        // Reset the player's world/screen position -- and
                        // maze_dx, the endless-scroll offset -- BEFORE
                        // resetting ghosts. maze_dx is a global that
                        // player_update_motion() normally recomputes every
                        // gameplay frame, but nothing recomputes it here:
                        // left at whatever value the PREVIOUS game ended
                        // on, the maze background and all 4 ghosts' initial
                        // screen positions (init_ghost_data() below derives
                        // theirs from world_px + maze_dx) render at that
                        // stale offset for several seconds of fade/intro
                        // (nothing corrects it until the first real
                        // gameplay frame runs) -- an uncentered maze that
                        // visibly snaps into place once it does, and
                        // possibly-coincident stale ghost screen positions
                        // that could spuriously overlap Pac-Man's fresh
                        // position for a collision check.
                        player.world_px = 23 * MAZE_TILES_SIZE_PX; // 184
                        player.world_py = 21 * MAZE_TILES_SIZE_PX; // 168
                        player.x_pos_px = (int16_t)((SCREEN_WIDTH - SPRITE_SIZE_PX) / 2);
                        player.y_pos_px = player.world_py - 3;
                        maze_dx = player.x_pos_px - player.world_px;
                        xram0_struct_set(MAZE_CONFIG, vga_mode2_config_t, x_pos_px, maze_dx);

                        // Reset ghosts, maze, and player to starting positions
                        reset_ghosts_to_initial_positions();
                        reset_death_sequence();
                        reset_prizes_and_mazes_level();

                        player.score = 0;
                        player.score_by_cat[SCORE_CAT_PELLET] = 0;
                        player.score_by_cat[SCORE_CAT_PRIZE] = 0;
                        player.score_by_cat[SCORE_CAT_GHOST] = 0;
                        reset_score_history();
                        reset_extra_life_threshold();
                        player.pellets_eaten = 0;
                        player.lives = 3;
                        player.dir = DIR_NONE;
                        clear_player_queued_dir();
                        player.frame = 5; // Facing left / closed

                        update_player_score_display(player.score);
                        update_player_lives_display(player.lives);
                        reset_game_timer();
                        countdown_reset();

                        xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, x_pos_px, player.x_pos_px);
                        xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, y_pos_px, player.y_pos_px);
                        xram0_struct_set(PLAYER_CONFIG, vga_mode5_sprite_t, xram_sprite_ptr, (SPRITE_DATA + (player.frame * SPRITE_FRAME_SIZE)));

                        s_title_substate = TITLE_SUBSTATE_GAME_START_FADE_IN;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_GAME_START_FADE_IN: {
                    // 8 frames: fade in maze and font palettes from black (every 2 frames)
                    if (s_title_timer < 8) {
                        s_title_timer++;
                    } else {
                        restore_maze_palette();
                        restore_font_palette();
                        s_title_substate = TITLE_SUBSTATE_GAME_START_WAIT_20;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_GAME_START_WAIT_20: {
                    // 20 frames wait with maze revealed
                    if (s_title_timer < 20) {
                        s_title_timer++;
                    } else {
                        // Play PACMAN01.BIN intro theme
                        music_init("ROM:pacman01");
                        s_title_substate = TITLE_SUBSTATE_GAME_START_INTRO_MUSIC;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_GAME_START_INTRO_MUSIC: {
                    // 246 frames wait while playing intro music PACMAN01.BIN
                    update_ready_go_animation(s_title_timer);

                    if (s_title_timer < 246) {
                        s_title_timer++;
                    } else {
                        // Intro music complete -> Clear READY/GO message & start game!
                        clear_ready_go_animation();

                        s_game_state = STATE_GAMEPLAY;
                        s_game_bgm_playing = false;

                        // Automatically start Pac-Man moving left
                        player.dir = DIR_LEFT;
                        clear_player_queued_dir();
                        set_game_motion_started(true);

                        // Start playing gameplay music (PACMAN00.BIN, now
                        // remapped in CMakeLists.txt to PacManCE_00.BIN)
                        s_game_bgm_playing = true;
                        music_init("ROM:pacman00");
                        sfx_set_ambient("ROM:sfxnormal");
                    }
                    break;
                }

                case TITLE_SUBSTATE_WARM_FADE_OUT: {
                    // 8 frames: fade title & font & maze palettes out to black (every 2 frames)
                    if (s_title_timer < 8) {
                        uint8_t step = 4 - (s_title_timer / 2); // 4, 3, 2, 1
                        set_title_palette_scaled(step - 1, 4, false, 0, false);
                        set_font_palette_scaled(step - 1, 4);
                        s_title_timer++;
                    } else {
                        set_title_palette_black();
                        set_font_palette_black();
                        set_maze_palette_black();
                        s_title_substate = TITLE_SUBSTATE_WARM_BLACK;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_WARM_BLACK: {
                    // 16 frames on black screen
                    if (s_title_timer < 16) {
                        s_title_timer++;
                    } else {
                        // Prepare title screen for fade in
                        xram0_struct_set(TITLE_MAP_CONFIG, vga_mode2_config_t, y_pos_px, 0);

                        write_text_to_text_map(11, 6,  "PICOCOMPUTER  6502");
                        write_text_to_text_map(17, 8,  "NORMAL");
                        write_text_to_text_map(17, 9,  "EXTRA ");
                        write_text_to_text_map(17, 10, "OPTIONS");

                        // Start Title Music (PACMAN02.BIN) when returning to title screen after Game Over
                        music_init("ROM:pacman02");

                        s_menu_selection = 0;
                        s_title_substate = TITLE_SUBSTATE_WARM_FADE_IN;
                        s_title_timer = 0;
                    }
                    break;
                }

                case TITLE_SUBSTATE_WARM_FADE_IN: {
                    // 8 frames: fade in title & font palettes from black (every 2 frames)
                    if (s_title_timer < 8) {
                        uint8_t step = (s_title_timer / 2) + 1; // 1, 2, 3, 4
                        set_title_palette_scaled(step, 4, false, 0, false);
                        set_font_palette_scaled(step, 4);
                        s_title_timer++;
                    } else {
                        restore_title_palette();
                        restore_font_palette();
                        s_title_substate = TITLE_SUBSTATE_MENU_READY;
                        s_title_timer = 0;
                    }
                    break;
                }
            }

            hide_all_ghosts();
        } else if (s_game_state == STATE_GAMEPLAY) {
            if (press_start) {
                s_game_state = STATE_PAUSED;
                pause_init();
                continue;
            }

            // 2. STATE_GAMEPLAY Updates
            // Once the end-of-game countdown reaches its FINISH! hold,
            // normal motion stops -- the game is effectively over, just
            // waiting to hand off to the results screen.
            if (!countdown_should_freeze()) {
                player_update_motion(&actions);
                ghost_update_motion();
                prize_update_motion();
                update_maze_munchers_animation();
                update_lives_blink_animation();
            }
            update_game_timer_display();

            countdown_update();

            // Start playing gameplay music (PACMAN00.BIN, remapped to
            // PacManCE_00.BIN) once Pac-Man begins to move
            if (!s_game_bgm_playing && is_game_motion_started()) {
                s_game_bgm_playing = true;
                music_init("ROM:pacman00");
                sfx_set_ambient("ROM:sfxnormal");
            }

            if (countdown_finished()) {
                // 5 minute timer expired and the FINISH! hold completed ->
                // End game, show results screen
                start_results_screen();
            } else if (!countdown_should_freeze()) {
                // Update maze palette animation
                tile_mode2_palette_update(frame);
            }
        } else if (s_game_state == STATE_PAUSED) {
            pause_update(press_up, press_down, press_action);
        } else if (s_game_state == STATE_RESULTS) {
            results_update(press_start, press_action);
        } else if (s_game_state == STATE_CONGRATS) {
            congrats_update(press_up, press_down, press_action);
        } else if (s_game_state == STATE_RANKINGS) {
            rankings_update(press_start || press_action);
        } else if (s_game_state == STATE_OPTIONS) {
            options_update(press_up, press_down, press_action);
        } else { // STATE_SOUNDTEST
            soundtest_update(press_up, press_down, press_left, press_right, press_action, press_fire);
        }

        frame++;
        if (frame >= 60) frame = 0;
    }

    return 0;
}
