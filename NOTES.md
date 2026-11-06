# ios-switch: handoff notes

Goal: bring up XNU (xnu-8796, iOS 16 generation) on a Nintendo Switch
(rev 0 / Erista, Tegra X1 T210), following the dipa-studio/ios-sweet
approach. **Status 2026-11-05 evening: XNU boots on the Switch on all 4 cores to a
PID 1 init in userland** (see "Hardware log"). Earlier the same day: design and
fact-gathering done, no Switch-specific code written yet. Session 2 (same day) re-read hekate's
L4T launcher for the BL33 handoff and fixed the `shim-nx/` design (see
"Session 2" below). `shim-nx/` is now **written and builds, but has not
been run on hardware yet** (see "shim-nx status"). Session 3 added the
XNU loader to shim-nx and `tools/mkadt-nx.py` (see "Session 3").

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

## Session 2 (2026-11-05): handoff details and shim-nx design

Environment:
- The bind mount `/home/j/ios-switch` is now active (481 GB free there).
- The LLVM tools are installed (`llvm-objcopy`, `llvm-nm`, `llvm-readelf`,
  `llvm-objdump`), so the build needs no GNU binutils. The old
  `shim/build.sh` calls `aarch64-linux-gnu-gcc`, which isn't installed,
  so it can't be reused as-is.

More hekate L4T launcher facts (`bootloader/l4t/l4t.c`, `bdk/soc/hw_init.c`):
- ini keys it reads: `boot_prefixes` (required; if it's missing you get
  "Path missing"). `bl31.bin` and `bl33.bin` are loaded from that path.
  Optional keys: `uart_port`, `sld_type`, `ram_oc`, `ram_oc_vdd2`,
  `ram_oc_vddq`, `ram_oc_opt`. SC7-Entry, SC7-Exit and BPMP-FW always come
  from `bootloader/sys/l4t/`. On T210 the Minerva MTC table is also
  required ("Minerva missing" otherwise). Use the switchroot L4T files.
- BL33 env block: every ini key goes in as `key=val\n` text at
  **0xA9FC0000** (BL33_LOAD_BASE - 256K), followed by `autoboot`,
  `autoboot_list` and `loader_rev=7`. A valid block is marked by magic
  0x33334C42 at 0xA9FBFFFC. The shim could take its options from here
  (e.g. a `nx_` key in the ini).
- BL31 gets `FLAGS_PMC_NON_SECURE`, so the shim and XNU can write PMC
  directly (needed for the reboot-to-RCM bit) without going through an SMC.
- On T210 the BPMP firmware starts BL31, and `SYSCTR0` counters are reset
  just before that.
- Carveouts: every MC security carveout has `BOM_HI = 0`, i.e. all of them
  are in the low bank, just below TZDRAM (TZDRAM is 1 MiB at 0xFFF00000,
  or 8 MiB at 0xFF800000 if hekate is built with the NVDEC/TSEC carveouts).
  This confirms the plan to give XNU **0x1_00000000 + 2 GiB**, which no
  carveout touches. The shim should stay below ~0xFF000000.
- `hw_deinit(keep_display = sld_type)` keeps DC/host1x running when
  sld_type=1, then calls `minerva_deinit()` (DRAM to 204 MHz).
  `minerva_prep_boot_l4t()` (800 MHz) runs *before* that. Open question:
  what clock does DRAM actually run at when BL33 starts? Measure it with
  a memcpy benchmark in the shim.

Reusing parts of the old shim:
- `shim/console.h` hard-codes FB_BASE 0x5c000000 at 1080x2400, and
  `console.c` hard-codes SCALE 3. Wrap these in `#ifndef` so shim-nx can
  set 720x1280 and SCALE 2 (16 px glyphs, 44 columns). The shim's FB
  address isn't chosen yet. It must be in the low bank, below the
  carveouts and away from 0xA8000000-0xAB000000 (e.g. 0xF0000000).
- `shim/start.S` vectors and `exc_common` can be copied as-is. The Linux
  Image header and the PSCI CPU_ON stack logic cannot.
- `shim.c` `exc_handler` (the on-screen panic) is the model for the
  shim-nx version: replace `reset()` with reboot-to-RCM.

shim-nx design (implemented, see "shim-nx status" below):
- Files: `start.S` (header, EL2->EL1, vectors), `shim.ld` (linked at the
  absolute address **0xAA000000**: the load address is fixed, so no PIC
  is needed), `t210.h` (MMIO bases and helpers), `display.c`, `i2c.c`
  (I2C5 + MAX77620), `sdmmc.c`, `main.c`, `build.sh` (clang
  `--target=aarch64-none-elf`, `ld.lld`, `llvm-objcopy -O binary` ->
  `out/bl33.bin`).
- Header: `b _start`, then pad. The u32 at +0x10 must not make
  `0xAA000000 + val` hit FDT magic: set it to 0.
- EL2->EL1 (handle entry at EL1 as well, by checking CurrentEL):
  HCR_EL2 = RW; CNTHCTL_EL2 |= EL1PCEN|EL1PCTEN; CNTVOFF_EL2 = 0;
  VPIDR/VMPIDR from MIDR/MPIDR; CPTR_EL2 = 0x33FF; SCTLR_EL1 = safe RES1
  value; SPSR_EL2 = EL1h with DAIF masked; ELR_EL2 = el1 entry; eret.
  CPACR_EL1.FPEN = 3.
- Progress stages you can see on screen: (1) fill hekate's FB 0xF5A00000
  with one colour, which proves the shim runs at all; (2) move window A
  to the shim's own FB (register list above) and clear it to a second
  colour, which proves the DC programming works; (3) text console.
- Volume-down = GPIO port X (index 23), bit 7: IN register
  0x6000D000 + 0x50C + 0x30 = **0x6000D53C**, active low. hekate already
  set the buttons up as GPIO inputs. On press: PMC_SCRATCH0 |= 2, then
  PMC_CNTRL |= 1<<4 (RCM).
