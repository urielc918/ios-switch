/*
 * SD card on SDMMC1, cold start (hekate powers the card off before BL33).
 * Polled PIO, 1-bit bus, default speed (25 MHz) after identification.
 * Register facts and the power-up order are in NOTES.md.
 */
#include "t210.h"
#include "i2c.h"
#include "sdmmc.h"

#define CAR_CLK_OUT_ENB_Y	(CAR_BASE + 0x298)
#define CAR_RST_DEVICES_Y	(CAR_BASE + 0x2A4)
#define CAR_RST_DEV_Y_CLR	(CAR_BASE + 0x2AC)
#define CAR_RST_DEVICES_L	(CAR_BASE + 0x004)

/* SDHCI standard */
#define S(r)		(SDMMC1_BASE + (r))
#define SD_BLKSIZE	0x04	/* 16 */
#define SD_BLKCNT	0x06	/* 16 */
#define SD_ARG		0x08
#define SD_TRNMOD_CMD	0x0C	/* trnmod (16) | cmd (16) << 16 */
#define  TRN_READ	BIT(4)
#define  CMD_DATA	BIT(5)
#define  CMD_IDX_CHK	BIT(4)
#define  CMD_CRC_CHK	BIT(3)
#define  RSP_NONE	0
#define  RSP_136	1
#define  RSP_48		2
#define  RSP_48_BUSY	3
#define SD_RESP0	0x10
#define SD_BUFFER	0x20
#define SD_PRESENT	0x24
#define  PS_CMD_INHIBIT	BIT(0)
#define  PS_DAT_INHIBIT	BIT(1)
#define SD_HOSTCTL	0x28	/* 8 */
#define SD_PWRCON	0x29	/* 8 */
#define  PWR_ON		BIT(0)
#define  PWR_330	(7 << 1)
#define SD_CLKCON	0x2C	/* 16 */
#define  CLK_INT_EN	BIT(0)
#define  CLK_INT_STABLE	BIT(1)
#define  CLK_CARD_EN	BIT(2)
#define  CLK_PROG_MODE	BIT(5)
#define SD_TIMEOUT	0x2E	/* 8 */
#define SD_SWRST	0x2F	/* 8 */
#define  RST_ALL	BIT(0)
#define  RST_CMD	BIT(1)
#define  RST_DAT	BIT(2)
#define SD_INTSTS	0x30	/* normal (16) | error (16) << 16, write 1 to clear */
#define  INT_CMD_DONE	BIT(0)
#define  INT_XFER_DONE	BIT(1)
#define  INT_BUF_RD	BIT(5)
#define  INT_ERR	BIT(15)
#define SD_NORINTEN	0x34	/* 16 */
#define SD_ERRINTEN	0x36	/* 16 */
#define SD_HOSTCTL2	0x3E	/* 16 */
#define  HC2_PRESET_EN	BIT(15)
#define  HC2_VDD_180	BIT(3)

/* Tegra vendor */
#define SD_VENCLKCTL	0x100
#define SD_IOTRIMCTL	0x1AC
#define SD_CMPPADCTL	0x1E0
#define  PAD_E_INPUT_PWRD BIT(31)
#define SD_AUTOCALCFG	0x1E4
#define  AUTOCAL_ENABLE	BIT(29)
#define  AUTOCAL_START	BIT(31)
#define SD_AUTOCALSTS	0x1EC
#define  AUTOCAL_ACTIVE	BIT(31)
#define SD_IOSPARE	0x1F0

#define MS	1000

static int wait_clear8(uintptr_t a, uint8_t m, uint32_t us)
{
	uint32_t s = usec();
	while (rd8(a) & m)
		if (usec() - s > us)
			return -1;
	return 0;
}

static void reset_lines(uint8_t m)
{
	wr8(S(SD_SWRST), rd8(S(SD_SWRST)) | m);
	wait_clear8(S(SD_SWRST), m, 100 * MS);
}

int sd_card_present(void)
{
	wr32(PINMUX_GPIO_PZ1, PINMUX_INPUT_ENABLE | PINMUX_PULL_UP | 2);
	wr32(APB_MISC_VGPIO_GPIO_MUX_SEL, 0);
	gpio_msk(GPIO_MSK_CNF(GPIO_PORT_Z), BIT(1), BIT(1));
	gpio_msk(GPIO_MSK_OE(GPIO_PORT_Z), BIT(1), 0);
	udelay(100);
	return !(rd32(GPIO_IN(GPIO_PORT_Z)) & BIT(1));
}

