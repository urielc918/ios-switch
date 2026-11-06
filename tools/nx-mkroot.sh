#!/bin/bash
# Build the iOS 16 root image for the Switch (iPhone 8 / D20AP, 16.7.16 20H392):
# tools/mkiosroot.sh (OS + SystemOS + AppOS -> one HFS+ volume), then the
# topaz bring-up tweaks (tools/sweet-root-tweaks.py), then a read-only fsck.
#
#   tools/nx-mkroot.sh [OUT.img] [SIZE]
#     default OUT research/ipsw-8/ios16-root-d20ap-20H392.img, SIZE 16G
#
# Uses sudo (apfs-fuse, loop mounts, root-owned files): run it in a terminal
# where sudo can ask for the password. Writes only OUT and temporary mounts.
set -euo pipefail
cd "$(dirname "$0")/.."
ipsw=research/ipsw-8
out=${1:-$ipsw/ios16-root-d20ap-20H392.img}
size=${2:-16G}
os=$ipsw/087-86684-044.dmg sysos=$ipsw/087-86580-047.dmg appos=$ipsw/087-86715-047.dmg
for f in "$os" "$sysos" "$appos"; do [[ -s $f ]] || { echo "missing $f" >&2; exit 1; }; done
[[ ! -e $out ]] || { echo "refusing to overwrite $out" >&2; exit 1; }

sudo -v
tools/mkiosroot.sh "$os" "$sysos" "$appos" "$out" "$size"

mnt=$(mktemp -d)
trap 'mountpoint -q "$mnt" && sudo umount "$mnt"; rmdir "$mnt" 2>/dev/null || true' EXIT
# A freshly built volume can still be flagged dirty; Linux then mounts it read-only.
sudo fsck.hfsplus -f -y "$out" | grep -v 'Illegal name' || true
sudo mount -t hfsplus -o loop,force "$out" "$mnt"
sudo python3 tools/sweet-root-tweaks.py "$mnt"
df -h "$mnt"
sudo umount "$mnt"
fsck.hfsplus -f -n "$out"
sha256sum "$out" | tee "$out.sha256"
