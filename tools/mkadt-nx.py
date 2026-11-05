#!/usr/bin/env python3
"""Build the Apple Device Tree for XNU on the Nintendo Switch (T210, Erista).

Same shape as tools/mkadt.py (topaz), with the Switch's hardware:
4x Cortex-A57, GICv2 (GIC-400), 720x1280 panel, SD card on SDMMC1, and
XNU's RAM in the upper DRAM bank. Identity: iPhone 8 (iPhone10,1, D20AP),
see NOTES.md "Decisions". Usage:

    mkadt-nx.py OUT.adt [--dump] [--product D20AP.adt] [--cpus N]

--product takes the real D20AP device tree (extracted from the iPhone 8
IPSW, kept out of git, like research/ipsw-x/d22ap.adt for topaz) and copies
its /product node; without it there is no /product node.
"""
import argparse, hashlib, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt  # noqa: E402
from mkadt import node, u32, u64, cstr, d22ap_product as product_props_from  # noqa: E402

# XNU's RAM: the whole upper bank of the 4 GiB Erista. Every hekate/ATF
# carve-out is in the low bank (MC BOM_HI = 0, see NOTES.md); the shim, its
# framebuffer and the payload staging area stay in the low bank as well.
PHYS_BASE = 0x100000000
MEM_SIZE = 0x80000000

# /arm-io children are relative to this; it must not be 0 (serial_init bails).
SOC_BASE = 0x50000000

# GIC-400: distributor and CPU interface. pe_fiq.c reads reg as four u64
# <base0 size0 base1 size1>; for GICv2 the second region is the CPU interface.
GICD, GICD_SIZE = 0x50041000, 0x1000
GICC, GICC_SIZE = 0x50042000, 0x2000
VTIMER_INTID = 27
TIMEBASE = 19200000

# shim-nx framebuffer: window A of display controller A scans it out.
FB, FB_W, FB_H = 0xF0000000, 720, 1280
FB_STRIDE = FB_W * 4
FB_SIZE = (FB_STRIDE * FB_H + 0xFFFFF) & ~0xFFFFF

# SD card host (SDHCI + Tegra vendor registers); for the planned XNU driver.
SDMMC1, SDMMC1_SIZE = 0x700B0000, 0x200

NX_ECID = 0x4E58000000000001  # 'NX' + 1, synthetic
BOOT_MANIFEST_HASH = hashlib.sha384(b'nx boot manifest').digest()