/* Pads, rails and controller clock, in the order hekate uses. */
static int power_on(struct sd_info *sd)
{
	/* hekate leaves the SDMMC1 pads (GPIO port M 0-5) in the "discharge"
	 * state only transiently; make sure they're back on the SDMMC function. */
	gpio_msk(GPIO_MSK_CNF(GPIO_PORT_M), 0x3F, 0);
	gpio_msk(GPIO_MSK_OE(GPIO_PORT_M), 0x3F, 0);
	gpio_msk(GPIO_MSK_OUT(GPIO_PORT_M), 0x3F, 0);

	wr32(APB_MISC_SDMMC1_CLK_LPBK, 1);
	clr32(PINMUX_SDMMC1_CLK, PINMUX_SCHMT);
	wr32(PINMUX_SDMMC1_CLK, PINMUX_DRIVE_2X | PINMUX_INPUT_ENABLE | PINMUX_PULL_DOWN);
	uint32_t pm = PINMUX_DRIVE_2X | PINMUX_INPUT_ENABLE | PINMUX_PULL_UP;
	wr32(PINMUX_SDMMC1_CMD, pm);
	wr32(PINMUX_SDMMC1_DAT3, pm);
	wr32(PINMUX_SDMMC1_DAT2, pm);
	wr32(PINMUX_SDMMC1_DAT1, pm);
	wr32(PINMUX_SDMMC1_DAT0, pm);

	/* Controller IO power. */
	set32(PMC_NO_IOPOWER, PMC_NO_IOPOWER_SDMMC1);
	udelay(1 * MS);
	clr32(PMC_NO_IOPOWER, PMC_NO_IOPOWER_SDMMC1);
	(void)rd32(PMC_NO_IOPOWER);

	/* Card VDD: GPIO E4 high (DMIC3_CLK pad). */
	wr32(PINMUX_DMIC3_CLK, PINMUX_PULL_DOWN | 2);
	gpio_msk(GPIO_MSK_CNF(GPIO_PORT_E), BIT(4), BIT(4));
	gpio_msk(GPIO_MSK_OUT(GPIO_PORT_E), BIT(4), BIT(4));
	gpio_msk(GPIO_MSK_OE(GPIO_PORT_E), BIT(4), BIT(4));
	udelay(10 * MS);

	set32(PMC_PWR_DET_VAL, PMC_PWR_DET_33V_SDMMC1);
	(void)rd32(PMC_PWR_DET_VAL);

	/* Card IO rail: MAX77620 LDO2 = 3.3 V, normal mode. */
	sd->i2c_clk_fixed = i2c5_init();
	sd->ldo2_err = i2c5_write8(MAX77620_ADDR, MAX77620_LDO2_CFG, (3 << 6) | ((3300 - 800) / 50));
	sd->ldo2 = i2c5_read8(MAX77620_ADDR, MAX77620_LDO2_CFG);
	if (sd->ldo2_err)
		return SD_ERR_LDO2;
	udelay(1 * MS);

	/* Pad slew codes (T210). */
	wr32(APB_MISC_SDMMC1_PAD_CFGPADCTRL,
	     (rd32(APB_MISC_SDMMC1_PAD_CFGPADCTRL) & 0x0FFFFFFF) | 0x50000000);
	(void)rd32(APB_MISC_SDMMC1_PAD_CFGPADCTRL);
	udelay(1 * MS);

	/* Legacy timeout clock: 12 MHz. */
	if (!(rd32(CAR_CLK_OUT_ENB_Y) & CLK_Y_SDMMC_LEGACY_TM) ||
	    (rd32(CAR_RST_DEVICES_Y) & CLK_Y_SDMMC_LEGACY_TM)) {
		wr32(CAR_CLK_SOURCE_SDMMC_LEGACY_TM, (4U << 29) | 66);
		wr32(CAR_CLK_ENB_Y_SET, CLK_Y_SDMMC_LEGACY_TM);
		udelay(2);
		wr32(CAR_RST_DEV_Y_CLR, CLK_Y_SDMMC_LEGACY_TM);
	}

	/* SDMMC1 module clock: PLLP / 16.5 = 24.7 MHz. */
	wr32(CAR_CLK_ENB_L_CLR, CLK_L_SDMMC1);
	wr32(CAR_RST_DEV_L_SET, CLK_L_SDMMC1);
	wr32(CAR_CLK_SOURCE_SDMMC1, (0U << 29) | 31);
	wr32(CAR_CLK_ENB_L_SET, CLK_L_SDMMC1);
	(void)rd32(CAR_RST_DEVICES_L);
	udelay(100);
	wr32(CAR_RST_DEV_L_CLR, CLK_L_SDMMC1);
	(void)rd32(CAR_RST_DEVICES_L);
	return 0;
}

