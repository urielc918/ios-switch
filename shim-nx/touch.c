/*
 * Power up the Switch touchscreen (STM FTS4, I2C3 address 0x49) and read its
 * chip info, for the screen and for XNU's touch driver, which then only
 * resets the controller and turns sensing on (xnu patch, bsd/dev/nx_input.c).
 * Sequence and register facts from hekate's touch_power_on (see NOTES.md).
 */
#include "t210.h"
#include "i2c.h"
#include "touch.h"

#define FTS4_ADDR		0x49
#define FTS4_CMD_HW_REG_READ	0xB6
#define FTS4_HW_REG_CHIP_ID	0x0004
#define FTS4_HW_REG_EVENT_COUNT	0x0023
#define FTS4_CMD_READ_ONE_EVENT	0x85
#define FTS4_EV_CONTROLLER_READY 0x10

static int event_count(void)
{
	uint8_t cmd[3] = { FTS4_CMD_HW_REG_READ, 0, FTS4_HW_REG_EVENT_COUNT }, buf[2];
	return i2c3_xfer_rs(FTS4_ADDR, cmd, 3, buf, 2) ? -1 : buf[1] >> 1;
}

/* Wait up to ms for the controller's "ready" event (it queues one after reset). */
static int wait_ready(unsigned ms)
{
	uint32_t s = usec();
	while (usec() - s < ms * 1000) {
		if (event_count() > 0) {
			uint8_t cmd = FTS4_CMD_READ_ONE_EVENT, ev[8];
			if (!i2c3_xfer_rs(FTS4_ADDR, &cmd, 1, ev, 8) && ev[0] == FTS4_EV_CONTROLLER_READY)
				return 0;
		}
		udelay(500);
	}
	return -1;
}

int touch_power_on(struct touch_info *t)
{
	/* Reset line: DAP4_SCLK pad as GPIO J7, output low. */
	wr32(PINMUX_DAP4_SCLK, PINMUX_PULL_DOWN | 1);
	gpio_msk(GPIO_MSK_CNF(GPIO_PORT_J), BIT(7), BIT(7));
	gpio_msk(GPIO_MSK_OUT(GPIO_PORT_J), BIT(7), 0);
	gpio_msk(GPIO_MSK_OE(GPIO_PORT_J), BIT(7), BIT(7));
	udelay(20);

	/* AVDD/DVDD: MAX77620 LDO6 = 2.9 V, normal mode. */
	i2c5_init();
	t->ldo6_err = i2c5_write8(MAX77620_ADDR, MAX77620_LDO6_CFG, (3 << 6) | ((2900 - 800) / 50));
	t->ldo6 = i2c5_read8(MAX77620_ADDR, MAX77620_LDO6_CFG);

	/* I2C3 pads (and I2C4's off the I2C3 function), clock, controller. */
	wr32(PINMUX_I2C3_SCL, PINMUX_INPUT_ENABLE);
	wr32(PINMUX_I2C3_SDA, PINMUX_INPUT_ENABLE);
	set32(PINMUX_I2C4_SCL, 1);
	set32(PINMUX_I2C4_SDA, 1);
	t->i2c_clk_fixed = i2c3_init();
	udelay(1000);

	/* Out of reset; the controller boots and queues CONTROLLER_READY. */
	gpio_msk(GPIO_MSK_OUT(GPIO_PORT_J), BIT(7), BIT(7));
	udelay(10000);
	t->ready = wait_ready(50) == 0;

	uint8_t cmd[3] = { FTS4_CMD_HW_REG_READ, 0, FTS4_HW_REG_CHIP_ID }, buf[7] = { 0 };
	t->err = i2c3_xfer_rs(FTS4_ADDR, cmd, 3, buf, 7);
	t->chip_id = (uint16_t)(buf[1] << 8 | buf[2]);
	t->fw_ver = (uint16_t)(buf[3] << 8 | buf[4]);
	return t->err == 0 && t->chip_id == 0x3670 ? 0 : -1;
}
