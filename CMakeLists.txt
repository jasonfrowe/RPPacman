cmake_minimum_required(VERSION 3.18)

add_subdirectory(tools)

set(LLVM_MOS_PLATFORM rp6502)
find_package(llvm-mos-sdk REQUIRED)

project(RPPacMan C CXX ASM)

add_executable(RPPacMan)
rp6502_asset(RPPacMan help src/help.txt)
rp6502_executable(RPPacMan DATA file RESET file)
target_sources(RPPacMan PRIVATE
    src/main.c
)
