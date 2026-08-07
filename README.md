# Championship Pac-Man for the RP6502

This is a port of the classic Championship Pac-Man game to the RP6502 platform using the LLVM-MOS SDK. The game features the original maze layout, graphics, and gameplay mechanics, providing an authentic Pac-Man experience on the RP6502 hardware.


## Building the Game

Created a blank binary file for the text map data:

```bash
dd if=/dev/zero of=Text_map.bin bs=1 count=600
```

This will make a 600-byte file filled with zeros, which can be used as a placeholder for the text map data in the game.
