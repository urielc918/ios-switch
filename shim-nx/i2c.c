/*
 * Tegra I2C controller, normal (non-packet) mode, polled. Only I2C5 (PMIC
 * bus) is used for now. Register facts in NOTES.md.
 */
#include "t210.h"
#include "i2c.h"

#define I2C5		(I2C_BASE + 0x1000)
#define I2C_CNFG	0x00
#define  CNFG_LEN(n)	(((n) - 1) << 1)
#define  CNFG_READ	BIT(6)
#define  CNFG_GO	BIT(9)
#define  CNFG_NEW_FSM	BIT(11)
#define  CNFG_DEBOUNCE4	(2 << 12)
#define I2C_CMD_ADDR0	0x04
#define I2C_CMD_DATA1	0x0C
#define I2C_STATUS	0x1C
#define  STATUS_BUSY	BIT(8)
#define  STATUS_NOACK	0xF
#define I2C_INT_STATUS	0x68
#define I2C_CLK_DIV	0x6C
#define I2C_BC_CONFIG	0x84
#define I2C_BC_STATUS	0x88
#define I2C_CONFIG_LOAD	0x8C

static void load_cfg(uintptr_t b)
{
	wr32(b + I2C_CONFIG_LOAD, BIT(5) | BIT(2) | BIT(0));
	for (int i = 0; i < 20 && (rd32(b + I2C_CONFIG_LOAD) & BIT(0)); i++)
		udelay(1);
}

int i2c5_init(void)
{
	int touched = 0;
	if (!(rd32(CAR_CLK_OUT_ENB_H) & CLK_H_I2C5) || (rd32(CAR_RST_DEVICES_H) & CLK_H_I2C5)) {
		wr32(CAR_RST_DEV_H_SET, CLK_H_I2C5);
		wr32(CAR_CLK_ENB_H_CLR, CLK_H_I2C5);
		wr32(CAR_CLK_SOURCE_I2C5, 6U << 29);	/* 19.2 MHz, div 1 -> 400 kHz */
		wr32(CAR_CLK_ENB_H_SET, CLK_H_I2C5);
		udelay(2);
		wr32(CAR_RST_DEV_H_CLR, CLK_H_I2C5);
		(void)rd32(CAR_RST_DEVICES_H);
		touched = 1;
	}
	uintptr_t b = I2C5;
	wr32(b + I2C_CLK_DIV, (5 << 16) | 1);
	wr32(b + I2C_BC_CONFIG, (9 << 16) | BIT(1) | BIT(0));	/* bus clear */
	load_cfg(b);
	for (int i = 0; i < 10 && !(rd32(b + I2C_INT_STATUS) & BIT(11)); i++)
		udelay(25);
	(void)rd32(b + I2C_BC_STATUS);
	wr32(b + I2C_INT_STATUS, rd32(b + I2C_INT_STATUS));
	return touched;
}

static int xfer(uintptr_t b, uint32_t cnfg)
{
	wr32(b + I2C_CNFG, cnfg);
	load_cfg(b);
	wr32(b + I2C_CNFG, cnfg | CNFG_GO);
	uint32_t s = usec();
	while (rd32(b + I2C_STATUS) & STATUS_BUSY)
		if (usec() - s > 100000)
			return -1;
	return (rd32(b + I2C_STATUS) & STATUS_NOACK) ? -2 : 0;
}

int i2c5_write8(uint8_t dev, uint8_t reg, uint8_t val)
{
	uintptr_t b = I2C5;
	wr32(b + I2C_CMD_ADDR0, dev << 1);
	wr32(b + I2C_CMD_DATA1, reg | (uint32_t)val << 8);
	return xfer(b, CNFG_LEN(2) | CNFG_DEBOUNCE4 | CNFG_NEW_FSM);
}

int i2c5_read8(uint8_t dev, uint8_t reg)
{
	uintptr_t b = I2C5;
	wr32(b + I2C_CMD_ADDR0, dev << 1);
	wr32(b + I2C_CMD_DATA1, reg);
	int r = xfer(b, CNFG_LEN(1) | CNFG_DEBOUNCE4 | CNFG_NEW_FSM);
	if (r)
		return r;
	wr32(b + I2C_CMD_ADDR0, dev << 1 | 1);
	r = xfer(b, CNFG_LEN(1) | CNFG_DEBOUNCE4 | CNFG_NEW_FSM | CNFG_READ);
	if (r)
		return r;
	return rd32(b + I2C_CMD_DATA1) & 0xFF;
}
