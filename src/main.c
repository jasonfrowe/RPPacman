#include <rp6502.h>
#include <stdio.h>
#include <stdbool.h>
#include "constants.h"

static bool init_graphics(void)
{
    // 320×240 canvas
    int rc;
    rc = xreg_vga_canvas(1);
    if (rc < 0) {
        return false;
    }


    return true;
}

uint8_t vsync_last = 0;

int main(void)
{
    puts("Hello from RPPacMan!");

    // Initialize input
    xreg(0, 0, 0, KEYBOARD_INPUT);
    xreg(0, 0, 2, GAMEPAD_INPUT);

    // Initialise graphics
    if (!init_graphics()) {
        return 1;
    }

    // Main loop
    while (true) {
        // 1. SYNC
        if (RIA.vsync == vsync_last) continue;
        vsync_last = RIA.vsync;

    }

    return 0;
}