- PSCI: SMC with x0 = 0x84000000 (PSCI_VERSION), print the result.
- GICv2 test: GICD_CTLR = 1, ISENABLER0 bit 27, IPRIORITYR byte 27 = 0xA0;
  GICC_PMR = 0xF0, GICC_CTLR = 1; CNTV_TVAL = freq/10, CNTV_CTL = 1;
  unmask IRQ. Handler: IAR (GICC+0x0C) == 27 -> count++, re-arm, EOIR
  (GICC+0x10). This assumes ATF already put INTID 27 in Group 1 (NS);
  if no IRQs arrive, check that first. PPIs bypass the Tegra legacy
  interrupt controller; SPIs will have to go through it later.

## shim-nx status (2026-11-05)

`shim-nx/` builds with `./build.sh` (clang 22, `-Werror`) into
`out/bl33.bin` (~50 KB including .bss and the 32 KB stack). It has **not
been run on hardware yet**. How to deploy it and what each screen means:
`shim-nx/README.md` (hekate ini: `l4t=1`, `boot_prefixes=switchroot/ios/`).

- Files: `start.S` (header, EL2->EL1, vectors), `t210.h` (register map),
  `display.c` (window A take-over), `i2c.c` (I2C5, MAX77620), `sdmmc.c`
  (SD cold init + PIO single-block read), `main.c` (stages and tests).
  It uses `../shim/console.c` and `font.h`. `shim/console.h` and
  `console.c` now take `FB_BASE/FB_W/FB_H/FB_STRIDE/SCALE` via `-D`; the
  defaults are unchanged for sweet.
- Checked on the host: header bytes (`b _start`, 0 at +0x10, image size
  at +0x18), the link puts everything at 0xAA000000, there are no
  libc/`memset` calls, and the worst-case console output (8 env lines +
  4 GPT partitions) fits 720x1280 at SCALE 2 (39 rows).
