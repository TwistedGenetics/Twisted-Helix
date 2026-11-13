#!/bin/sh
cpu=""
case "$*" in *aarch64*) cpu="-mcpu=cortex_a55";; *arm-linux*) cpu="-mcpu=cortex_a17";; esac
exec python3 -m ziglang cc $cpu "$@"
