# ios-switch: handoff notes

Goal: bring up XNU (xnu-8796, iOS 16 generation) on a Nintendo Switch
(rev 0 / Erista, Tegra X1 T210), following the dipa-studio/ios-sweet
approach. Status as of 2026-11-05: **design and fact-gathering done, no
Switch-specific code written yet.**

## Where things are

- Repo: `/home/j/2tbstorage/ios-switch` (git, branch `main`).
  `/home/j/ios-switch` is meant to be a bind mount of it (not active yet:
  `sudo mount --bind /home/j/2tbstorage/ios-switch /home/j/ios-switch`).
- Commit 1 imports **MakrSas/ios-topaz** `e4772911e116` as the base. It is
  a fork of dipa-studio/ios-sweet `45263bee51df` with an ARMv8.0 and
  4 KiB-page kernel (patches 0081, 0140, 0142), which the A57 needs.
  Upstream READMEs/NOTES are in `docs/upstream/`.
- `work/` (git-ignored) holds reference material:
  - `work/upstream/{ios-sweet,ios-topaz,ios-sailfish,ios-a5xelte,hekate}`
  - `work/xnu-src/xnu`: xnu-8796.141.3 with darwin-xnu-build's patches and
    the full topaz series applied, committed as git baseline (`HEAD` =
    patched tree, `HEAD~1` = pristine). New patches should be made as diffs
    against `HEAD` and must pass `git apply --check` there: **CI silently
    skips patches that don't apply.** (0031 and 0041 skip locally only
    because CI stages HFS sources first; expected.)
- hekate 6.5.4 payload: session scratchpad (re-download from CTCaer/hekate
  releases if gone). Sent to the Switch with `fusee-nano` (installed).

## Machine constraints

- Root disk has ~2.4 GB free; do all large work on `/home/j/2tbstorage`.
- No `qemu-system-aarch64`, no `gh`, no aarch64 gcc. `clang` 22 + `ld.lld`
  are present: build the shim with `--target=aarch64-none-elf`.
- XNU itself only builds on macOS (GitHub Actions `macos-14`, see
  `.github/workflows/xnu.yml`). Pushing/running CI needs the user's OK.
- Switch USB: when it was low on battery the PC reported over-current on
  several ports. Charge it on the official adapter before RCM work.

## Decisions

- **Firmware identity: iPhone 8 (iPhone10,1, 750x1334 @2x)**, as in
  ios-sailfish. Close to the 720x1280 panel (scale ~0.96 both axes), its
  userland is reported ARMv8.0-clean, and it is a 2 GB device like the
  memory we can give XNU.
- **Boot path:** RCM -> hekate -> L4T launcher with switchroot's `bl31.bin`
  (ATF, provides PSCI) and **our shim as `bl33.bin`** (instead of u-boot).
- Keep XNU `MACHINE_CONFIG=SWEET` (staging scripts check for it); add an
  `NX` define plus `TOPAZ_4K` and `-mcpu=cortex-a57`.

## Hardware facts gathered (T210 / Erista)

Boot handoff (hekate `bootloader/l4t/l4t.c`):
- `bl33.bin` is loaded at **0xAA000000** and entered at **EL2**
  (`SPSR_EL2T`). Shim must drop to EL1. ATF also starts secondaries at
  EL2, so XNU's `sweet_secondary_entry` needs the same drop.
- hekate patches BL33 only if `base + u32@(base+0x10)` holds FDT magic:
  keep that header word pointing at something that isn't FDT magic.
- `sld_type` defaults to 1: **display stays on** at handoff.
- **SD card is powered off** (`sd_end()`) at handoff: cold init needed.
- TZDRAM at 0xFF800000/0xFFF00000; GPU/NVDEC/TSEC carveouts just below
  ~0xFF700000. DTB area 0xA8000000. Plan: give XNU the upper bank
  **0x1_00000000 + 2 GiB**; shim, payload staging and framebuffer stay in
  the low bank.

CPU / interrupts:
- 4x Cortex-A57, ARMv8.0, **no 16K granule**, no LSE/PAN. MPIDR Aff0 0-3.
- **GICv2 (GIC-400)**: GICD 0x50041000, GICC 0x50042000. XNU's SWEET code
  is GICv3-only via system registers in three places: `pexpert/arm/pe_fiq.c`
  (init), `osfmk/arm64/sleh.c` `sleh_irq` (ack/EOI),
  `iokit/Kernel/arm/SweetCPU.cpp` `signalCPU` (IPI via ICC_SGI1R).
  Replace with MMIO GICC_IAR/EOIR and GICD_SGIR. Virtual timer = INTID 27
  (ADT `virtual-timer-intid`). Timer 19.2 MHz.

Display (DC A at 0x54200000, register index << 2):
- Window A pitch mode, from hekate `di.inl` `_di_winA_pitch`:
  WINDOW_HEADER(0x42)=A_SELECT(1<<4); WIN_OPTIONS(0x700)=0 then
  WIN_ENABLE(1<<30); COLOR_DEPTH(0x703)=0xC (B8G8R8A8);
  POSITION(0x704)=0; H/V_INITIAL_DDA(0x707/0x708)=0;
  PRESCALED_SIZE(0x706)=(1280<<16)|(720*4); DDA_INC(0x709)=0x10001000;
  SIZE(0x705)=(1280<<16)|720; LINE_STRIDE(0x70A)=((720*2)<<16)|(720*4);
  BUFFER_CONTROL(0x702)=0; SURFACE_KIND(0x80B)=0; START_ADDR(0x800)=fb;
  H/V_OFFSET(0x806/0x808)=0; DISP_WIN_OPTIONS(0x402)=DSI_ENABLE(1<<29);
  DISPLAY_COMMAND(0x32)=1<<5; STATE_CONTROL(0x41)=GENERAL_UPDATE(1<<8)|
  WIN_A_UPDATE(1<<9), then GENERAL_ACT_REQ(1)|WIN_A_ACT_REQ(1<<1).
