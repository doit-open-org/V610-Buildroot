// SPDX-License-Identifier: GPL-2.0-only
/*
 * board_io_config.h — 板级引脚配置模块 结构体定义
 */

#ifndef BOARD_IO_CONFIG_H
#define BOARD_IO_CONFIG_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>

/* ===== IOCFG Block Base Addresses ===== */
#define IOCFG0_BASE  0x10260000UL
#define IOCFG2_BASE  0x17940000UL
#define IOCFG3_BASE  0x11130000UL

/* ===== PL061 GPIO Controller ===== */
#define PL061_DATA  0x000UL
#define PL061_DIR   0x400UL

/* ===== IOCFG bit[12] — IE (Input Enable), always set per HW default ===== */
#define IOCFG_IE  BIT(12)

/* ===== Table 1: GPIO -> (IOCFG block, offset) ===== */
struct gpio_iocfg_entry {
	const char *gpio;
	u8 block;
	u32 offset;
};

extern const struct gpio_iocfg_entry gpio_iocfg_map[];
extern const u32 iocfg_block_base[];
extern const u32 pl061_base[];

#define GPIO_IOCFG_MAP_COUNT 52


/* ===== Table 2: (GPIO, function) -> MUX value ===== */
struct func_entry {
	const char *func;
	u8 mux;
};

struct gpio_mux_group {
	const char *gpio;
	const struct func_entry *funcs;
	int count;
};

extern const struct gpio_mux_group pin_func_mux[];

#define PIN_FUNC_MUX_COUNT 52


/* ===== Table 3: Drive -> code ===== */
struct drive_entry {
	const char *name;
	u8 code;
};

extern const struct drive_entry drive_encode[];

#define DRIVE_ENCODE_COUNT 28

#endif /* BOARD_IO_CONFIG_H */
