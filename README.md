# ios-switch

XNU (xnu-8796, the iOS 16 kernel) on the **Nintendo Switch** (rev 0 /
Erista, Tegra X1 / T210), following the approach of
[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) and its
fork ios-topaz, which run XNU and the iOS 16 userland on Qualcomm phones.
The base is ios-topaz (ARMv8.0, 4 KiB-page kernel), imported in the first
commit. The original READMEs and notes are in `docs/upstream/`.

**Status (2026-11-05):** XNU and the iOS 16.7.16 userland run on the
Switch: Apple's launchd starts the iOS daemons from an HFS+ root on the SD
card, on all four CPU cores, and SpringBoard reaches the **lock screen** on
the Switch's panel (CPU-drawn, no GPU), operated with the **touchscreen
and the buttons** (power, volume; volume up + down = Home). The boot shim passes every
hardware test (EL2 entry, display take-over, PSCI 1.1, GICv2 timer
interrupts, SD card cold init and reads). Kernels are built by CI on
GitHub's macOS runners. Day-to-day notes, decisions and every hardware result:
[`NOTES.md`](NOTES.md).

## Boot chain

RCM → hekate → hekate's L4T launcher → switchroot's ATF (`bl31.bin`) →
**`shim-nx`** as `bl33.bin` (EL2 → EL1, display, SD power, XNU loader) →
XNU, with its payload (kernel, Apple device tree, RAM disk) appended to
`bl33.bin`.

## Layout

| Path | What |
|---|---|
| `shim-nx/` | Switch BL33 shim: diagnostics on screen, XNU loader |
| `tools/mkadt-nx.py` | Apple device tree for the Switch (iPhone 8 identity) |
| `tools/mkpayload.py` | Kernel + ADT (+ RAM disk) → payload |
| `tools/nx-sdcard.sh` | Everything above → SD card layout |
| `tools/rcm-test/` | Tiny RCM payload: proves code runs without a display |
| `xnu/patches/0001-0152`, `0900-0950` | ios-sweet / ios-topaz kernel patches |
| `xnu/patches/0153` | Xcode 16 `iig` compatibility (`IORPC.kernelContent`) |
| `xnu/patches/0960-0972` | Switch: machine config, early framebuffer, GICv2, secondary CPUs, Qualcomm drivers off, SD root device, SD console log, boot CPU, panic log, display, buttons, touchscreen |
| `shim/` | the topaz phone shim (shared console code) |

## Building

- Shim: `shim-nx/build.sh` (clang + ld.lld, no GNU cross tools).
- Kernel: the `xnu` GitHub Actions workflow (`macos-15`), or
  `tools/build-local.sh` on a Mac. Both build with `TOPAZ_4K=1 NX=1`.
- SD card: `tools/nx-sdcard.sh OUT KERNEL --init` gives a first test
  image whose userland is a tiny `init` that runs as PID 1. Copy OUT onto
  the card's FAT32 partition and pick "iOS NX" in hekate.

## Licences

Repository MIT (from ios-sweet); XNU patches APSL 2.0; see `CREDITS.md`.
hekate (GPLv2) was used as a reference for register facts only, none of
its code is copied.
