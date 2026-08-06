#ifndef CONSTANTS_H
#define CONSTANTS_H

// Screen dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Sprite data configuration
#define SPRITE_DATA_START       0x0000U             // Starting address in XRAM for sprite data

#define MAZE_TILES_DATA        (SPRITE_DATA_START)  // Address for maze tiles data
#define MAZE_TILES_DATA_SIZE   (0x1000U)            // Size of maze tiles data (4KB)
#define MAZE_TILES_SIZE_PX      8                   // Size of each maze tile in pixels

#define MAZE_MAP_DATA          (MAZE_TILES_DATA + MAZE_TILES_DATA_SIZE) // Address for maze map data
#define MAZE_MAP_DATA_SIZE     (0x0582U)            // Size of maze map data (4KB)
#define MAZE_MAP_WIDTH          47                  // Width of the maze map in tiles
#define MAZE_MAP_HEIGHT         30                  // Height of the maze map in tiles

#define SPRITE_DATA     (MAZE_MAP_DATA + MAZE_MAP_DATA_SIZE) // Address for player sprite data
#define SPRITE_DATA_SIZE (0x3100U)           // Size of player sprite data (256 bytes)
#define SPRITE_SIZE_PX   16                  // Size of player sprite in pixels
#define SPRITE_FRAME_SIZE       0x0080U             // 128 bytes per 16x16 4bpp frame

#define SPRITE_DATA_END        (SPRITE_DATA + SPRITE_DATA_SIZE) // End address of sprite data

// Palette configurations
#define MAZE_PALETTE_ADDR    0xFC00  // 16-color palette (32 bytes, 0xFC00-0xFC1F)
#define MAZE_PALETTE_SIZE    0x0020
#define PLAYER_PALETTE_ADDR  0xFC20  // 16-color palette (32 bytes, 0xFC20-0xFC3F)
#define PLAYER_PALETTE_SIZE  0x0020

// OPL2 sound chip configuration
#define OPL_XRAM_ADDR   0xFE00  // Native RIA OPL2 register page
#define OPL_SIZE        0x0100

// RIA input buffers are provided at fixed XRAM addresses.
#define GAMEPAD_INPUT   0xFF78  // 40 bytes for 4 gamepads
#define KEYBOARD_INPUT  0xFFA0  // 32 bytes keyboard bitfield

// Configs 
extern unsigned MAZE_CONFIG; // Maze configuration
extern unsigned PLAYER_CONFIG; // Player configuration

#endif // CONSTANTS_H