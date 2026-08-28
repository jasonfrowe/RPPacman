cmake_minimum_required(VERSION 3.21)

include(${CMAKE_CURRENT_LIST_DIR}/tools/rp6502.cmake)

project(RPPacMan C CXX ASM)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

add_executable(RPPacMan)
rp6502_asset(RPPacMan help src/help.txt)

# XRAM Assets
rp6502_asset(RPPacMan 0x10000 images/Maze_tiles_v2_4bpp.bin)
rp6502_asset(RPPacMan 0x11000 images/Maze_map.bin)
rp6502_asset(RPPacMan 0x11582 images/Sprites_RP6502_4bpp.bin)
rp6502_asset(RPPacMan 0x16402 images/Fonts_RP6502_4bpp.bin)
rp6502_asset(RPPacMan 0x19A82 images/Text_map.bin)
rp6502_asset(RPPacMan 0x19CDA images/R_Mazes.bin)
rp6502_asset(RPPacMan 0x1D970 images/title_map.bin)
rp6502_asset(RPPacMan 0x1DE20 images/Titles_RP6502_4bpp.bin)

# Additional assets to read in from the ROM 
rp6502_asset(RPPacMan resultsmap images/Results_map.bin)
rp6502_asset(RPPacMan resultstiles images/Results_tiles_4bpp.bin) #8x8, 132 frames
rp6502_asset(RPPacMan titlemap images/title_map_copy.bin)
rp6502_asset(RPPacMan titletile images/Titles_RP6502_4bpp_copy.bin)
rp6502_asset(RPPacMan congratsmap images/Congrats_map.bin)
rp6502_asset(RPPacMan congratstiles images/Congrats_tiles_4bpp.bin) #8x8 126 frames
rp6502_asset(RPPacMan rankingsmap images/Rankings_map.bin)
rp6502_asset(RPPacMan rankingstiles images/Rankings_tiles_4bpp.bin) #8x8 106 frames
rp6502_asset(RPPacMan optionsmap images/Options_map.bin)
rp6502_asset(RPPacMan optionstiles images/Options_tiles_4bpp.bin) #8x8 147 frames
rp6502_asset(RPPacMan soundtestmap images/Soundtest_map.bin)
rp6502_asset(RPPacMan soundtesttiles images/Soundtest_tiles_4bpp.bin) #8x8 123 frames
rp6502_asset(RPPacMan readygopt1 images/ReadyGo_pt1_tiles_4bpp.bin) #16x16 52 frames trimmed to 16x8
rp6502_asset(RPPacMan readygopt2 images/ReadyGo_pt2_tiles_4bpp.bin) #16x16 52 frames trimmed to 16x8
rp6502_asset(RPPacMan readygopt3 images/ReadyGo_pt3_tiles_4bpp.bin) #16x16 52 frames trimmed to 16x8
rp6502_asset(RPPacMan rmazesb images/R_Mazes_b.bin) #Extra 'b' maps
rp6502_asset(RPPacMan rmazesc images/R_Mazes_c.bin) #Extra 'c' maps


# Music assets
rp6502_asset(RPPacMan pacman00 music/tracks/PacManCE_00.BIN) #Normal game music PacManCE_00.BIN
rp6502_asset(RPPacMan pacman01 music/tracks/PacManCE_01.BIN) #Game start jingle PacManCE_01.BIN
rp6502_asset(RPPacMan pacman02 music/tracks/PacManCE_02.BIN) #Title screen music PacManCE_02.BIN
rp6502_asset(RPPacMan pacman03 music/tracks/PacManCE_03.BIN) #Ranking music PacManCE_03.BIN
rp6502_asset(RPPacMan pacman04 music/tracks/PacManCE_04.BIN) #Extra mode game music PacManCE_04.BIN
rp6502_asset(RPPacMan pacman05 music/tracks/PacManCE_05.BIN) #Results screen music PacManCE_05.BIN (also used for Congrats screen)
rp6502_asset(RPPacMan pacman06 music/tracks/PacManCE_06.BIN) #Unused music, included for fun PacManCE_06.BIN
rp6502_asset(RPPacMan pacman07 music/tracks/PacManCE_07.BIN) #Options screen music PacManCE_07.BIN

# SFX-channel assets (reserved OPL2 channel 5 only -- see opl2_translate.py's
# sfx profile and opl.c's sfx_play/sfx_set_ambient). The two ambient loops
# (normal/frightened) play whenever no one-shot stinger is active.
rp6502_asset(RPPacMan sfxdeath      music/tracks/PacManCE_12.BIN) # Pac-Man dying
rp6502_asset(RPPacMan sfxpellet     music/tracks/PacManCE_13.BIN) # eating a pellet
rp6502_asset(RPPacMan sfxghosteat   music/tracks/PacManCE_15.BIN) # eating a ghost
rp6502_asset(RPPacMan sfxmazeup     music/tracks/PacManCE_16.BIN) # maze updates after a prize is collected
rp6502_asset(RPPacMan sfxprizeplace music/tracks/PacManCE_18.BIN) # a prize is placed on the map
rp6502_asset(RPPacMan sfxextralife  music/tracks/PacManCE_19.BIN) # extra life awarded
rp6502_asset(RPPacMan sfxfrightened music/tracks/PacManCE_20.BIN) # ambient loop while ghosts are vulnerable
rp6502_asset(RPPacMan sfxnormal     music/tracks/PacManCE_21.BIN) # ambient loop during normal play

rp6502_executable(RPPacMan DATA default RESET default)

target_sources(RPPacMan PRIVATE
    src/main.c
    src/tile_mode2.c
    src/sprite_mode5.c
    src/input.c
    src/player.c
    src/ghost.c
    src/prizes.c
    src/opl.c
    src/title_anim.c
    src/results.c
    src/hiscores.c
    src/congrats.c
    src/pause.c
    src/options.c
    src/soundtest.c
    src/countdown.c
)

enable_testing()
add_subdirectory(tests)
