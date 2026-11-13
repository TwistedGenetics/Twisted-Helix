# Cross toolchain for the 32-bit MPCs (Gen1: One / One+ / Retro / Live II / X / Force): Cortex-A17, NEON, hard-float.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cc.sh)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cxx.sh)
set(CMAKE_C_COMPILER_TARGET arm-linux-gnueabihf.2.31)
set(CMAKE_CXX_COMPILER_TARGET arm-linux-gnueabihf.2.31)
