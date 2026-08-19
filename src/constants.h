#ifndef CONSTANTS_H
#define CONSTANTS_H

// Screen dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define WORLD_WIDTH 376
#define WORLD_WIDTH_D2 (WORLD_WIDTH / 2)
#define WORLD_HEIGHT 240
#define WORLD_HEIGHT_D2 (WORLD_HEIGHT / 2)

// GHOST parameters
#define NGHOSTS 4                                   // Number of ghosts in the game
#define NPRIZES 2                                   // Number of prizes in the game
#define NMAZE_MUNCHERS 12                           // Number of maze transition munchers 
#define NSPARKLES 6                                 // Number of sparkle frames for prize animation 
#define NGHOST_SCORE_DISPLAYS 12                   // 3 score displays of 4 digits (12 slots: slot 0 for eaten ghost, slots 1-2 for eaten prizes)

// Sprite data configuration
#define SPRITE_DATA_START       0x0000U             // Starting address in XRAM for sprite data

#define MAZE_TILES_DATA        (SPRITE_DATA_START)  // Address for maze tiles data
#define MAZE_TILES_DATA_SIZE   (0x1000U)            // Size of maze tiles data (4096 bytes)
#define MAZE_TILES_SIZE_PX      8                   // Size of each maze tile in pixels

#define MAZE_MAP_DATA          (MAZE_TILES_DATA + MAZE_TILES_DATA_SIZE) // Address for maze map data
#define MAZE_MAP_DATA_SIZE     (0x0582U)            // Size of maze map data 1410 bytes (47 tiles * 30 tiles = 1410 bytes)
#define MAZE_MAP_WIDTH          47                  // Width of the maze map in tiles
#define MAZE_MAP_HEIGHT         30                  // Height of the maze map in tiles

#define SPRITE_DATA            (MAZE_MAP_DATA + MAZE_MAP_DATA_SIZE) // Address for player sprite data
#define SPRITE_DATA_SIZE       (0x4E80U)            // Size of player sprite data (157 frames * 16x16 = 20096 bytes)
#define SPRITE_SIZE_PX          16                  // Size of player sprite in pixels
#define SPRITE_FRAME_SIZE       0x0080U             // 128 bytes per 16x16 4bpp frame

#define FONT_TILES_DATA        (SPRITE_DATA + SPRITE_DATA_SIZE) // Address for font data
#define FONT_TILES_DATA_SIZE   (0x3680U)            // Size of font data (109 frames * 16*16 = 13952 bytes)

#define TEXT_MAP_DATA          (FONT_TILES_DATA + FONT_TILES_DATA_SIZE) // Address for text map data
#define TEXT_MAP_DATA_SIZE     (0x0258U)            // Size of text map data (1200 bytes, 40x15 characters)
#define TEXT_MAP_WIDTH          40                  // Width of the text map in characters
#define TEXT_MAP_HEIGHT         15                  // Height of the text map in characters

#define ALL_MAZE_MAPS_DATA     (TEXT_MAP_DATA + TEXT_MAP_DATA_SIZE) // Address for all maze maps data
#define ALL_MAZE_MAPS_DATA_SIZE  (0x3C96U)          // Size of all maze maps data (15510 bytes, 47x30 tiles * 11 maps)

// Title screen and menus
#define TITLE_MAP_DATA          (ALL_MAZE_MAPS_DATA + ALL_MAZE_MAPS_DATA_SIZE) // Address for title map data
#define TITLE_MAP_DATA_SIZE     (0x04B0U)           // Size of title map data (1200 bytes, 40x30 tiles)
#define TITLE_MAP_WIDTH         40                  // Width of the title map in tiles
#define TITLE_MAP_HEIGHT        30                  // Height of the title map in tiles

#define TITLE_TILES_DATA        (TITLE_MAP_DATA + TITLE_MAP_DATA_SIZE) // Address for title tiles data
#define TITLE_TILES_DATA_SIZE   (0x14E0U)           // Size of title tiles data (5248 bytes, 164 tiles * 8x8 pixels * 4bpp)

#define SPRITE_DATA_END        (TITLE_TILES_DATA + TITLE_TILES_DATA_SIZE) // End address of sprite data

// Palette configurations
#define MAZE_PALETTE_ADDR       0xFC00              // 16-color palette (32 bytes, 0xFC00-0xFC1F)
#define MAZE_PALETTE_SIZE       0x0020
#define PLAYER_PALETTE_ADDR     0xFC20              // 16-color palette (32 bytes, 0xFC20-0xFC3F)
#define PLAYER_PALETTE_SIZE     0x0020
#define FONT_PALETTE_ADDR       0xFC40              // 16-color palette (32 bytes, 0xFC40-0xFC5F)
#define FONT_PALETTE_SIZE       0x0020
#define TITLE_PALETTE_ADDR      0xFC60              // 16-color palette (32 bytes, 0xFC60-0xFC7F)
#define TITLE_PALETTE_SIZE      0x0020

// OPL2 sound chip configuration
#define OPL_XRAM_ADDR           0xFE00              // Native RIA OPL2 register page
#define OPL_SIZE                0x0100

// RIA input buffers are provided at fixed XRAM addresses.
#define GAMEPAD_INPUT           0xFF78              // 40 bytes for 4 gamepads
#define KEYBOARD_INPUT          0xFFA0              // 32 bytes keyboard bitfield

// Configs 
extern unsigned MAZE_CONFIG;                        // Maze configuration
extern unsigned PLAYER_CONFIG;                      // Player configuration
extern unsigned GHOST_CONFIG;                       // Ghost configuration
extern unsigned TEXT_MAP_CONFIG;                    // Text map configuration
extern unsigned TITLE_MAP_CONFIG;                   // Title map configuration
extern unsigned PRIZE_CONFIG;                       // Prize configuration
extern unsigned PRIZE_SPARKLE_CONFIG;                // Prize sparkle configuration
extern unsigned MAZE_MUNCHERS_CONFIG;              // Maze transition configuration
extern unsigned GHOST_SCORE_CONFIG;                  // Ghost score configuration

#endif // CONSTANTS_H