- hekate's own FB: 0xF5A00000, 720x1280x4, pixel = fb[x + y*720].
  Buffer is portrait; hold the Switch upright for portrait output.
  Orientation (0 vs 180 deg) still to confirm on hardware.

SD card (SDMMC1 at 0x700B0000, SDHCI + Tegra vendor regs):
- Card detect GPIO PZ1 (low = inserted), pinmux PZ1 0x280.
- Pinmux (base 0x70003000): CLK 0x00, CMD 0x04, DAT3..0 0x08-0x14;
  APB_MISC (0x70000000) SDMMC1_CLK_LPBK 0x8D4 = 1,
  SDMMC1_PAD_CFGPADCTRL 0xA98, VGPIO_MUX_SEL 0xB74 = 0.
- Power: PMC (0x7000E400) NO_IOPOWER 0x44 bit 12 pulse; card VDD = GPIO
  PE4 high (pinmux DMIC3_CLK 0xB4); PWR_DET_VAL 0xE4 bit 12 (3.3 V);
  IO rail = MAX77620 **LDO2** at 3.3 V: I2C5, addr 0x3C, reg 0x27 =
  (3<<6)|0x32.
- Clocks (CAR 0x60006000): SDMMC1 = L bank bit 14 (RST_DEV_L_SET/CLR
  0x300/0x304, CLK_ENB_L_SET/CLR 0x320/0x324); CLK_SOURCE_SDMMC1 0x150 =
  PLLP (src 0) div field 31 -> 24.7 MHz. Legacy TM clock: Y bank bit 1
  (RST 0x2A8, ENB 0x29C), CLK_SOURCE 0x694 = (4<<29)|66 (12 MHz).
- Vendor regs: VENCLKCTL 0x100 (trim [28:24] = 2 on T210, clear bit 2),
  IOTRIMCTL 0x1AC (clear bit 2), SDMEMCOMPPADCTL 0x1E0 (VREF_SEL [3:0]=7,
  E_INPUT_PWRD bit 31 during cal), AUTOCALCFG 0x1E4 (offsets
  pd=0x7D pu=0 at 3.3 V; ENABLE bit 29, START bit 31), AUTOCALSTS 0x1EC
  (ACTIVE bit 31), IOSPARE 0x1F0 (set bit 19). Cal timeout fallback:
  PAD_CFGPADCTRL = (v & 0xF8080FFF) | (0xC0C << 12).
- Card clock: ID mode 25 MHz source / 64 = ~386 kHz; default speed /1.
  Then standard SDHCI: host v4 + 64-bit addressing, SDMA or PIO.

I2C (base 0x7000C000; buses at +0, +0x400, +0x500, +0x700, +0x1000
(I2C5 = PMIC), +0x1100): normal mode, CMD_ADDR0 0x04, DATA1 0x0C,
CNFG 0x00 ((len-1)<<1 | 2<<12 | 1<<11 | read<<6, GO = 1<<9),
CONFIG_LOAD 0x8C (write 0x25, wait bit 0 clear), STATUS 0x1C (busy
bit 8, NOACK [3:0]). I2C5 clock: H bank bit 15, CLK_SOURCE 0x128.

Buttons: volume down GPIO X7, volume up X6 (active low; GPIO base
0x6000D000, port offset ((port>>2)<<8)+((port&3)<<2), CNF +0, OE +0x10,
OUT +0x20, IN +0x30). Power = MAX77620 ONOFFSTAT (0x15) bit 2.

Reboot: PMC_SCRATCH0 (0x50) bit 1 = reboot to RCM, then PMC_CNTRL (0x0)
bit 4 = main reset. Power off: MAX77620 ONOFFCNFG1 (0x41) bit 1.

## Next steps

1. **`shim-nx/`**: entry header at 0xAA000000, EL2->EL1, reprogram DC
   window A to a shim framebuffer, reuse `shim/console.c` + `font.h`
   (make FB_* overridable), PSCI version probe, GICv2 + vtimer IRQ test,
   then I2C5 + LDO2 + SDMMC1 cold init and read sector 0 (show MBR/GPT on
   screen). Build with clang/lld. Reboot-to-RCM on volume-down for a quick
   retry loop.
2. **`tools/mkadt-nx.py`** from `tools/mkadt.py`: iPhone 8 identity,
   4x A57 (`IONameMatch` for SweetCPU must change from `qcom,kryo470`),
   GICv2 regs, memory 0x1_00000000 + 2 GiB, `/nx-sdmmc` node.
3. **XNU patches `xnu/patches/02xx-nx-*`**: machine flags; GICv2;
   EL2 drop in `sweet_secondary_entry` (`osfmk/arm64/start.s`);
   framebuffer geometry (hard-coded 1080x2400 / 0x9c000000 in `start.s`,
   `arm_vm_init.c`, `sweet_early.c`, `SweetFramebuffer.cpp`, `SweetHID.cpp`);
   gate out the Qualcomm drivers in `iokit/KernelConfigTables.cpp`
   (SweetHIDButtons/Touch/Battery match on IOResources and would touch
   Qualcomm MMIO); SD block device modelled on `bsd/dev/sweet_ufs.c`
   (bdevsw/cdevsw, `rd=` root name, persistent log ring at the end of the
   partition: readable by putting the SD card in a PC).
4. Later: touch (STM FTS over I2C), Joy-Con rails, GPU.

Licensing: repo MIT (from ios-sweet), XNU patches APSL 2.0. hekate is
GPLv2: use it for register facts only, don't copy its code.
