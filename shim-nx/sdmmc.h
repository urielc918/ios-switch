#pragma once
#include <stdint.h>

enum {
	SD_ERR_NO_CARD = -1,
	SD_ERR_LDO2 = -2,
	SD_ERR_RESET = -3,
	SD_ERR_CLOCK = -4,
	SD_ERR_INHIBIT = -5,
	SD_ERR_CMD_TIMEOUT = -6,
	SD_ERR_CMD = -7,
	SD_ERR_BUSY = -8,
	SD_ERR_OCR = -9,
	SD_ERR_DATA_TIMEOUT = -10,
	SD_ERR_DATA = -11,
};

struct sd_info {
	int stage;		/* last command attempted (100 = done) */
	uint32_t last_err;	/* SDHCI INTSTS (normal | error << 16) on failure */
	int i2c_clk_fixed, ldo2_err, ldo2;
	int autocal_timeout;
	uint32_t autocal_sts;
	int v2, sdhc;
	uint32_t ocr, rca, cid[4], csd[4];
};

int sd_card_present(void);
int sd_init(struct sd_info *sd);
int sd_read(struct sd_info *sd, uint32_t lba, void *buf);
