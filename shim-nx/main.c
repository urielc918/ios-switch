/*
 * Switch (T210) BL33 bring-up shim. Stages, each visible on the panel:
 *   1. paint hekate's framebuffer          -> the shim runs at all
 *   2. move DC window A to our framebuffer -> display programming works
 *   3. text console: CPU/EL state, BL33 env, PSCI, GICv2 + virtual timer,
 *      SD card cold init and sector 0
 * Volume-down at any point (including the panic screen) reboots to RCM.
 */
#include "t210.h"
#include "console.h"
#include "display.h"
#include "i2c.h"
#include "sdmmc.h"

#define HEKATE_FB	0xF5A00000UL
#define SHIM_FB		0xF0000000UL
#define BL33_ENV	0xA9FC0000UL
#define BL33_ENV_MAGIC_ADDR (BL33_ENV - 4)
#define BL33_ENV_MAGIC	0x33334C42

#define DKGREEN	0xff004000U
#define DKBLUE	0xff000040U
#define GREY	0xff808080U

static const char *stage = "start";

static void __attribute__((noreturn)) reboot_rcm(void)
{
	set32(PMC_SCRATCH0, PMC_SCRATCH0_MODE_RCM);
	set32(PMC_CNTRL, PMC_CNTRL_MAIN_RST);
	for (;;)
		__asm__ volatile("wfi");
}

static void poll_rcm(void)
{
	if (vol_down_pressed())
		reboot_rcm();
}

static void __attribute__((noreturn)) idle(void)
{
	con_skip();
	con_str("", "VOL- = reboot to RCM");
	for (;;)
		poll_rcm();
}

static void fill(uintptr_t fb, uint32_t c)
{
	volatile uint32_t *p = (volatile uint32_t *)fb;
	for (int i = 0; i < FB_W * FB_H; i++)
		p[i] = c;
}

/* ---- exceptions / IRQ ---- */

static volatile unsigned vt_ticks, spurious, other_irq;
static uint64_t vt_period;

static void irq_handler(void)
{
	uint32_t iar = rd32(GICC_IAR);
	uint32_t id = iar & 0x3FF;
	if (id >= 1020) {
		spurious++;
		return;
	}
	if (id == INTID_VTIMER) {
		vt_ticks++;
		if (vt_ticks < 10)
			wsr("cntv_tval_el0", vt_period);
		else
			wsr("cntv_ctl_el0", 0);
	} else {
		other_irq = id;
	}
	wr32(GICC_EOIR, iar);
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	if (slot == 5) {	/* IRQ, current EL, SPx */
		irq_handler();
		return;
	}
	con_clear(BLUE);
	switch (slot & 3) {
	case 0: con_str("PANIC", "SYNC"); break;
	case 1: con_str("PANIC", "IRQ"); break;
	case 2: con_str("PANIC", "FIQ"); break;
	default: con_str("PANIC", "SERROR"); break;
	}
	con_hex2("SLOT", "n=", slot, 0, 0);
	con_str("STAGE", stage);
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("SPSR", rsr("spsr_el1"));
	con_hex("X0", regs[0]);
	con_hex("X1", regs[1]);
	con_hex("X30", regs[29]);
	idle();
}

/* ---- tests ---- */

static uint64_t smc(uint64_t fn, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	__asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory");
	return x0;
}

static void show_env(void)
{
	if (rd32(BL33_ENV_MAGIC_ADDR) != BL33_ENV_MAGIC) {
		con_hex2("ENV", "magic=", rd32(BL33_ENV_MAGIC_ADDR), 0, 0);
		return;
	}
	/* "key=val\n" lines; show each (truncated by the console). */
	char line[40];
	const volatile char *p = (const volatile char *)BL33_ENV;
	for (int n = 0; n < 8 && *p; n++) {
		int i = 0;
		while (*p && *p != '\n') {
			if (i < (int)sizeof(line) - 1)
				line[i++] = *p;
			p++;
		}
		line[i] = 0;
		if (*p)
			p++;
		con_str("ENV", line);
	}
}

static void test_gic(void)
{
	stage = "gic";
	con_hex2("GICD", "iidr=", rd32(GICD_IIDR), "typer=", rd32(GICD_TYPER));
	con_hex2("GICC", "iidr=", rd32(GICC_IIDR), "ctlr=", rd32(GICC_CTLR));

	wr32(GICD_CTLR, 1);
	wr8(GICD_IPRIORITYR + INTID_VTIMER, 0xA0);
	wr32(GICD_ISENABLER(0), BIT(INTID_VTIMER));
	wr32(GICC_PMR, 0xF0);
	wr32(GICC_BPR, 0);
	wr32(GICC_CTLR, 1);

	uint64_t frq = rsr("cntfrq_el0");
	if (!frq)
		frq = 19200000;
	vt_period = frq / 10;
	vt_ticks = 0;
	wsr("cntv_tval_el0", vt_period);
	wsr("cntv_ctl_el0", 1);
	isb();
	uint32_t t0 = usec();
	__asm__ volatile("msr daifclr, #2");
	while (vt_ticks < 10 && usec() - t0 < 3000000)
		poll_rcm();
	__asm__ volatile("msr daifset, #2");
	uint32_t dt = usec() - t0;
	wsr("cntv_ctl_el0", 0);
	con_hex2("VTIMER", "ticks=", vt_ticks, "us=", dt);
	con_hex2("", "spur=", spurious, "other=", other_irq);
	con_hex2("", "ctl=", (uint32_t)rsr("cntv_ctl_el0"), "pend0=", rd32(GICD_BASE + 0x200));
	con_str("GIC", vt_ticks >= 10 ? "OK (expect ~1000000 us)" : "FAIL: no vtimer IRQs");
}

