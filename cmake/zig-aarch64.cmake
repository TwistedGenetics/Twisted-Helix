# Cross toolchain for the 64-bit MPCs (Gen2): Zig's clang, glibc 2.31, Cortex-A55 baseline (as the MPC SDK).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cc.sh)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig-cxx.sh)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu.2.31)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu.2.31)
