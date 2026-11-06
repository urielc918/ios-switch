# shim-nx: Switch (T210 / Erista) BL33 bring-up shim

Runs as `bl33.bin` under hekate's L4T launcher (in place of u-boot), after
switchroot's ATF `bl31.bin`. It is entered at EL2 at 0xAA000000 and drops
to EL1.

## Build

    ./build.sh          # -> out/bl33.bin (clang/ld.lld/llvm-objcopy)

## SD card layout

    bootloader/sys/l4t/      sc7entry.bin, sc7exit.bin, bpmpfw.bin (ship with hekate)
    bootloader/sys/libsys_minerva.bso   (hekate; T210 needs the MTC table)
    switchroot/ios/bl31.bin  switchroot L4T ATF
    switchroot/ios/bl33.bin  out/bl33.bin

`bootloader/ini/ios-nx.ini`:

    [iOS NX shim]
    l4t=1
    boot_prefixes=/switchroot/ios/

`l4t=1` selects the L4T launcher. Every key in the section is also passed
to BL33 as `key=val` text at 0xA9FC0000; the shim prints them on screen.

## What the screen should show

The panel is portrait: hold the Switch upright.

1. **Dark green, full screen**: the shim is running and painting hekate's
   framebuffer (0xF5A00000).
2. **Red**: window A now scans out the shim's own framebuffer
   (0xF0000000). If the screen stays plain green or goes black, the window
   switch failed. Boot again **holding volume-up**: that skips the switch
   and draws everything into hekate's buffer (`DC skip` on screen).
   Start the entry by touch in the Nyx GUI (Launch), then hold volume-up
   until text appears. The shim checks it 0.3 s after the green fill. In
   hekate's text menu, volume-up moves the cursor, so press it only after
   the entry is selected.
3. Text:
   - `ENTRY el=2`, `CPU el=1`, MIDR/MPIDR, CNTFRQ.
   - `DC addr`/`DC FULL`/`DC skip`: whether window A only needed START_ADDR changed
     (`addr`, expected) or a full reprogram (`FULL`), plus what hekate left
     in the window registers.
   - `ENV`: the BL33 env lines from hekate.
   - `PSCI ver=` (expect 0x00010000 or higher), PSCI_FEATURES for CPU_ON
     and SYSTEM_OFF (0 = supported).
   - `VTIMER ticks=0000000A us=~000F4240` and `GIC OK`: ten virtual
     timer IRQs at 100 ms, delivered through GICv2.
   - `SD ret=0 stage=64` (0x64 = 100, i.e. done), the PMIC LDO2 readback
     (expect 0xF2), pad autocal status, OCR/RCA/CID, then `LBA0 sig=AA55`
     with MBR entries and, for GPT, the first four partitions.
   - On failure, `stage=` is the last SD command attempted and `intsts=`
     is the SDHCI interrupt status (normal | error << 16).
4. A **blue** `PANIC` screen shows ESR/ELR/FAR for any unexpected exception.

**Volume-down at any time** (also on the panic screen) reboots to RCM, for
the next `fusee-nano` try.