static void autocal(struct sd_info *sd)
{
	set32(S(SD_CMPPADCTL), PAD_E_INPUT_PWRD);
	(void)rd16(S(SD_CLKCON));
	udelay(1);
	set32(S(SD_AUTOCALCFG), AUTOCAL_ENABLE | AUTOCAL_START);
	(void)rd16(S(SD_CLKCON));
	udelay(2);
	uint32_t s = usec();
	sd->autocal_timeout = 0;
	while (rd32(S(SD_AUTOCALSTS)) & AUTOCAL_ACTIVE)
		if (usec() - s > 10 * MS) {
			sd->autocal_timeout = 1;
			break;
		}
	sd->autocal_sts = rd32(S(SD_AUTOCALSTS));
	if (sd->autocal_timeout) {
		clr32(S(SD_AUTOCALCFG), AUTOCAL_ENABLE);
		wr32(APB_MISC_SDMMC1_PAD_CFGPADCTRL,
		     (rd32(APB_MISC_SDMMC1_PAD_CFGPADCTRL) & 0xF8080FFF) | (0xC0C << 12));
	}
	clr32(S(SD_CMPPADCTL), PAD_E_INPUT_PWRD);
}

static void set_card_clock(unsigned divisor)
{
	uint16_t c = rd16(S(SD_CLKCON)) & ~CLK_CARD_EN;
	wr16(S(SD_CLKCON), c);
	unsigned lo = divisor >> 1, hi = lo >> 8;
	c = (c & 0x003F) | (lo & 0xFF) << 8 | (hi & 3) << 6;
	wr16(S(SD_CLKCON), c);
	wr16(S(SD_CLKCON), c | CLK_CARD_EN);
	udelay(1 * MS);
}

static int host_init(struct sd_info *sd)
{
	wr8(S(SD_SWRST), RST_ALL);
	if (wait_clear8(S(SD_SWRST), RST_ALL, 100 * MS))
		return SD_ERR_RESET;

	set32(S(SD_IOSPARE), BIT(19));
	clr32(S(SD_IOTRIMCTL), BIT(2));
	/* trim 2, tap 4 (T210 SDMMC1) */
	wr32(S(SD_VENCLKCTL), (rd32(S(SD_VENCLKCTL)) & 0xE000FFFB) | (2U << 24) | (4U << 16));
	wr32(S(SD_CMPPADCTL), (rd32(S(SD_CMPPADCTL)) & ~0xFU) | 7);
	wr32(S(SD_AUTOCALCFG), (rd32(S(SD_AUTOCALCFG)) & 0xFFFF8080) | (0x7D << 8) | 0);
	autocal(sd);

	wr16(S(SD_CLKCON), rd16(S(SD_CLKCON)) | CLK_INT_EN);
	uint32_t s = usec();
	while (!(rd16(S(SD_CLKCON)) & CLK_INT_STABLE))
		if (usec() - s > 100 * MS)
			return SD_ERR_CLOCK;
	wr16(S(SD_HOSTCTL2), rd16(S(SD_HOSTCTL2)) & ~(HC2_PRESET_EN | HC2_VDD_180));
	wr16(S(SD_CLKCON), rd16(S(SD_CLKCON)) & ~CLK_PROG_MODE);
	wr8(S(SD_TIMEOUT), 14);
	wr8(S(SD_HOSTCTL), 0);			/* 1-bit, normal speed, SDMA select (unused) */
	wr8(S(SD_PWRCON), PWR_330);
	wr8(S(SD_PWRCON), PWR_330 | PWR_ON);
	wr16(S(SD_NORINTEN), INT_CMD_DONE | INT_XFER_DONE | INT_BUF_RD);
	wr16(S(SD_ERRINTEN), 0x017F);
	wr32(S(SD_INTSTS), 0xFFFFFFFF);
	set_card_clock(64);			/* 24.7 MHz / 64 = 386 kHz */
	return 0;
}