- Choices made while writing it:
  - Shim FB at **0xF0000000**.
  - Delays use the T210 microsecond timer (TIMERUS 0x60005010), which
    hekate leaves running. If the screen stays green and nothing else
    happens, suspect that this timer is stopped (stage 1 waits 0.3 s on it).
  - Window A take-over only rewrites START_ADDR if the window already
    looks like hekate's (enabled, depth 0xC, 720x1280, pitch 2880,
    kind 0); otherwise it does the full program from the register list
    above. Holding **volume-up** skips the take-over (console drawn into
    hekate's FB).
  - SD: 1-bit bus, ~386 kHz while identifying, then 24.7 MHz, PIO, no
    host v4/DMA. The SD power-up order follows hekate
    (`_sdmmc_config_sdmmc1` + `sdmmc_init`): pads back to SPIO (port M
    0-5) -> NO_IOPOWER pulse -> GPIO E4 card VDD -> PWR_DET -> LDO2 0xF2
    -> pad slew 0x5xxxxxxx -> legacy TM clock -> SDMMC1 clock (PLLP/16.5)
    -> SWRST_ALL -> IOSPARE/IOTRIM/VENCLKCTL (trim 2, tap 4)/VREF 7
    -> autocal offsets 0x7D/0 -> autocal -> internal clock -> 3.3 V
    bus power -> card clock.
  - I2C5 is re-clocked only if CAR shows it disabled or held in reset
    (`i2cfix=` on screen).
- No memory benchmark: with the MMU off every access is Device memory,
  so the numbers wouldn't say anything about DRAM clock. Do it after
  adding page tables (see `shim/mmu.c`).

## Session 3 (2026-11-05): loader path and NX ADT

Boot path for XNU (decided): **the payload rides inside `bl33.bin`**,
as on topaz, where it rode behind the shim Image. hekate's `_l4t_sd_load`
reads the whole file at 0xAA000000, whatever its size, so the shim
needs no SD/FAT driver to boot XNU.
- hekate memory map (`bdk/memory_map.h`) while it loads: heap
  0x82000000-0x92000000, RAM disk buffer 0x95000000 (only for its own
  features), PSTORE 0xB0000000 (2 MiB, L4T panic log; overwriting it is
  harmless), **eMMC sector cache 0xC7000000**, SDMMC DMA buffers
  0xE5000000/0xEF000000/0xF0000000, Nyx buffers 0xED000000-0xF6xxxxxx,
  IPL FB 0xF5A00000. So `bl33.bin` must end below **0xC7000000** (~460 MiB
  total), and `shim-nx/build.sh` refuses a larger file. The shim FB at
  0xF0000000 is hekate's eMMC DMA buffer, which is free after handoff.
- `shim-nx/build.sh [PAYLOAD]` appends the payload at the first 4K
  boundary after `_end`. `shim-nx/loader.c`: `loader_find` (magic
  SWEETXNU there), `loader_check` (version, payload end < 0xC7000000,
  phys_base in the upper bank, kernel fits), `loader_boot` (copy segments
  to phys_base + (va - virt_base), ADT + boot_args behind the kernel as
  on topaz, video = the FB being scanned out (720x1280, stride 2880),
  disables INTID 27 and GICC, masks DAIF, IC IALLU, jumps with x0 =
  boot_args PA at EL1, MMU off).
- After the tests the shim waits: **VOL+ boots XNU**, VOL- = RCM. The
  hekate ini key `nx_autoboot=1` boots without waiting (read from the
  BL33 env with `env_get`).
- `shim/payload.h`: the payload and boot_args structs, moved out of
  `shim/loader.c` so both shims share them. The old loader still compiles
  (checked with clang for aarch64; GNU cross gcc isn't installed).
- `FB_BASE` in shim-nx is now the shim FB (0xF0000000). Stage 1 sets
  `con_fb` to hekate's FB explicitly.

iPhone 8 firmware: newest = **iOS 16.7.16 (20H392)** for iPhone10,1
(D20AP), the same build as topaz's iPhone X root, IPSW
`https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77002/0281BFC6-38ED-485A-A8BE-8871861174B1/iPhone_4.7_P3_16.7.16_20H392_Restore.ipsw`
(6.0 GB, "iPhone_4.7_P3", shared by D20AP/D201AP). `tools/remotezip.py`
fetched `Firmware/all_flash/DeviceTree.d20ap.im4p` (34,302 bytes) into
`research/ipsw-8/` (git-ignored). Its IM4P payload is **LZFSE** (`bvx2`,
compression info type 1, 184,572 bytes raw). There's no LZFSE decoder on
this machine, and building lzfse from GitHub was blocked by the
permission rules. To finish: install `lzfse` (AUR) or `pip install
pyliblzfse` with the user's OK, decode the OCTET STRING payload ->
`research/ipsw-8/d20ap.adt` -> `mkadt-nx.py --product
research/ipsw-8/d20ap.adt`. Optional for the first boots.
`mkiosroot.sh` itself is model-independent (it takes the IPSW's
OS/SystemOS/AppOS dmgs).

**iOS root for the Switch (started 2026-11-05 evening).** The user
chose to give the **switchroot Android partitions** on the SD card to iOS
(the card has no free space: FAT32 172 GB, Ubuntu 127 GB, Android
(several small + 5.8 GB + 127 GB), emuMMC 29.2 GB). Identify the Android
partitions by GPT name before changing anything, and show the user the
sgdisk commands first.
- D20AP 16.7.16 BuildManifest (`research/ipsw-8/BuildManifest.plist`):
  OS = `087-86684-044.dmg` (4.6 GB), Cryptex1,SystemOS = `087-86580-047.dmg`
  (2.7 GB), Cryptex1,AppOS = `087-86715-047.dmg` (10 MB, same file as in
  topaz's iPhone X IPSW), RestoreRamDisk 087-86719-047 / update
  087-86622-047. Downloading the three into `research/ipsw-8/`.
- Tools missing on this machine: apfs-fuse, hfsprogs (mkfs.hfsplus/
  fsck.hfsplus), gptfdisk (sgdisk). The kernel has `hfsplus.ko`. The user
  has to install them (pacman: gptfdisk; AUR: apfs-fuse, hfsprogs).

`tools/mkadt-nx.py OUT [--dump] [--product D20AP.adt] [--cpus N]`
(imports helpers from `mkadt.py`):
- Identity iPhone 8: `D20AP|iPhone10,1|nintendo,icosa|nvidia,tegra210|
  xiaomi,sweet|AppleARM`, model iPhone10,1, target D20/D20AP,
  display-scale 2, chip-id 0x8015, synthetic ECID 0x4E58000000000001.
  `xiaomi,sweet` stays because SweetPlatformExpert matches it.
- /chosen dram-base 0x1_00000000, dram-size 0x80000000.
- 4 CPUs, compatible `arm,cortex-a57|ARM,v8`, reg 0-3. **SweetCPU's
  IONameMatch (`qcom,kryo470`) must get `ARM,v8` in the NX patch.**
- `/arm-io` with SOC_BASE 0x50000000. `/arm-io/gic`: compatible
  `arm,gic-400`, reg = <GICD-SOC 0x1000 GICC-SOC 0x2000> (pe_fiq.c reads
  four u64s; for GICv2 the second region is the CPU interface),
  virtual-timer-intid 27.
- `/arm-io/disp0` canvas 750x1334; `/sweet-fbcon` reg <0xF0000000
  0x400000> 720x1280 stride 2880 (pe_serial.c `fbcon_probe` reads it, so
  the text console needs no XNU change).
- `/nx-sdmmc` reg <0x700B0000 0x200> for the planned SD driver.
- No wlan, no sweet-ufs. No home-button-type (iPhone 8 has a Home
  button). /product only with `--product` (the real D20AP tree from the
  iPhone 8 IPSW, not in the repo).

Checked on the host: a synthetic Mach-O (`__TEXT` + `__DATA` +
LC_UNIXTHREAD) -> `mkpayload.py` with the NX ADT -> `build.sh PAYLOAD`
-> a Python model of loader.c found the payload right after `_end`,
segments at 0x101004000/0x101008000, entry PA 0x101005000, ADT intact.
There is no real XNU kernel locally (it only builds on macOS CI).

What the SWEET XNU tree does at boot (from `work/xnu-src/xnu`, patched =
HEAD), and so what the NX patches have to deal with:
- GIC: `pexpert/arm/pe_fiq.c` `pe_init_fiq` finds the redistributor by
  GICR_TYPER, writes GICR_IGROUPR0/IPRIORITYR/ISENABLER0 and uses ICC_*
  system registers (GICv3 only). `sleh_irq` and `SweetCPU::signalCPU` are
  also GICv3 (see "Hardware facts").
- Early debug framebuffer is hard-coded: `osfmk/arm64/sweet_early.c`
  (0x5c000000 topaz / 0x9c000000 sweet, 1080x2400, stride 4320),
  `osfmk/arm64/start.s` (~lines 519-525, 687-715) and `arm_vm_init.c`
  (~1856, 1884) map it VA=PA with L2 blocks.
- The fbcon text console comes from the ADT (`/sweet-fbcon`), so it's fine.
- Root device: `sweet_ufs_rootdev()` (bsd/dev/sweet_ufs.c) runs only for
  `rd=ufsroot0`, and it also starts the log thread, sampler, touch, keys
  and USB. With `rd=md0` (RAM disk payload segment, `mkpayload.py
  --ramdisk`) none of that runs, so **the first NX target is a RAM disk
  root**.
- SweetFramebuffer.cpp `start` (AppleCLCD, matches `sweet,display` =
  our disp0) calls `topaz_modem_init`, `topaz_audio_init`,
  `sweet_busy_init`, and `topaz_adreno_init` (boot-arg gpu=1), and
  programs MDP/DSI registers: all Qualcomm MMIO. **NX must gate this**:
  either drop disp0 from the ADT for the first boots (no AppleCLCD, text
  console only) or patch SweetFramebuffer to a plain RAM framebuffer.
- KernelConfigTables: SweetHIDButtons, SweetHIDTouch and SweetBattery
  match IOResources, so they always start; SweetBattery calls
  `sweet_i2c1_init` and `sweet_cpufreq_max` (Qualcomm). They need an NX
  gate.

## XNU NX patches (2026-11-05, not built yet)

`xnu/patches/0960-0968-nx-*.patch` (0960-0967 first numbered 0200-0207, see "CI" for why they moved), made on branch `nx` of
`work/xnu-src/xnu` (`master` = pristine + topaz series, unchanged).
Checked: all eight apply in order with `git apply --check` on a `master`
worktree, and the result is identical to `nx`. **Not compiled**: XNU only
builds on macOS (CI or `tools/build-local.sh`). The GICv2 C block was
compiled on its own with stubs (`clang -Weverything`, no warnings).
Regenerate after editing the branch with
`git format-patch --start-number 200 master..nx` (then rename to the
names below).

Build switch: **`TOPAZ_4K=1 NX=1`** in the make environment ->
`-DTOPAZ_4K=1 -DNX_SWITCH=1` (`makedefs/MakeInc.def`). The CI workflow
(`env:`) and `tools/build-local.sh` now export both. Before this,
neither set TOPAZ_4K, so CI built the 16K topaz kernel, which the A57
can't run. SWEET.h `#error`s if NX is set without TOPAZ_4K. `-mcpu` stays
cortex-a53 (ARMv8.0, fine on the A57).

- **0960 machine config**: SWEET.h under NX_SWITCH: no `__ARM_AMP__`
  (one A57 cluster; `MAX_CPU_CLUSTERS` 1 in board_config.h),
  `NX_GIC_V2` instead of `HAS_GIC_V3`, `SWEET_VTIMER_INTID` 27, GICv2
  register offsets (`GICD_V2_*`, `GICC_V2_*`), and
  `SWEET_EARLY_FB_PA/WIDTH/HEIGHT/STRIDE/BLOCKS` (NX: 0xF0000000,
  720x1280, 2880, 2 blocks; topaz values kept for the 4K topaz build).
- **0961 early framebuffer**: start.s (white band on the bottom 32 lines,
  VA=PA 2 MB-block mapping in its own L2 table: FB in L1 slot 3, RAM
  in slot 4), sweet_early.c and arm_vm_init.c (`sweet_early_map_final`
  alias) use the SWEET_EARLY_FB_* constants instead of 0x5c000000/1080/
  2400/4320/6.
- **0962 GICv2**: pe_fiq.c `#if NX_GIC_V2` `pe_init_fiq`: maps GICD/GICC
  from `/arm-io/gic` reg (+ soc base), reads virtual-timer-intid, sets
  GICD_CTLR bit 0 once. On every CPU (banked): SGI/PPI priorities 0xA0,
  enables vtimer + SWEET_IPI_SGI (1), GICC_PMR 0xF0, BPR 0, GICC_CTLR 1.
  `nx_gic_ack/eoi/send_sgi` (protos.h). sleh.c `sleh_irq` NX branch: IAR
  from GICC (intid = iar & 0x3FF), EOIR with the full IAR. SweetCPU
  `signalCPU`: GICD_SGIR = (1 << Aff0) << 16 | SGI. 0966 replaces the
  assumption "GIC interface number = Aff0": each core records its own
  mask from the banked GICD_ITARGETSR0 in `pe_init_fiq` (logged as
  `NX GIC: CPU Aff0 N has GIC interface mask 0x..`), and
  `nx_gic_send_sgi(aff0, sgi)` uses it, falling back to 1 << Aff0.
- **0963 secondary EL2**: `sweet_secondary_entry` already drops EL2->EL1
  (topaz needed it too); under NX it skips the `ICC_SRE_EL2` write. The
  GIC-400 has no system-register interface, so the A57 would most likely
  trap there. PSCI CPU_ON goes through `smc #0` (fine for ATF).
- **0964 gate Qualcomm**: KernelConfigTables: SweetCPU IONameMatch
  `ARM,v8` under NX. AppleCLCD::start (SweetFramebuffer.cpp) under NX
  only runs `sweet_amfi_service_init` + `sweet_busy_init` and returns
  false. The rest is `#else`'d out (not just after a `return`, because of
  -Weverything -Werror's unreachable-code warning); no static helper or
  variable becomes unused. SweetBattery::start under NX: `fReal = false`
  (no cpufreq / i2c1), and `poll()` then never touches SPMI or I2C.
- Checked, no gating needed for the first boot: sweet_ufs/keys/touch/usb/
  log only start via `rd=ufsroot0`; topaz modem/audio/adreno, redwood
  wlan/gpu only from AppleCLCD; SweetHIDButtons/Touch don't touch MMIO.
  `sweet_amfi`, HFS, mockfs, AMFI policy are software.

- **0965 SD root** (`bsd/dev/nx_sdmmc.c`, + `bsd/conf/files`, +
  IOKitBSDInit `rd=sdroot0` -> `nx_sdmmc_rootdev()`, which panics if
  unavailable like the UFS path). Division of work: **shim-nx powers
  SDMMC1** (pads, LDO2, PMC, CAR clocks: `test_sd()` in main.c, which must
  stay before the loader). **XNU** does SWRST_ALL, re-applies the Tegra
  trims (trim 2, tap 4, VREF 7, autocal offsets 0x7D/0 + autocal),
  identifies the card (CMD0/8/ACMD41/2/3/9/7), switches to a **4-bit bus
  (ACMD6) at 24.7 MHz**, CMD16 for SDSC. Polled PIO only: CMD17/18 and
  CMD24/25 with block count + auto CMD12, up to 2048 blocks (1 MiB) per
  command; after every write CMD13 until the card is in TRAN state and
  ready. Partition: boot-arg `sdroot=NAME` (GPT name, default `ios`) or
  `sdroot=mbr:N` (MBR slot 1-4). Bounds checked in strategy and again
  per command. Writes only with `sdrw=1`. Devices `/dev/sdroot0`,
  `/dev/rsdroot0`, 512-byte sectors. Capacity from CSD v1/v2
  (host-tested: SDHC C_SIZE 60871 -> 62332928 blocks; SDSC 2 GB
  case right). For non-NX builds the file only has an ENXIO stub (avoids
  an empty translation unit under -Weverything). Syntax-checked with
  stub headers (`clang -Weverything`); the remaining warnings were stub
  artifacts (uint64_t is unsigned long there, the lock group macro).
  Not done yet: high-speed mode (CMD6, 50 MHz).
- **0967 SD console log** (boot-arg `sdlog=1`, `sdlogpart=` default
  `ioslog`, `sdlogsecs=` default 2): a thread copies the console ring
  every SWEET kernel keeps (`sweet_log_copy`/`sweet_log_position` in
  sweet_ufs.c, fed by `_cnputs`) into a 4 MiB shadow with the on-card
  layout and writes only the changed sectors, then the header sector.
  Format = topaz's SWEETLOG (header at 0, ring at +4096), so
  `tools/sweet-log.py` reads a `dd` of the partition unchanged. **Its own
  partition, unlike topaz** (which used the tail of the root partition):
  on an SD card the HFS image may fill the root partition, and its last
  sectors hold the alternate volume header. Needs >= 4 MiB + 4 KiB. Started
  from IOFindBSDRoot, so it also works with rd=md0. The card bring-up
  (`card_up`) is shared with the root device and runs once. Independent
  of `sdrw`: it only ever writes inside that partition (bounds checked
  per command). `tools/nx-sdcard.sh --log` adds `sdlog=1`.
- SD layout plan (needs the user's OK before anything is written to the
  card): keep hekate's FAT32 partition, add a GPT partition named `ios`
  with an HFS+ root image dd'd into it, boot with `rd=sdroot0`, plus a
  small (e.g. 8 MiB) GPT partition `ioslog` for the console log.
  Until then: RAM disk root (`rd=md0`).

`tools/nx-sdcard.sh OUTDIR KERNEL [--ramdisk ROOT.img] [--bl31 F]
[--cmdline ...] [--cpus N]` does the whole chain below (ADT with 4 CPUs,
payload, shim build) and writes an SD layout to OUTDIR
(`switchroot/ios/bl33.bin`, optional `bl31.bin`,
`bootloader/ini/ios-nx.ini`). It adds `cpus=N` (default 4 since the SMP test passed) and `rd=md0`
with a RAM disk, else `rd=sdroot0`. Tested with the synthetic kernel +
a 64 KiB dummy RAM disk. It writes no device.

**First kernel test = `--init`**: `tools/nx-sdcard.sh OUT KERNEL --init`
builds `tools/sweet-init.S` with the local clang (`-target
arm64-apple-ios16.0 -c` gives a Mach-O object; no Apple toolchain needed)
+ `tools/mkinit.py` (static, ad-hoc signed arm64 executable, 16,635
bytes) and uses it as the RAM disk with `rd=md0`. mockfs (topaz 0021)
serves the RAM disk itself as `/sbin/launchd`, so there's no filesystem
to build. Expected on screen: `*** SWEET userspace: init running as
PID 1 ***` + heartbeat. Tested with the synthetic kernel (RAM disk
segment 0x8000 at PA 0x101010000).

Static review of the NX build while CI ran (no compiler available here):
- Include chains are fine: `SweetCPU.cpp` -> pexpert.h -> pexpert/protos.h
  -> machine/protos.h -> `pexpert/arm/protos.h` (nx_gic_* prototypes);
  pe_fiq.c includes protos.h itself under NX_GIC_V2; sleh.c already
  includes it. `delay()` is usable from BSD code (sweet_ufs.c uses it).
- No implementation-defined system registers in the topaz/sweet code
  that the A57 lacks: the only raw `S3_*` access is ACNTVCT_EL0 under
  `HAS_ACNTVCT` (Apple SoCs only, not SWEET). The A57 is ARMv8.0 like the
  A53, so topaz 0081 (no PAN, PFZ without CASA, userland LSE/LDAPR
  emulation) covers it too.
- Designated initializer for IORPC in IOUserServer.cpp lists
  `.kernelContent` last, as declared (C++ requires the order).

First NX kernel boot (once CI has built one):
`tools/mkadt-nx.py nx.adt` ->
`tools/mkpayload.py KERNEL nx.adt payload.bin --ramdisk ROOT.img
--cmdline "debug=0xc cpus=1 keepsyms=1 -noprogress rd=md0"` ->
`shim-nx/build.sh payload.bin` -> SD `switchroot/ios/bl33.bin`. The
first goal is the text console on the panel. Start with cpus=1, then
cpus=4 to test PSCI + the EL2 drop + SGIs.

## CI (2026-11-05)

- Repo: **https://github.com/urielc918/ios-switch** (private, created
  with `gh` as urielc918; push over HTTPS with
  `git -c credential.helper= -c credential.helper='!gh auth git-credential' push`.
  The machine's SSH key isn't registered on GitHub). The first push of
  `main` did not trigger the path-filtered workflow; start builds with
  `gh workflow run xnu -R urielc918/ios-switch --ref main`.
- Run 42121484163 was cancelled at once: **"The configuration
  'macos-14-arm64-us-xl' is not supported"**: GitHub retired the macOS 14
  images on 2026-11-02 (actions/runner-images). macos-15 arm64 has Xcode
  16.0-16.4 and 26.x, **no Xcode 15**. The workflow comment says that
  Xcode 16's `iig` emits `IORPC.kernelContent`, which xnu-8796 lacks.
- Fix: patch **0153-xcode16-iig-iorpc-kernelcontent**: `IORPC` gets
  `IORPCMessage * kernelContent` under `#ifdef KERNEL`, last member, as in
  apple-oss-distributions/xnu main, and `IOUserServer.cpp` fills it at
  both places the kernel builds an IORPC (`IOUserServer::server`,
  `IOUserServerUEXTTrap`), as upstream does. Workflow `runs-on: macos-15`
  (it already selects the oldest installed Xcode). Private repo: macOS
  minutes count 10x.
- **Run 42122592548 (macos-15, Xcode 16), failed after 5 min**
  (`tools/ci-check.sh` summary, full log in `logs/`): 212 patches
  applied. Skipped: **0205/0207** (NX SD), plus blacktop's own
  iobuffermemd_monterey/skywalk_sonoma/syntax_checker_sonoma/TrustCache
  (expected; their ventura/kas_info/machine_routines variants applied).
  0031 and 0041 DID apply in CI. One compile error, twice:
  `IOUserServer.cpp:3212/3361: no member 'kernelContent' in 'IORPC'`.
  - Cause 1: **patch order**. blacktop's build.sh applies
    `dxb/patches/*.patch` in glob order, and topaz's series includes
    **0900-0950** (HackiOS-derived), which sort after 02xx. The NX patches
    were made on a tree with 09xx already applied, and some 09xx touch
    `bsd/conf/files` and `IOKitBSDInit.cpp`. **Renumbered: NX patches are
    now 0960-0967** (0200->0960 ... 0207->0967). Rule: new patches must
    sort after every patch they were made on top of.
  - Cause 2: the kernel includes **`iokit/IOKit/IORPC.h`**, a separate
    regular file (same content) next to `iokit/DriverKit/IORPC.h`. 0153
    now patches both.
  - New local baseline that matches CI exactly: `work/xnu-ci` (git, tag
    `ci-base`) = the run's `xnu-buildtree` artifact source with 0153 and
    the NX patches reverse-applied. It differs from `work/xnu-src` master
    only in sources staged by the `.sh` scripts (pthread, bearssl, HFS)
    and a few files the build edits (MakeInc.def/.cmd, vfs_subr.c,
    make_symbol_aliasing.sh, Libsyscall.xcconfig).
  - Despite the failure, `make -k` compiled every other NX-touched file
    with Xcode 16's clang (pe_fiq.c, sleh.c, SweetCPU.cpp,
    SweetFramebuffer.cpp, SweetBattery.cpp, KernelConfigTables.cpp,
    start.s, arm_vm_init.c, sweet_early.c): warnings only, no errors.
    New NX-caused warning: `SweetFramebuffer.cpp:1191 unused function
    'sweet_cons_show'` (fine). nx_sdmmc.c wasn't compiled (0205 skipped).
  - Order check now done like CI: `master~1` (pristine) + every
    `xnu/patches/*.patch` in sorted order: only 0031/0041 skip locally
    (no staged HFS sources); 0153 and 0960-0967 apply.
- Run 42123957935 (renumbered series), attempt 1: **"The job was not
  acquired by Runner of type hosted even after multiple attempts"**
  (15 min queue, no minutes used; GitHub also warns about macOS arm64
  capacity). Re-ran with `gh run rerun`.
- **Run 42123957935 attempt 2: FIRST NX KERNEL BUILT** (4 min 51 s, warm
  ccache). 214 patches applied; only blacktop's superseded variants
  skipped (iobuffermemd_monterey, skywalk_sonoma, syntax_checker_sonoma,
  TrustCache). `kernel.development.sweet` 17,579,272 bytes, SHA-256
  e2087ea8...347b, in `work/ci-42123957935/` (git-ignored). Contains
  the NX strings (`NX GIC: GICv2 ...`, `NX SD: ...`, `NX SD LOG: ...`,
  `NX FB: no display driver yet`, `IOFindBSDRoot: NX SD root ...`), linked
  at 0xfffffff007004000 (TOPAZ_4K layout), entry 0xfffffff007454488,
  kernel_end 0xfffffff00855c000 (~37 MB mapped from phys_base).
- **First boot files**: `work/sdcard-init/` = `tools/nx-sdcard.sh
  work/sdcard-init work/ci-42123957935/kernel.development.sweet --init`:
  `switchroot/ios/bl33.bin` (17,034,384 bytes, SHA-256 7a227b37...88f5:
  shim + payload with sweet-init RAM disk at PA 0x102554000,
  cmdline `debug=0xc keepsyms=1 -noprogress cpus=1 rd=md0`) and
  `bootloader/ini/ios-nx.ini` ("[iOS NX]", now
  `boot_prefixes=/switchroot/ios/` with the leading slash, as in the
  proven entry). `bl31.bin` is already on the card in
  `switchroot/ios/`. **15:24 copied to the card** over hekate's USB
  mass storage (Nyx Tools -> USB Tools -> SD Card): `bl33.bin` +
  `ios-nx.ini` verified with cmp; the earlier diagnostics shim was kept as
  `switchroot/ios/bl33-shim.bin` (copy it over bl33.bin to go back).
- **20:0x FIRST XNU BOOT ON THE SWITCH** (kernel of CI run 42123957935,
  `--init` payload, cpus=1, hekate via OmniRCM -> "iOS NX" -> shim).
  Shim display: the tests filled all 39 rows, so the screen "stopped" at
  `PAYLOAD at=AA00D000 end=AB03EC90` (the loader's lines and its VOL+
  prompt were drawn below the edge). The user pressed VOL+ anyway.
  XNU then ran through early boot, VM, GICv2 init, timer and IOKit up to
  CPU matching, and **panicked in SweetCPU::startAll: `SWEET CPU: expected
  boot MPIDR 0x100 and logical CPU 0, got 0x0/0`**: topaz's check that it
  boots on gold core 0x100. Fixed in **0968-nx-boot-cpu** (NX expects
  0x0; CPU name "Cortex-A57"). The topology check in
  `SweetCPU::start` (Aff1 <= 1, Aff0 <= 3) already fits the Switch.
  Shim fix (commit 78ed511): the boot prompt goes on the test screen
  (one line per MBR entry now), and the loader gets a fresh screen.
- CI run 42160158910 (0968): success in 3 min 23 s, 215 patches
  applied. Kernel SHA-256 a42ab1d7..., in `work/ci-42160158910/`. New
  `bl33.bin` (shim 78ed511 + this kernel + sweet-init, SHA-256
  ad2c14b8...) copied to the card over hekate UMS and verified.
- **2026-11-05 ~20:15: USERLAND RUNS ON THE SWITCH.** Kernel 0968 (CI
  42160158910) + sweet-init RAM disk, cmdline `debug=0xc serial=3
  keepsyms=1 -noprogress cpus=1 rd=md0`, bl33.bin SHA-256 fc021df4...
  The user reports the last 10 lines on screen are `SWEET init: still
  alive`: the topaz test init is PID 1 (served by mockfs from the RAM
  disk) and its heartbeat keeps coming. So everything works on the
  hardware: the shim loader, XNU early boot at 4K pages on the A57, VM,
  GICv2 + vtimer (heartbeat = select() timeouts = timer IRQs), IOKit,
  SweetCPU (cpus=1), mockfs root, exec of a static arm64 Mach-O, syscalls
  and the console.
- **SMP WORKS: `cpus=4`**: the user reports `4/4 CPUs initialized, SGI1
  verified` (SweetCPU::startAll), then the init heartbeat. Confirmed on
  hardware: XNU starting the secondaries with PSCI CPU_ON (smc #0 to
  ATF), `sweet_secondary_entry` dropping EL2 -> EL1 without
  ICC_SRE_EL2 (0963), GICv2 SGIs between cores with per-core
  ITARGETSR0 masks (0962/0966). `nx-sdcard.sh` now defaults to cpus=4.
- cpus=4 test image (`work/sdcard-init-smp/`, bl33.bin SHA-256
  4941834a...) copied to the card; the working cpus=1 image is kept as
  `switchroot/ios/bl33-cpus1.bin`. (That time the card didn't auto-mount;
  `udisksctl mount -b /dev/sdc1` does it without sudo. The card stays
  mounted: the user asked not to unmount after writing.)
- 0968 boot: after VOL+ the screen went **black** and stayed black. Cause:
  the cmdline from `nx-sdcard.sh` lacked **`serial=3`**. The SWEET text
  console (fbcon from `/sweet-fbcon`) is registered as a serial device
  (pe_serial.c `register_serial_functions`), so kernel printf only reaches
  it with serial=3 (topaz's mkpayload default `debug=0xc serial=3 cpus=1
  keepsyms=1` had it). Without it only the early console and panics draw
  (that's why the 0x100 panic was visible). No panic this time = it
  probably got past SweetCPU. `nx-sdcard.sh` default is now `debug=0xc
  serial=3 keepsyms=1 -noprogress`. New bl33.bin SHA-256 fc021df4...
- 19:55 the fusee-nano send reported success but the user saw no hekate; the
  user then sent hekate with **OmniRCM** (their own tool) and it worked.
  fusee-nano had worked at 13:47, so either can be used.
- The CI ccache wrapper now bypasses ccache for `.s` inputs (as
  `tools/build-local.sh` already did): assembler files include the
  generated `assym.s`, which ccache doesn't hash, so stale struct offsets
  could survive a layout change (TOPAZ_4K/NX).

## Hardware log (2026-11-05)

- RCM works and is reachable: `0955:7321` "APX", device id
  `8085050500000018c705326401101062` (Erista).
- hekate 6.5.4 (`work/rcm/hekate_ctcaer_6.5.4.bin`, SHA-256
  95f6e933...8c24, **identical to the official GitHub release asset**)
  was sent 3 times with fusee-nano: "Sent 0x2d000 bytes, Smashed the
  stack" each time, but **the screen stayed black**, including once
  with the SD card out (it should show hekate's SD error screen). The
  Switch dropped off USB on its own 12 s to 70 s after each send, and
  once 5 s after entering RCM without any send.
- Around 13:14-13:15 a high-speed device on another PC port (`usb 3-3`)
  failed to enumerate (descriptor read -71/-110, address -62), likely
  the Switch on a bad cable/port. Another device on `usb 3-4` kept
  failing to enumerate the whole time (not the Switch).
- Port matters for permissions: on `usb 1-1` the RCM node gets
  `root:adbusers` + a user ACL (uaccess) and fusee-nano works as user j.
  On `usb 3-3` it was `crw------- root` with no ACL -> "Permission
  denied". Use port 1-1.
- **RCM ping test (13:26): payload execution confirmed.**
  `tools/rcm-test/rcm_ping.bin` (80 bytes, ARMv4T, PC-relative: waits
  3 s on TIMERUS, then PMC_SCRATCH0 bit 1 + PMC_CNTRL MAIN_RST). Sent at
  13:26:22.88; USB disconnect at 13:26:25.90 (3.0 s); re-enumerated as
  RCM (device 46) at 13:26:26.24. So: the exploit, BPMP execution,
  TIMERUS and reboot-to-RCM all work. hekate's black screen is therefore
  the panel/backlight or hekate dying in early init (DRAM, PMIC, display),
  not the transfer. Open question for the user: does Horizon show a picture?
- Horizon shows a picture (user, 13:27), so the panel is fine.
- **Likely cause of hekate's black screen: its low-battery gate.**
  `bootloader/main.c` `_check_low_battery()` (line 1158, called at
  startup): if MAX17050 AvgVCELL < 3100 mV (not charging) or < 3300 mV
  (BQ24193 says charging), hekate loops with the **display off**. Power
  press or a charge-status change shows a battery icon (bottom left) for
  15 s; when that times out **and not charging, it powers off** (matches
  the USB disconnects 12-70 s after each send). **VOL+ and VOL- held
  together** skips the check. Our shim-nx never runs this check, but it
  only runs after hekate, so the battery has to be above the threshold
  anyway (or hold both volume keys while hekate starts).
- 13:28:28 hekate re-sent (from the RCM state the ping left): user saw
  **no battery icon** after a Power press; disconnect at 13:28:43 was the
  user unplugging. Caveat: the battery loop polls Power with
  `btn_wait_timeout_single(0, ...)` every 250 ms, so a short tap can be
  missed. Retry with the charger connected (a charge-status change turns
  the screen on by itself and hekate won't power off while charging),
  Power held ~1 s, then VOL+ and VOL- together. If still black, hekate
  dies earlier: `hw_init()` (clocks, PMIC, SDRAM) runs before the battery
  check in `ipl_main`.
- User suspects a **faulty charger**.
- **bl31.bin source**: switchroot's L4T ATF from the Lakka 5.0 archive
  (`download.switchroot.org/lakka/Lakka-Switch.aarch64-5.0-devel-
  20240122151037-5f29696.tar`, plain tar, member `.../lakka/boot/bl31.bin`,
  45655 bytes, SHA-256 77b7f5c6...cda4, strings `[TF] bl31 exit`,
  `tegra_sip_fast`). Pulled out by streaming the tar through Python's
  `tarfile` (`r|`) and keeping only that member; the other switchroot
  downloads are 2 GB+ 7z archives.
- `work/sdcard/` (git-ignored) = SD files for the first shim test:
  hekate 6.5.4 zip's `bootloader/` (no .ini files in it, so the user's
  config stays), `switchroot/ios/{bl31,bl33}.bin` (shim without payload),
  `bootloader/ini/ios-nx.ini` (`[iOS NX shim] l4t=1
  boot_prefixes=switchroot/ios/`). Copy onto the card's FAT32 root.
- **14:05 FIRST SHIM-NX RUN ON HARDWARE** (hekate -> "iOS NX shim"):
  text console visible. `ENTRY el=00000002 x0=00000000` (ATF enters BL33
  at EL2, x0 = 0), `CPU midr=411FD071` (Cortex-A57 r1p1)
  `mpidr=80000000` (core 0, Aff0 0), `el=00000001` (EL2->EL1 drop works),
  `frq=0124F800` (CNTFRQ = 19.2 MHz = ADT timebase).
  `DC addr 40000000 start=F5A00000 depth=0000000C size=050002D0
  stride=05A00B40 kind=0`: window A exactly as predicted from hekate's
  `_di_winA_pitch` (enabled, B8G8R8A8, 1280x720 lines x px, pitch 2880,
  UV stride 1440, pitch-linear), so the take-over only rewrote START_ADDR
  and window A now scans out 0xF0000000 (blue screen seen). `ENV`
  parsed: `l4t=1`, `boot_prefixes=/switchroot/ios/`, `autoboot=2`,
  `autoboot_list=1`, `loader_rev=7`.
  `PSCI ver=00010001` (PSCI 1.1). **GICv2 + vtimer work**: `VTIMER
  ticks=0000000A us=000F4258` (10 IRQs in 1,000,024 us), spur 0, other 0,
  `GIC OK`: INTID 27 arrives as a Group 1 IRQ at EL1 through the MMIO
  GIC-400 CPU interface, which is what xnu patch 0962 assumes. **SD cold
  init works**: `SD ret=0 stage=0x64`, `i2cfix=0` (I2C5 still clocked
  after hekate), `ldo2=0xF2` (LDO2 3.3 V, normal mode), `calto=0`
  (autocal finished), `calsts=12181518`, `ocr=C0FF8000` (powered up,
  CCS=1: SDHC/SDXC), `RCA 0x5048`. **SD reads work**: `LBA0 sig=AA55`,
  hybrid MBR as made by hekate's partition manager: type 0x0C at LBA
  0x8000, 0x15818000 sectors (= sdc1 FAT32, 172 GB), a second entry
  (user read the type as 0xE0, LBA 0x36020000, 0x03A4F800 sectors: likely
  the Linux partition), type 0xEE protective at LBA 1; `GPT n=0x0A
  esz=0x80` (10 entries = the 10 partitions Linux sees). Partition names
  not reported.
  **Result: every shim-nx stage passed on the first hardware run.** The
  hardware facts that xnu patches 0960-0967 rely on are now confirmed on
  the device, not just read from hekate's source.
- **The user's SD card** (461 GB, 10 partitions): `sdc1` FAT32 "SWITCH SD"
  (atmosphere, emuMMC, Nintendo, hekate 6.5.4 already installed:
  `update.bin` identical to the release), `sdc2` ext4 "SWR-NOB" = switchroot
  Ubuntu Noble 5.1.2 (`bootloader/ini/L4T-noble.ini`,
  `switchroot/ubuntu-noble/`), sdc3-10 unlabeled (Android/emuMMC-style
  partitions). **Leave all of that alone.** The user has a backup of the
  emuNAND (said 14:04) in case the card has to be repartitioned for the
  `ios`/`ioslog` partitions later; still ask before repartitioning.
- 14:03 copied to the card (only new files): `switchroot/ios/bl31.bin`
  (= the card's `switchroot/ubuntu-noble/bl31.bin`, SHA-256 dea0ecd4...e2b4,
  Noble 5.1.2, newer than the Lakka one and proven on this unit),
  `switchroot/ios/bl33.bin` (shim-nx without payload, verified with cmp),
  `bootloader/ini/ios-nx.ini` ("[iOS NX shim]", `boot_prefixes=/switchroot/ios/`
  with a leading slash as in L4T-noble.ini). Then unmounted sdc1/sdc2.
- **13:47:11 hekate 6.5.4 shows its screen** (after ~30 min on the
  Switch's own power supply, same PC port 1-1, SD card out). So the earlier
  black screens were the low battery, not the payload or USB. Before an
  RCM session, charge the Switch first; if hekate is black, hold VOL+ and
  VOL- together. Until the Switch can show
  hekate's screen, nothing else can be tested on hardware. Next try: a
  known-good charger and cable, the PC's rear port `usb 1-1`, then
  re-send (the watcher one-liner in this session's history sends the
  moment `lsusb -d 0955:7321` matches).

## Next steps

1. ~~Run shim-nx on hardware~~ done 2026-11-05: all stages pass. Next: MMU + caches (from `shim/mmu.c`, 4K granule) to speed up the
   payload copy, and a DRAM benchmark. Secondaries are started by XNU
   itself through PSCI CPU_ON, so the EL2 drop belongs in XNU (step 3).
   SD in the shim is diagnostics only now; boot uses the payload in
   bl33.bin.
2. ~~`tools/mkadt-nx.py`~~ done (Session 3). Optional: `--product` with
   the real D20AP tree once the iPhone 8 IPSW DeviceTree is extracted.
3. **XNU patches**: 0960-0964 done (machine flags, early FB, GICv2,
   secondary EL2, Qualcomm gating; see "XNU NX patches"). Next: get a
   CI build (needs the user's OK to push), fix compile errors. Still
   open:
   - ~~SD block device~~ done (0965), ~~SD console log~~ done (0967).
     Still to add: high-speed mode.
   - A Tegra DC framebuffer driver for AppleCLCD (IOMobileFramebuffer for
     backboardd), replacing the NX early return in patch 0964.
   - SweetHID.cpp still assumes 1080x2400 (only matters once there is
     touch input).
4. Later: touch (STM FTS over I2C), Joy-Con rails, GPU.

Licensing: repo MIT (from ios-sweet), XNU patches APSL 2.0. hekate is
GPLv2: use it for register facts only, don't copy its code.
