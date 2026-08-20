include(EmuTest)

set(PACMAN_ROM $<TARGET_FILE_DIR:RPPacMan>/RPPacMan.rp6502)

pacman_add_emu_test(boot ROM ${PACMAN_ROM})
pacman_add_emu_test(gameplay ROM ${PACMAN_ROM})
