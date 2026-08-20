if(CMAKE_HOST_SYSTEM_VERSION MATCHES "[Mm]icrosoft" OR CMAKE_HOST_WIN32)
    set(_emu_names rp6502-emu rp6502-emu.exe)
else()
    set(_emu_names rp6502-emu)
endif()
find_program(RP6502_EMU NAMES ${_emu_names}
             HINTS ${CMAKE_SOURCE_DIR}/tools NO_DEFAULT_PATH)
if(NOT RP6502_EMU)
    message(WARNING
        "No rp6502-emu, so the script tests will not be registered. Fetch it with:\n"
        "    cmake -P ${CMAKE_SOURCE_DIR}/tools/rp6502.cmake")
endif()

function(pacman_add_emu_test name)
    cmake_parse_arguments(E "" "ROM;TIMEOUT;SEED" "" ${ARGN})
    if(NOT RP6502_EMU)
        return()
    endif()
    if(NOT E_SEED)
        set(E_SEED 1)
    endif()
    if(NOT E_TIMEOUT)
        set(E_TIMEOUT 60)
    endif()
    set(_scratch ${CMAKE_CURRENT_BINARY_DIR}/scratch/${name})
    file(MAKE_DIRECTORY ${_scratch})

    set(_script ${CMAKE_CURRENT_LIST_DIR}/emu/${name}.txt)

    add_test(NAME emu.${name}
             COMMAND ${RP6502_EMU} --mute --seed ${E_SEED} --tmpdrive
                     --script ${_script} ${E_ROM}
             WORKING_DIRECTORY ${_scratch})
    set_tests_properties(emu.${name} PROPERTIES TIMEOUT ${E_TIMEOUT} LABELS emu)
endfunction()
