/*
 * Tegra X1 (T210, Erista) register map for the Switch BL33 shim.
 * Facts are listed in NOTES.md; this file only names them.
 */
#pragma once
#include <stdint.h>

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline uint16_t rd16(uintptr_t a) { return *(volatile uint16_t *)a; }
static inline uint8_t  rd8(uintptr_t a)  { return *(volatile uint8_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }
static inline void wr16(uintptr_t a, uint16_t v) { *(volatile uint16_t *)a = v; }
static inline void wr8(uintptr_t a, uint8_t v)   { *(volatile uint8_t *)a = v; }
static inline void set32(uintptr_t a, uint32_t m) { wr32(a, rd32(a) | m); }
static inline void clr32(uintptr_t a, uint32_t m) { wr32(a, rd32(a) & ~m); }

#define BIT(n)		(1U << (n))

/* Bases */
#define GICD_BASE	0x50041000UL
#define GICC_BASE	0x50042000UL
#define DC_A_BASE	0x54200000UL	/* register index << 2 */
#define TMR_BASE	0x60005000UL
#define CAR_BASE	0x60006000UL
#define GPIO_BASE	0x6000D000UL
#define APB_MISC_BASE	0x70000000UL
#define PINMUX_BASE	0x70003000UL
#define I2C_BASE	0x7000C000UL
#define PMC_BASE	0x7000E400UL
#define SDMMC1_BASE	0x700B0000UL

/* Timer: free-running microsecond counter, kept running by hekate. */
#define TIMERUS_CNTR_1US	(TMR_BASE + 0x10)
static inline uint32_t usec(void) { return rd32(TIMERUS_CNTR_1US); }
static inline void udelay(uint32_t us)
{
	uint32_t s = usec();
	while ((uint32_t)(usec() - s) <= us)
		;
}

/* PMC */
#define PMC_CNTRL		(PMC_BASE + 0x00)
#define  PMC_CNTRL_MAIN_RST	BIT(4)
#define PMC_NO_IOPOWER		(PMC_BASE + 0x44)
#define  PMC_NO_IOPOWER_SDMMC1	BIT(12)
#define PMC_SCRATCH0		(PMC_BASE + 0x50)
#define  PMC_SCRATCH0_MODE_RCM	BIT(1)
#define PMC_PWR_DET_VAL		(PMC_BASE + 0xE4)
#define  PMC_PWR_DET_33V_SDMMC1	BIT(12)

/* CAR */
#define CAR_RST_DEVICES_H	(CAR_BASE + 0x008)
#define CAR_RST_DEVICES_U	(CAR_BASE + 0x00C)
#define CAR_CLK_OUT_ENB_U	(CAR_BASE + 0x018)
#define CAR_CLK_SOURCE_I2C3	(CAR_BASE + 0x1B8)
#define CAR_RST_DEV_U_SET	(CAR_BASE + 0x310)
#define CAR_RST_DEV_U_CLR	(CAR_BASE + 0x314)
#define CAR_CLK_ENB_U_SET	(CAR_BASE + 0x330)
#define CAR_CLK_ENB_U_CLR	(CAR_BASE + 0x334)
#define  CLK_U_I2C3		BIT(3)
#define CAR_CLK_OUT_ENB_H	(CAR_BASE + 0x014)
#define CAR_CLK_SOURCE_I2C5	(CAR_BASE + 0x128)
#define CAR_CLK_SOURCE_SDMMC1	(CAR_BASE + 0x150)
#define CAR_CLK_ENB_Y_SET	(CAR_BASE + 0x29C)
#define CAR_RST_DEV_Y_SET	(CAR_BASE + 0x2A8)
#define CAR_RST_DEV_L_SET	(CAR_BASE + 0x300)
#define CAR_RST_DEV_L_CLR	(CAR_BASE + 0x304)
#define CAR_RST_DEV_H_SET	(CAR_BASE + 0x308)
#define CAR_RST_DEV_H_CLR	(CAR_BASE + 0x30C)
#define CAR_CLK_ENB_L_SET	(CAR_BASE + 0x320)
#define CAR_CLK_ENB_L_CLR	(CAR_BASE + 0x324)
#define CAR_CLK_ENB_H_SET	(CAR_BASE + 0x328)
#define CAR_CLK_ENB_H_CLR	(CAR_BASE + 0x32C)
#define CAR_CLK_SOURCE_SDMMC_LEGACY_TM (CAR_BASE + 0x694)
#define  CLK_L_SDMMC1		BIT(14)
#define  CLK_H_I2C5		BIT(15)
#define  CLK_Y_SDMMC_LEGACY_TM	BIT(1)

/* GPIO: port index, pin bit. Masked registers take (mask << 8) | value. */
#define GPIO_PORT_E	4
#define GPIO_PORT_J	9
#define GPIO_PORT_M	12
#define GPIO_PORT_X	23
#define GPIO_PORT_Z	25
#define GPIO_PORT_OFF(p)	((((p) >> 2) << 8) + (((p) & 3) << 2))
#define GPIO_CNF(p)		(GPIO_BASE + 0x00 + GPIO_PORT_OFF(p))
#define GPIO_OE(p)		(GPIO_BASE + 0x10 + GPIO_PORT_OFF(p))
#define GPIO_OUT(p)		(GPIO_BASE + 0x20 + GPIO_PORT_OFF(p))
#define GPIO_IN(p)		(GPIO_BASE + 0x30 + GPIO_PORT_OFF(p))
#define GPIO_MSK_CNF(p)		(GPIO_BASE + 0x80 + GPIO_PORT_OFF(p))
#define GPIO_MSK_OE(p)		(GPIO_BASE + 0x90 + GPIO_PORT_OFF(p))
#define GPIO_MSK_OUT(p)		(GPIO_BASE + 0xA0 + GPIO_PORT_OFF(p))
static inline void gpio_msk(uintptr_t reg, uint32_t mask, uint32_t val)
{
	wr32(reg, (mask << 8) | (val & mask));
}

/* Pinmux */
#define PINMUX_SDMMC1_CLK	(PINMUX_BASE + 0x00)
#define PINMUX_SDMMC1_CMD	(PINMUX_BASE + 0x04)
#define PINMUX_SDMMC1_DAT3	(PINMUX_BASE + 0x08)
#define PINMUX_SDMMC1_DAT2	(PINMUX_BASE + 0x0C)
#define PINMUX_SDMMC1_DAT1	(PINMUX_BASE + 0x10)
#define PINMUX_SDMMC1_DAT0	(PINMUX_BASE + 0x14)
#define PINMUX_DMIC3_CLK	(PINMUX_BASE + 0xB4)
#define PINMUX_I2C3_SCL		(PINMUX_BASE + 0xCC)	/* 0xBC + 8 * 2 */
#define PINMUX_I2C3_SDA		(PINMUX_BASE + 0xD0)
#define PINMUX_I2C4_SCL		(PINMUX_BASE + 0xD4)	/* CAM_I2C */
#define PINMUX_I2C4_SDA		(PINMUX_BASE + 0xD8)
#define PINMUX_DAP4_SCLK	(PINMUX_BASE + 0x150)	/* touch reset, GPIO J7 */
#define PINMUX_GPIO_PZ1		(PINMUX_BASE + 0x280)
#define  PINMUX_PULL_DOWN	(1 << 2)
#define  PINMUX_PULL_UP		(2 << 2)
#define  PINMUX_INPUT_ENABLE	BIT(6)
#define  PINMUX_SCHMT		BIT(12)
#define  PINMUX_DRIVE_2X	BIT(13)

/* APB_MISC */
#define APB_MISC_SDMMC1_CLK_LPBK	(APB_MISC_BASE + 0x8D4)
#define APB_MISC_SDMMC1_PAD_CFGPADCTRL	(APB_MISC_BASE + 0xA98)
#define APB_MISC_VGPIO_GPIO_MUX_SEL	(APB_MISC_BASE + 0xB74)

/* Buttons (active low) */
#define BTN_VOL_DOWN_PIN	7	/* GPIO X7 */
#define BTN_VOL_UP_PIN		6	/* GPIO X6 */
static inline int vol_down_pressed(void)
{
	return !(rd32(GPIO_IN(GPIO_PORT_X)) & BIT(BTN_VOL_DOWN_PIN));
}
static inline int vol_up_pressed(void)
{
	return !(rd32(GPIO_IN(GPIO_PORT_X)) & BIT(BTN_VOL_UP_PIN));
}

/* GICv2 */
#define GICD_CTLR		(GICD_BASE + 0x000)
#define GICD_TYPER		(GICD_BASE + 0x004)
#define GICD_IIDR		(GICD_BASE + 0x008)
#define GICD_ISENABLER(n)	(GICD_BASE + 0x100 + 4 * (n))
#define GICD_ICENABLER(n)	(GICD_BASE + 0x180 + 4 * (n))
#define GICD_IPRIORITYR		(GICD_BASE + 0x400)	/* byte per INTID */
#define GICD_SGIR		(GICD_BASE + 0xF00)
#define GICC_CTLR		(GICC_BASE + 0x00)
#define GICC_PMR		(GICC_BASE + 0x04)
#define GICC_BPR		(GICC_BASE + 0x08)
#define GICC_IAR		(GICC_BASE + 0x0C)
#define GICC_EOIR		(GICC_BASE + 0x10)
#define GICC_IIDR		(GICC_BASE + 0xFC)
#define INTID_VTIMER		27

/* System registers */
#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" :: "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")
#define dsb() __asm__ volatile("dsb sy" ::: "memory")