static uint8_t sector[512] __attribute__((aligned(8)));

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void show_gpt(struct sd_info *sd)
{
	if (sd_read(sd, 1, sector)) {
		con_str("GPT", "LBA1 read failed");
		return;
	}
	if (le32(sector) != 0x20494645 || le32(sector + 4) != 0x54524150) {	/* "EFI PART" */
		con_str("GPT", "no EFI PART header");
		return;
	}
	uint32_t entries_lba = le32(sector + 72), n = le32(sector + 80), esz = le32(sector + 84);
	con_hex2("GPT", "n=", n, "esz=", esz);
	if (esz != 128 || sd_read(sd, entries_lba, sector))
		return;
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = sector + i * 128;
		if (!le32(e) && !le32(e + 4))
			continue;
		char name[13];
		for (int j = 0; j < 12; j++)
			name[j] = e[56 + 2 * j] ? (char)e[56 + 2 * j] : 0;
		name[12] = 0;
		/* one line: name, first LBA, sector count (32-bit is enough for display) */
		con_hex2(name, "", le32(e + 32), "n=", le32(e + 40) - le32(e + 32) + 1);
	}
}

static void test_sd(void)
{
	static struct sd_info sd;
	stage = "sd";
	int r = sd_init(&sd);
	con_hex2("SD", "ret=", (uint32_t)r, "stage=", sd.stage);
	con_hex2("", "i2cfix=", sd.i2c_clk_fixed, "ldo2=", (uint32_t)sd.ldo2);
	con_hex2("", "calto=", sd.autocal_timeout, "calsts=", sd.autocal_sts);
	if (r) {
		con_hex2("", "intsts=", sd.last_err, 0, 0);
		con_str("SD", r == SD_ERR_NO_CARD ? "no card" : "FAIL");
		return;
	}
	con_hex2("", "ocr=", sd.ocr, "rca=", sd.rca);
	con_hex2("CID", "", sd.cid[3], "", sd.cid[2]);
	con_hex2("", "", sd.cid[1], "", sd.cid[0]);

	stage = "sd read";
	uint32_t t0 = usec();
	r = sd_read(&sd, 0, sector);
	uint32_t dt = usec() - t0;
	if (r) {
		con_hex2("LBA0", "ret=", (uint32_t)r, "intsts=", sd.last_err);
		return;
	}
	con_hex2("LBA0", "sig=", sector[510] | sector[511] << 8, "us=", dt);
	int gpt = 0;
	for (int i = 0; i < 4; i++) {
		const uint8_t *e = sector + 446 + 16 * i;
		if (!e[4])
			continue;
		if (e[4] == 0xEE)
			gpt = 1;
		con_hex2("MBR", "type=", e[4], "lba=", le32(e + 8));
		con_hex2("", "", 0, "size=", le32(e + 12));
	}
	if (gpt)
		show_gpt(&sd);
}

void shim_main(uint64_t entry_el, uint64_t entry_x0)
{
	/* Stage 1: hekate's buffer is what the panel scans out right now. */
	con_fb = (volatile uint32_t *)HEKATE_FB;
	fill(HEKATE_FB, DKGREEN);
	udelay(300000);

	/* Stage 2: our own buffer. */
	stage = "display";
	/* Holding volume-up skips this and keeps drawing into hekate's
	 * buffer: the fallback if the window switch blanks the panel. */
	struct dc_state dc = { 0 };
	int full = -1;
	if (!vol_up_pressed()) {
		fill(SHIM_FB, DKBLUE);
		full = display_take_over(SHIM_FB, &dc);
		con_fb = (volatile uint32_t *)SHIM_FB;
	}
	con_clear(DKBLUE);

	/* Stage 3. */
	con_str("NX SHIM", __DATE__ " " __TIME__);
	con_hex2("ENTRY", "el=", entry_el, "x0=", (uint32_t)entry_x0);
	con_hex2("CPU", "midr=", rsr("midr_el1"), "mpidr=", rsr("mpidr_el1"));
	con_hex2("", "el=", rsr("CurrentEL") >> 2, "frq=", rsr("cntfrq_el0"));
	con_hex2("DC", full < 0 ? "skip " : full ? "FULL " : "addr ", dc.win_options, "start=", dc.start);
	con_hex2("", "depth=", dc.color_depth, "size=", dc.size);
	con_hex2("", "stride=", dc.stride, "kind=", dc.kind);
	show_env();
	poll_rcm();

	stage = "psci";
	uint64_t v = smc(0x84000000, 0, 0, 0);
	con_hex2("PSCI", "ver=", (uint32_t)v, 0, 0);
	con_hex2("", "cpu_on=", (uint32_t)smc(0x8400000A, 0xC4000003, 0, 0),
		 "off=", (uint32_t)smc(0x8400000A, 0x84000008, 0, 0));
	poll_rcm();

	test_gic();
	poll_rcm();
	test_sd();
	stage = "idle";
	idle();
}