static int cmd(struct sd_info *sd, unsigned idx, uint32_t arg, unsigned rsp, int data, uint32_t *resp)
{
	uint32_t f = rsp;
	if (rsp == RSP_48 || rsp == RSP_48_BUSY)
		f |= CMD_IDX_CHK | CMD_CRC_CHK;
	if (rsp == RSP_136)
		f |= CMD_CRC_CHK;
	if (idx == 41)		/* R3: no CRC, no index */
		f = RSP_48;

	uint8_t inhibit = PS_CMD_INHIBIT | ((data || rsp == RSP_48_BUSY) ? PS_DAT_INHIBIT : 0);
	uint32_t s = usec();
	while (rd32(S(SD_PRESENT)) & inhibit)
		if (usec() - s > 100 * MS)
			return SD_ERR_INHIBIT;

	wr32(S(SD_INTSTS), 0xFFFFFFFF);
	wr32(S(SD_ARG), arg);
	uint32_t trn = 0;
	if (data) {
		wr16(S(SD_BLKSIZE), 512);
		wr16(S(SD_BLKCNT), 1);
		trn = TRN_READ;
		f |= CMD_DATA;
	}
	wr32(S(SD_TRNMOD_CMD), trn | (idx << 8 | f) << 16);

	s = usec();
	uint32_t st;
	while (!((st = rd32(S(SD_INTSTS))) & (INT_CMD_DONE | INT_ERR)))
		if (usec() - s > 100 * MS) {
			sd->last_err = st;
			reset_lines(RST_CMD | RST_DAT);
			return SD_ERR_CMD_TIMEOUT;
		}
	if (st & INT_ERR) {
		sd->last_err = st;
		reset_lines(RST_CMD | RST_DAT);
		return SD_ERR_CMD;
	}
	wr32(S(SD_INTSTS), INT_CMD_DONE);
	if (resp)
		for (int i = 0; i < (rsp == RSP_136 ? 4 : 1); i++)
			resp[i] = rd32(S(SD_RESP0) + 4 * i);
	if (rsp == RSP_48_BUSY) {
		s = usec();
		while (!((st = rd32(S(SD_INTSTS))) & (INT_XFER_DONE | INT_ERR)))
			if (usec() - s > 500 * MS)
				return SD_ERR_BUSY;
		wr32(S(SD_INTSTS), INT_XFER_DONE);
	}
	return 0;
}

int sd_init(struct sd_info *sd)
{
	int r;
	uint32_t resp[4];

	if (!sd_card_present())
		return SD_ERR_NO_CARD;
	if ((r = power_on(sd)))
		return r;
	if ((r = host_init(sd)))
		return r;

	sd->stage = 0;
	if ((r = cmd(sd, 0, 0, RSP_NONE, 0, 0)))
		return r;
	udelay(1 * MS);
	sd->stage = 8;
	sd->v2 = cmd(sd, 8, 0x1AA, RSP_48, 0, resp) == 0 && (resp[0] & 0xFFF) == 0x1AA;
	sd->stage = 41;
	uint32_t s = usec();
	for (;;) {
		if ((r = cmd(sd, 55, 0, RSP_48, 0, resp)))
			return r;
		if ((r = cmd(sd, 41, 0x00FF8000 | (sd->v2 ? BIT(30) : 0), RSP_48, 0, resp)))
			return r;
		if (resp[0] & BIT(31))
			break;
		if (usec() - s > 1000 * MS)
			return SD_ERR_OCR;
		udelay(10 * MS);
	}
	sd->ocr = resp[0];
	sd->sdhc = !!(sd->ocr & BIT(30));
	sd->stage = 2;
	if ((r = cmd(sd, 2, 0, RSP_136, 0, sd->cid)))
		return r;
	sd->stage = 3;
	if ((r = cmd(sd, 3, 0, RSP_48, 0, resp)))
		return r;
	sd->rca = resp[0] >> 16;
	sd->stage = 9;
	if ((r = cmd(sd, 9, sd->rca << 16, RSP_136, 0, sd->csd)))
		return r;
	sd->stage = 7;
	if ((r = cmd(sd, 7, sd->rca << 16, RSP_48_BUSY, 0, resp)))
		return r;
	set_card_clock(1);			/* default speed, 24.7 MHz */
	sd->stage = 16;
	if (!sd->sdhc && (r = cmd(sd, 16, 512, RSP_48, 0, resp)))
		return r;
	sd->stage = 100;
	return 0;
}

int sd_read(struct sd_info *sd, uint32_t lba, void *buf)
{
	uint32_t resp[1];
	int r = cmd(sd, 17, sd->sdhc ? lba : lba * 512, RSP_48, 1, resp);
	if (r)
		return r;
	uint32_t s = usec(), st;
	while (!((st = rd32(S(SD_INTSTS))) & (INT_BUF_RD | INT_ERR)))
		if (usec() - s > 500 * MS)
			goto timeout;
	if (st & INT_ERR)
		goto error;
	wr32(S(SD_INTSTS), INT_BUF_RD);
	uint32_t *p = buf;
	for (int i = 0; i < 128; i++)
		p[i] = rd32(S(SD_BUFFER));
	s = usec();
	while (!((st = rd32(S(SD_INTSTS))) & (INT_XFER_DONE | INT_ERR)))
		if (usec() - s > 500 * MS)
			goto timeout;
	if (st & INT_ERR)
		goto error;
	wr32(S(SD_INTSTS), INT_XFER_DONE);
	return 0;
timeout:
	sd->last_err = st;
	reset_lines(RST_CMD | RST_DAT);
	return SD_ERR_DATA_TIMEOUT;
error:
	sd->last_err = st;
	reset_lines(RST_CMD | RST_DAT);
	return SD_ERR_DATA;
}
