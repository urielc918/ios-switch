#pragma once
#include <stdint.h>

struct touch_info {
	int ldo6_err, ldo6, i2c_clk_fixed, ready, err;
	uint16_t chip_id, fw_ver;
};

/* Power the STM FTS4 touchscreen and read its chip info (expect 0x3670). */
int touch_power_on(struct touch_info *t);
