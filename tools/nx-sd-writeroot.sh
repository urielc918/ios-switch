#!/bin/bash
# Write the iOS root image into the SD card's "ios" partition (made by
# tools/nx-sd-partition.sh) and verify it.
#
#   sudo tools/nx-sd-writeroot.sh /dev/sdX [IMAGE]
#     default IMAGE research/ipsw-8/ios16-root-d20ap-20H392.img
#
# Only writes to partition 3 of /dev/sdX, and only if it is named "ios",
# starts at sector 627179520 and is large enough; the emummc partition
# (10, from sector 906100736) and everything else are never opened for writing.
set -euo pipefail
dev=${1:?usage: nx-sd-writeroot.sh /dev/sdX [IMAGE]}
[[ $(id -u) == 0 ]] || { echo "run with sudo" >&2; exit 1; }
cd "$(dirname "$0")/.."
img=${2:-research/ipsw-8/ios16-root-d20ap-20H392.img}
name=${dev#/dev/}
part=/dev/${name}3
sys=/sys/block/$name/${name}3

[[ -s $img ]] || { echo "no image $img" >&2; exit 1; }
[[ -d $sys ]] || { echo "no partition 3 on $dev" >&2; exit 1; }
label=$(lsblk -no PARTLABEL "$part")
start=$(cat "$sys/start") size=$(cat "$sys/size")
need=$(( $(stat -c %s "$img") / 512 ))
[[ $label == ios && $start == 627179520 ]] || { echo "$part is '$label' at $start, not ios at 627179520" >&2; exit 1; }
(( size >= need )) || { echo "ios partition ($size sectors) smaller than the image ($need)" >&2; exit 1; }
(( start + size <= 906100736 )) || { echo "ios partition would reach the emummc" >&2; exit 1; }
! grep -q "^$part " /proc/mounts || { echo "$part is mounted" >&2; exit 1; }
[[ $(lsblk -no PARTLABEL "/dev/${name}10") == emummc && $(cat "/sys/block/$name/${name}10/start") == 906100736 ]] ||
	{ echo "emummc is not where expected; stopping" >&2; exit 1; }

echo "writing $img ($need sectors) to $part (ios, $size sectors at $start)"
dd if="$img" of="$part" bs=4M conv=fsync status=progress
echo "verifying"
cmp -n "$(stat -c %s "$img")" "$img" "$part" && echo "verified: $part holds $img"
sha256sum "$img" | cut -c1-64
