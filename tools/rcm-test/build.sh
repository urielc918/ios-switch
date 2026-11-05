#!/bin/bash
# Tiny RCM test payloads (BPMP, ARMv4T). Output in the same directory.
set -euo pipefail
cd "$(dirname "$0")"
clang --target=armv4t-none-eabi -c rcm_ping.S -o rcm_ping.o
ld.lld -Ttext=0x40010000 --build-id=none -o rcm_ping.elf rcm_ping.o
llvm-objcopy -O binary rcm_ping.elf rcm_ping.bin
rm -f rcm_ping.o
ls -l rcm_ping.bin
