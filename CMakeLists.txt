cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)

set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)

project(RPPacMan C CXX ASM)

add_executable(RPPacMan)
rp6502_asset(RPPacMan help src/help.txt)
# XRAM Assets
rp6502_asset(RPPacMan 0x10000 images/Maze_tiles_4bpp.bin)
rp6502_asset(RPPacMan 0x11000 images/Maze_map.bin)
rp6502_asset(RPPacMan 0x11582 images/Sprites_RP6502_4bpp.bin)
rp6502_asset(RPPacMan 0x16402 images/Fonts_RP6502_4bpp.bin)
rp6502_asset(RPPacMan 0x18082 images/Text_map.bin)
rp6502_asset(RPPacMan 0x182DA images/R_Mazes.bin)
rp6502_asset(RPPacMan 0x1BF70 images/title_map.bin)
rp6502_asset(RPPacMan 0x1C420 images/Titles_RP6502_4bpp.bin)

# Music assets
rp6502_asset(RPPacMan loderun2 music/LODERUN2.BIN)
rp6502_asset(RPPacMan pacman01 music/PACMAN01.BIN)
rp6502_asset(RPPacMan pacman02 music/PACMAN02.BIN)

rp6502_executable(RPPacMan DATA file RESET file)
target_sources(RPPacMan PRIVATE
    src/main.c
    src/tile_mode2.c
    src/sprite_mode5.c
    src/input.c
    src/player.c
    src/ghost.c
    src/prizes.c
    src/opl.c
)
