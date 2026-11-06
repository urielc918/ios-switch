/*
 * Tegra I2C controller, polled. Normal mode (I2C5, PMIC) and packet mode
 * with a repeated start between write and read (I2C3, touchscreen: the STM
 * FTS4 is read that way). Register facts in NOTES.md.
 */
#include "t210.h"
#include "i2c.h"

#define I2C5		(I2C_BASE + 0x1000)
#define I2C3		(I2C_BASE + 0x500)
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
#define I2C_TX_FIFO	0x50
#define I2C_RX_FIFO	0x54
#define I2C_PKT_STATUS	0x58
#define I2C_FIFO_CTRL	0x5C
#define I2C_FIFO_STATUS	0x60
#define  CNFG_PKT_GO	BIT(10)
#define  PKT_PROT_I2C	BIT(4)
#define  HDR_REP_START	BIT(16)
#define  HDR_READ	BIT(19)
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

/* Clock divisor (400 kHz from 19.2 MHz) and a bus clear. */
static void bus_setup(uintptr_t b)
{
	wr32(b + I2C_CLK_DIV, (5 << 16) | 1);
	wr32(b + I2C_BC_CONFIG, (9 << 16) | BIT(1) | BIT(0));	/* bus clear */
	load_cfg(b);
	for (int i = 0; i < 10 && !(rd32(b + I2C_INT_STATUS) & BIT(11)); i++)
		udelay(25);
	(void)rd32(b + I2C_BC_STATUS);
	wr32(b + I2C_INT_STATUS, rd32(b + I2C_INT_STATUS));
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
	bus_setup(I2C5);
	return touched;
}

/* I2C3 (touchscreen): U bank bit 3, CLK_SOURCE 0x1B8 = 19.2 MHz. Returns 1 if (re)clocked. */
int i2c3_init(void)
{
	int touched = 0;
	if (!(rd32(CAR_CLK_OUT_ENB_U) & CLK_U_I2C3) || (rd32(CAR_RST_DEVICES_U) & CLK_U_I2C3)) {
		wr32(CAR_RST_DEV_U_SET, CLK_U_I2C3);
		wr32(CAR_CLK_ENB_U_CLR, CLK_U_I2C3);
		wr32(CAR_CLK_SOURCE_I2C3, 6U << 29);
		wr32(CAR_CLK_ENB_U_SET, CLK_U_I2C3);
		udelay(2);
		wr32(CAR_RST_DEV_U_CLR, CLK_U_I2C3);
		(void)rd32(CAR_RST_DEVICES_U);
		touched = 1;
	}
	bus_setup(I2C3);
	return touched;
}

/*
 * Packet mode: write tx (1..20 bytes) and, after a repeated start, read rx
 * (1..32 bytes) from addr on I2C3. 0 on success, -1 timeout, -2 no ACK.
 */
int i2c3_xfer_rs(uint8_t addr, const uint8_t *tx, unsigned txlen, uint8_t *rx, unsigned rxlen)
{
	uintptr_t b = I2C3;
	int r = 0;
	if (txlen == 0 || txlen > 20 || rxlen == 0 || rxlen > 32)
		return -1;
	wr32(b + I2C_CMD_ADDR0, (uint32_t)addr << 1 | 1);
	wr32(b + I2C_CNFG, CNFG_DEBOUNCE4 | CNFG_NEW_FSM | CNFG_READ);
	wr32(b + I2C_FIFO_CTRL, BIT(0) | BIT(1));		/* flush RX and TX FIFOs */
	load_cfg(b);
	wr32(b + I2C_CNFG, (rd32(b + I2C_CNFG) & ~CNFG_GO) | CNFG_PKT_GO);
	wr32(b + I2C_TX_FIFO, PKT_PROT_I2C);
	wr32(b + I2C_TX_FIFO, txlen - 1);
	wr32(b + I2C_TX_FIFO, HDR_REP_START | (uint32_t)addr << 1);
	for (unsigned i = 0; i < txlen; i += 4) {
		uint32_t w = 0;
		for (unsigned j = 0; j < 4 && i + j < txlen; j++)
			w |= (uint32_t)tx[i + j] << (8 * j);
		wr32(b + I2C_TX_FIFO, w);
	}
	uint32_t s = usec();
	while (((rd32(b + I2C_PKT_STATUS) >> 4) & 0xFFF) != txlen - 1)
		if (usec() - s > 200000) {
			r = -1;
			goto out;
		}
	wr32(b + I2C_TX_FIFO, PKT_PROT_I2C);
	wr32(b + I2C_TX_FIFO, rxlen - 1);
	wr32(b + I2C_TX_FIFO, HDR_READ | (uint32_t)addr << 1);
	s = usec();
	for (unsigned i = 0; i < rxlen;) {
		if (rd32(b + I2C_FIFO_STATUS) & 0xF) {
			uint32_t w = rd32(b + I2C_RX_FIFO);
			for (unsigned j = 0; j < 4 && i < rxlen; j++, i++)
				rx[i] = (uint8_t)(w >> (8 * j));
		}
		if (usec() - s > 200000) {
			r = -1;
			break;
		}
	}
out:
	if (rd32(b + I2C_STATUS) & STATUS_NOACK)
		r = -2;
	udelay(20);
	wr32(b + I2C_CNFG, rd32(b + I2C_CNFG) & ~(CNFG_PKT_GO | CNFG_GO));
	return r;
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