def build(seed=None, product_path=None, ncpus=4):
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(PHYS_BASE)),
        ('dram-size', u64(MEM_SIZE)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('firmware-version', cstr('nx-shim')),
        ('system-firmware-version', cstr('nx-shim')),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
        ('crypto-hash-method', cstr('sha2-384')),
        # iPhone 8: 750x1334 points at @2x
        ('display-scale', u32(2)),
        ('unique-chip-id', struct.pack('<Q', NX_ECID)),
        ('chip-id', u32(0x8015)),  # A11, as on the iPhone 8
        ('certificate-production-status', u32(0)),
        ('effective-production-status-ap', u32(0)),
        ('certificate-security-mode', u32(1)),
        ('effective-security-mode-sep', u32(1)),
    ], [node('memory-map', [ph()]),
        node('asmb', [ph(), ('lp-sip0', u64(0x6f)), ('lp-sip1', u32(1))])])

    # One SMP cluster of four A57s, MPIDR Aff0 0-3. SweetCPU matches
    # 'qcom,kryo470' today; the NX XNU patch has to add 'ARM,v8' (or
    # 'arm,cortex-a57') to its IONameMatch.
    cpu_nodes = []
    for core in range(ncpus):
        cpu_nodes.append(node(f'cpu{core}', [
            ph(),
            ('device_type', cstr('cpu')),
            ('compatible', cstr('arm,cortex-a57') + cstr('ARM,v8')),
            ('reg', u32(core)),  # MPIDR Aff1:Aff0
            ('cpu-id', u32(core)),
            ('cluster-id', u32(0)),
            ('cluster-type', u32(0)),
            ('die-cluster-id', u32(0)),
            ('cluster-core-id', u32(core)),
            ('l2-cache-id', u32(0)),
            ('l3-cache-id', u32(0)),
            ('state', cstr('running' if core == 0 else 'stopped')),
            ('timebase-frequency', u32(TIMEBASE)),
            ('clock-frequency', u32(1785000000)),
            ('bus-frequency', u32(100000000)),
            ('memory-frequency', u32(1600000000)),
            ('peripheral-frequency', u32(TIMEBASE)),
            ('fixed-frequency', u32(TIMEBASE)),
        ]))
    cpus = node('cpus', [ph(), ('#address-cells', u32(1)), ('#size-cells', u32(0))], cpu_nodes)

    gic = node('gic', [
        ph(),
        ('compatible', cstr('arm,gic-400') + cstr('arm,cortex-a15-gic')),
        ('interrupt-controller', b''),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
        ('reg', u64(GICD - SOC_BASE, GICD_SIZE, GICC - SOC_BASE, GICC_SIZE)),
    ])
    arm_io = node('arm-io', [
        ph(),
        ('device_type', cstr('sweet-io')),
        ('compatible', cstr('sweet-io') + cstr('arm-io')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
        ('ranges', u64(0, SOC_BASE, 0x100000000 - SOC_BASE)),
    ], [gic,
        node('wdt', [ph(), ('no-pmu', u32(1))]),
        # AppleCLCD matches sweet,display. With NX_SWITCH it only starts the
        # AMFI service that userland needs (patch 0204) and touches no display
        # hardware, so the node stays even before there is a Tegra DC driver.
        node('disp0', [
            ph(),
            ('device_type', cstr('display-subsystem')),
            ('compatible', cstr('sweet,display')),
            ('canvas-width', u32(750)),
            ('canvas-height', u32(1334)),
        ])])

    fbcon = node('sweet-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    sdmmc = node('nx-sdmmc', [
        ph(),
        ('compatible', cstr('nvidia,tegra210-sdhci')),
        ('reg', u64(SDMMC1, SDMMC1_SIZE)),
    ])

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])

    # The iPhone 8 has a Home button (home-button-type absent = 0); the
    # Switch maps it to the Joy-Con HOME button later.
    buttons = node('buttons', [
        ph(),
        ('button-names', cstr('volup') + cstr('voldown') + cstr('hold')),
    ])

    product = []
    if product_path:
        props = product_props_from(product_path)
        if props:
            props = [(k, cstr('sRGB') if k == 'artwork-display-gamut' else v) for k, v in props]
            product = [node('product', [ph()] + props)]

    # 'xiaomi,sweet' stays: SweetPlatformExpert matches it (MACHINE_CONFIG
    # SWEET is kept for the NX build, see NOTES.md).
    return node('device-tree', [
        ph(),
        ('compatible', cstr('D20AP') + cstr('iPhone10,1') + cstr('nintendo,icosa') +
         cstr('nvidia,tegra210') + cstr('xiaomi,sweet') + cstr('AppleARM')),
        ('model', cstr('iPhone10,1')),
        ('target-type', cstr('D20')),
        ('target-sub-type', cstr('D20AP')),
        ('manufacturer', cstr('Apple Inc.')),
        ('#address-cells', u32(2)),
        ('#size-cells', u32(2)),
    ], [chosen, defaults, cpus, arm_io, fbcon, buttons] + product + [sdmmc])


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--dump', action='store_true')
    ap.add_argument('--product', help='real D20AP device tree to copy /product from')
    ap.add_argument('--cpus', type=int, default=4)
    a = ap.parse_args()
    blob = adt.serialize(build(product_path=a.product, ncpus=a.cpus))
    open(a.out, 'wb').write(blob)
    if a.dump:
        adt.dump(adt.parse(blob)[0])
    print(f'{a.out}: {len(blob)} bytes', file=sys.stderr)
