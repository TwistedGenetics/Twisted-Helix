#!/bin/sh
# zig c++ with the MPC CPU baselines. CMake passes --target=<triple> from CMAKE_CXX_COMPILER_TARGET.
cpu=""
case "$*" in *aarch64*) cpu="-mcpu=cortex_a55";; *arm-linux*) cpu="-mcpu=cortex_a17";; esac
exec python3 -m ziglang c++ $cpu "$@"
