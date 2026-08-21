cmake_minimum_required(VERSION 3.21)

include(${CMAKE_CURRENT_LIST_DIR}/tools/rp6502.cmake)

project(RPPacMan C CXX ASM)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

add_executable(RPPacMan)
rp6502_asset(RPPacMan help src/help.txt)

# XRAM Assets
rp6502_asset(RPPacMan 0x10000 images/Maze_tiles_4bpp.bin)
rp6502_asset(RPPacMan 0x11000 images/Maze_map.bin)
rp6502_asset(RPPacMan 0x11582 images/Sprites_RP6502_4bpp.bin)
rp6502_asset(RPPacMan 0x16402 images/Fonts_RP6502_4bpp.bin)
rp6502_asset(RPPacMan 0x19A82 images/Text_map.bin)
rp6502_asset(RPPacMan 0x19CDA images/R_Mazes.bin)
rp6502_asset(RPPacMan 0x1D970 images/title_map.bin)
rp6502_asset(RPPacMan 0x1DE20 images/Titles_RP6502_4bpp.bin)

# Music assets
rp6502_asset(RPPacMan pacman01 music/PACMAN01.BIN)
rp6502_asset(RPPacMan pacman02 music/PACMAN02.BIN)
rp6502_asset(RPPacMan pacman03 music/PacManCE_01.BIN)

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
)

enable_testing()
add_subdirectory(tests)
