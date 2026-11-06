#!/bin/bash
# Assemble the SD card files for booting XNU on the Switch through hekate.
#
#   tools/nx-sdcard.sh OUTDIR KERNEL [--ramdisk ROOT.img] [--bl31 bl31.bin]
#                      [--cmdline "..."] [--cpus N] [--log] [--init] [--console]
#
# --console: with the SD root, keep the kernel text console drawn after
# iOS's first frame (default: sweetfb_noconsole=1, the GUI owns the screen;
# the console still goes to the SD log with --log).
# --init: the RAM disk is tools/sweet-init.S as a static Mach-O (built here
# with clang -target arm64-apple-ios16.0 + tools/mkinit.py). With rd=md0
# mockfs serves it as /sbin/launchd: the first test of the kernel reaching
# userland ("SWEET userspace: init running as PID 1", then a heartbeat).
#
# --log adds sdlog=1: the kernel writes its console log to the SD card's
# GPT partition "ioslog" (xnu patch 0967). Read it on a PC with
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
ramdisk= bl31= cpus=4 extra= init= console=
cmdline="debug=0xc serial=3 keepsyms=1 -noprogress"
while [ $# -gt 0 ]; do
	case "$1" in
	--ramdisk) ramdisk=$2; shift 2 ;;
	--bl31) bl31=$2; shift 2 ;;
	--cmdline) cmdline=$2; shift 2 ;;
	--cpus) cpus=$2; shift 2 ;;
	--log) extra="$extra sdlog=1"; shift ;;
	--init) init=1; shift ;;
	--console) console=1; shift ;;
	*) echo "unknown option $1" >&2; exit 1 ;;
	esac
done
cmdline="$cmdline$extra"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
if [ -n "$init" ]; then
	clang -target arm64-apple-ios16.0 -c "$TREE/tools/sweet-init.S" -o "$work/sweet-init.o"
	python3 "$TREE/tools/mkinit.py" "$work/sweet-init.o" "$work/sweet-init"
	ramdisk=$work/sweet-init
fi
# Root: the RAM disk if given, else the SD card partition "ios".
if [ -n "$ramdisk" ]; then
	cmdline="$cmdline cpus=$cpus rd=md0"
else
	# The iOS root, with topaz's userland boot-args (ios-topaz README):
	# writable root (rootrw=1, topaz 0031) on a writable device (sdrw=1);
	# unentitled_ios_sim_launch=1: this is XNU's macOS configuration, which only
	# execs iOS-platform binaries with it (else EXEC_EXIT_REASON_WRONG_PLATFORM);
	# ignition_level=1 (dyld finds the shared cache under /System/Library/Caches);
	# launchd's job cache without AMFI; no watchdog.
	cmdline="$cmdline cpus=$cpus rd=sdroot0 sdrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 launchd_unsecure_cache=1 wdt=-1 wdt_test=no_user_checkin"
	[ -n "$console" ] || cmdline="$cmdline sweetfb_noconsole=1"
	# (sweetfb_stayon=1 is no longer needed: the power button wakes the display, patch 0971.)
	# Touch: the Nyx-derived default mapping is rotated 180 degrees on this
	# Switch (a top-right tap arrived at the bottom left): flip both axes.
	# (touch orientation is the kernel default since patch 0973)
	# Clock (0973): the MAX77620 RTC on this unit counts from a 2000-01-01 base
	# (a held-power off with a charged battery keeps it, checked with hekate).
	# nxtimeoff = real UTC - RTC, measured 2026-11-06 (lock screen Nov 5 2:31 PM
	# with the old offset 847221840 at real 4:39 AM PST: +50880 s; +-1 min;
	# why the old one was 14 h 08 min off is open, see NOTES.md Session 4).
	# Like hekate's nyx.ini timeoffset, it must be re-measured after an RTC reset.
	# Cross-check: nxtimeoff = nyx.ini timeoffset (hex, local non-DST) + 28800 (PST).
	cmdline="$cmdline nxtimeoff=847272720"
fi
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
boot_prefixes=/switchroot/ios/
EOF
echo "cmdline: $cmdline"
ls -l "$out/switchroot/ios" "$out/bootloader/ini/ios-nx.ini"
