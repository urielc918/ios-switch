#!/bin/bash
# Assemble the SD card files for booting XNU on the Switch through hekate.
#
#   tools/nx-sdcard.sh OUTDIR KERNEL [--ramdisk ROOT.img] [--bl31 bl31.bin]
#                      [--cmdline "..."] [--cpus N] [--log]
#
# --log adds sdlog=1: the kernel writes its console log to the SD card's
# GPT partition "ioslog" (xnu patch 0207). Read it on a PC with
#   sudo dd if=/dev/<ioslog partition> of=log.bin bs=1M count=5
#   tools/sweet-log.py log.bin
#
# OUTDIR gets the SD card layout (copy its contents to the card's root):
#   switchroot/ios/bl33.bin       shim-nx + payload (kernel, ADT, RAM disk)
#   switchroot/ios/bl31.bin       switchroot's L4T ATF, if --bl31 was given
#   bootloader/ini/ios-nx.ini     hekate boot entry (l4t=1)
# hekate's own bootloader/ folder (sys/l4t/*, libsys_minerva.bso) must
# already be on the card. Nothing is written to any device by this script.
set -euo pipefail
TREE=$(cd "$(dirname "$0")/.." && pwd)

out=${1:?usage: nx-sdcard.sh OUTDIR KERNEL [options]}
kernel=${2:?usage: nx-sdcard.sh OUTDIR KERNEL [options]}
shift 2
ramdisk= bl31= cpus=1
cmdline="debug=0xc keepsyms=1 -noprogress"
while [ $# -gt 0 ]; do
	case "$1" in
	--ramdisk) ramdisk=$2; shift 2 ;;
	--bl31) bl31=$2; shift 2 ;;
	--cmdline) cmdline=$2; shift 2 ;;
	--cpus) cpus=$2; shift 2 ;;
	--log) cmdline="$cmdline sdlog=1"; shift ;;
	*) echo "unknown option $1" >&2; exit 1 ;;
	esac
done
# Root: the RAM disk if given, else the SD card partition "ios".
if [ -n "$ramdisk" ]; then
	cmdline="$cmdline cpus=$cpus rd=md0"
else
	cmdline="$cmdline cpus=$cpus rd=sdroot0"
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 "$TREE/tools/mkadt-nx.py" "$work/nx.adt" --cpus 4
python3 "$TREE/tools/mkpayload.py" "$kernel" "$work/nx.adt" "$work/payload.bin" \
	--cmdline "$cmdline" ${ramdisk:+--ramdisk "$ramdisk"}
"$TREE/shim-nx/build.sh" "$work/payload.bin"

mkdir -p "$out/switchroot/ios" "$out/bootloader/ini"
cp "$TREE/shim-nx/out/bl33.bin" "$out/switchroot/ios/bl33.bin"
if [ -n "$bl31" ]; then
	cp "$bl31" "$out/switchroot/ios/bl31.bin"
elif [ ! -e "$out/switchroot/ios/bl31.bin" ]; then
	echo "note: no bl31.bin; copy switchroot's L4T bl31.bin to switchroot/ios/" >&2
fi
cat > "$out/bootloader/ini/ios-nx.ini" <<'EOF'
[iOS NX]
l4t=1
boot_prefixes=switchroot/ios/
EOF
echo "cmdline: $cmdline"
ls -l "$out/switchroot/ios" "$out/bootloader/ini/ios-nx.ini"
