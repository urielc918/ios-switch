#!/bin/bash
# Replace the switchroot Android partitions on the user's Switch SD card with
#   3  ios      Apple HFS+  627179520 .. 906067967  (~133 GiB, the iOS root)
#   4  ioslog   Linux data  906067968 .. 906100735  (16 MiB, kernel console log)
# and nothing else. Partitions 1 (hos_data), 2 (l4t / Ubuntu) and 10 (emummc)
# keep their numbers, sectors and contents. The hybrid MBR (FAT32 0x0C + emuMMC
# 0xE0 + 0xEE, which hekate uses to find the emuMMC) must stay byte-identical:
# it is checked before and after and restored from the backup if sgdisk
# changed it.
#
#   sudo tools/nx-sd-partition.sh /dev/sdX            (dry run: checks only)
#   sudo tools/nx-sd-partition.sh /dev/sdX --apply    (backup + change)
#
# Refuses to do anything unless the card matches the layout read on
# 2026-11-05 exactly (names, starts and sizes of all 10 partitions).
set -euo pipefail
dev=${1:?usage: nx-sd-partition.sh /dev/sdX [--apply]}
apply=${2:-}
[[ $(id -u) == 0 ]] || { echo "run with sudo" >&2; exit 1; }
cd "$(dirname "$0")/.."
name=${dev#/dev/}

# number name start size  (512-byte sectors), as read from sysfs on 2026-11-05
expected='1 hos_data 32768 360808448
2 l4t 360841216 266338304
3 boot 627179520 131072
4 recovery 627310592 131072
5 dtb 627441664 2048
6 misc 627443712 6144
7 cache 627449856 122880
8 super 627572736 12189696
9 userdata 639762432 266338304
10 emummc 906100736 61143040'
actual=$(for n in 1 2 3 4 5 6 7 8 9 10; do
	p=/sys/block/$name/$name$n
	[[ -d $p ]] || { echo "missing partition $n"; continue; }
	echo "$n $(lsblk -no PARTLABEL "/dev/$name$n") $(cat "$p/start") $(cat "$p/size")"
done)
[[ $(cat "/sys/block/$name/size") == 967245824 ]] || { echo "unexpected disk size" >&2; exit 1; }
if [[ $actual != "$expected" ]]; then
	echo "layout differs from the one this script was written for; not touching anything:" >&2
	diff <(echo "$expected") <(echo "$actual") >&2 || true
	exit 1
fi
[[ ! -e /sys/block/$name/${name}11 ]] || { echo "unexpected partition 11" >&2; exit 1; }
if grep -q "^/dev/$name" /proc/mounts; then
	echo "partitions of $dev are mounted; unmount them first:" >&2
	grep "^/dev/$name" /proc/mounts >&2
	exit 1
fi
echo "layout matches; emummc = partition 10 at sector 906100736 (stays untouched)"

mbr_table() { dd if="$dev" bs=1 skip=446 count=66 status=none | od -An -tx1 | tr -d ' \n'; }
mbr_before=$(mbr_table)
echo "hybrid MBR table: $mbr_before"

if [[ $apply != --apply ]]; then
	echo "dry run only; add --apply to change the card"
	exit 0
fi

stamp=$(date +%Y%m%d-%H%M%S)
mkdir -p research/sd-backup
sgdisk --backup="research/sd-backup/gpt-$stamp.bin" "$dev"
dd if="$dev" of="research/sd-backup/first1M-$stamp.bin" bs=1M count=1 status=none
sgdisk -p "$dev" > "research/sd-backup/sgdisk-p-$stamp.txt"
chown -R "${SUDO_UID:-0}:${SUDO_GID:-0}" research/sd-backup
echo "backup: research/sd-backup/*-$stamp.*"

sgdisk -d 9 -d 8 -d 7 -d 6 -d 5 -d 4 -d 3 \
	-n 3:627179520:906067967 -t 3:AF00 -c 3:ios \
	-n 4:906067968:906100735 -t 4:8300 -c 4:ioslog \
	"$dev"

mbr_after=$(mbr_table)
if [[ $mbr_after != "$mbr_before" ]]; then
	echo "sgdisk changed the hybrid MBR table: restoring the 66 bytes from the backup"
	dd if="research/sd-backup/first1M-$stamp.bin" of="$dev" bs=1 skip=446 seek=446 count=66 conv=notrunc status=none
	sync
	[[ $(mbr_table) == "$mbr_before" ]] || { echo "MBR restore failed: check $dev by hand" >&2; exit 1; }
fi
echo "hybrid MBR table unchanged: $(mbr_table)"
partprobe "$dev" 2>/dev/null || blockdev --rereadpt "$dev" || true
sleep 1
lsblk -o NAME,SIZE,PARTLABEL,PARTTYPENAME,FSTYPE,LABEL "$dev"
for n in 1 2 10; do
	echo "partition $n: $(lsblk -no PARTLABEL "/dev/$name$n") start $(cat "/sys/block/$name/$name$n/start") size $(cat "/sys/block/$name/$name$n/size")"
done
