#!/bin/bash
# Build the Switch BL33 shim: out/bl33.bin, loaded by hekate's L4T launcher
# at 0xAA000000. Needs clang + ld.lld + llvm-objcopy (no GNU cross tools).
set -euo pipefail
cd "$(dirname "$0")"
CC="clang --target=aarch64-none-elf -mcpu=cortex-a57"
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align
	-fno-stack-protector -fno-builtin -Wall -Wextra -Werror -I. -I../shim
	-DFB_BASE=0xF0000000UL -DFB_W=720 -DFB_H=1280 -DSCALE=2"
mkdir -p out
objs=()
for f in start.S main.c display.c i2c.c sdmmc.c loader.c ../shim/console.c; do
	o=out/$(basename "${f%.*}").o
	$CC $CFLAGS -c "$f" -o "$o"
	objs+=("$o")
done
ld.lld -T shim.ld --no-pie --build-id=none -o out/shim.elf "${objs[@]}"
llvm-objcopy -O binary out/shim.elf out/bl33.bin
# Pad to _end so .bss and the stack exist as loaded (zeroed) bytes.
end=$((16#$(llvm-nm out/shim.elf | awk '$3=="_end"{print $1}')))
truncate -s $((end - 0xAA000000)) out/bl33.bin
# Optional xnu payload (tools/mkpayload.py) as $1: appended at the first 4K
# boundary after _end, where loader.c looks for it. hekate loads the whole
# file; it must end below 0xC7000000 (hekate's eMMC cache).
if [ -n "${1:-}" ]; then
	truncate -s $(( (end - 0xAA000000 + 0xfff) & ~0xfff )) out/bl33.bin
	cat "$1" >> out/bl33.bin
	total=$(stat -c %s out/bl33.bin)
	(( 0xAA000000 + total <= 0xC7000000 )) || { echo "error: bl33.bin too big ($total bytes)" >&2; exit 1; }
fi
ls -l out/bl33.bin
