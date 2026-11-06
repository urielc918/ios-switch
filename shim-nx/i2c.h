#pragma once
#include <stdint.h>

#define MAX77620_ADDR		0x3C
#define MAX77620_ONOFFSTAT	0x15	/* bit 2: power button */
#define MAX77620_LDO2_CFG	0x27	/* [7:6] power mode, [5:0] (mV-800)/50 */
#define MAX77620_ONOFFCNFG1	0x41	/* bit 1: power off */

/* Returns 1 if the I2C5 clock had to be (re)enabled. */
int i2c5_init(void);
/* 0 on success, -1 timeout, -2 no ACK. */
int i2c5_write8(uint8_t dev, uint8_t reg, uint8_t val);
/* Byte value, or <0 as above. */
int i2c5_read8(uint8_t dev, uint8_t reg);

#define MAX77620_LDO6_CFG	0x2F	/* touchscreen AVDD/DVDD */

/* I2C3 (touchscreen). Returns 1 if the clock had to be (re)enabled. */
int i2c3_init(void);
/* Write tx, repeated start, read rx (packet mode). 0, -1 timeout, -2 no ACK. */
int i2c3_xfer_rs(uint8_t addr, const uint8_t *tx, unsigned txlen, uint8_t *rx, unsigned rxlen);
