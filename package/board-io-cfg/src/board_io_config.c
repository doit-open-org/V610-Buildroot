// SPDX-License-Identifier: GPL-2.0-only
/*
 * board_io_config.c — 板级引脚配置模块（主逻辑）
 *
 * 加载时遍历 dtsi 的 pin-cfg 子节点，读取字符串属性，
 * 查内嵌映射表计算 IOCFG 寄存器值，iowrite32 写入硬件。
 *
 * 内部辅助函数（__init，模块加载后释放）
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/string.h>
#include "board_io_config.h"

/* ===== IOCFG ioremap 缓存（跨 config_pin 调用复用） ===== */
static void __iomem *iocfg_virt[4];

/* ===== Lookup helpers ===== */

static const struct gpio_iocfg_entry *__init lookup_gpio(const char *gpio)
{
	int i;

	for (i = 0; i < GPIO_IOCFG_MAP_COUNT; i++) {
		if (!strcmp(gpio_iocfg_map[i].gpio, gpio)) {
			return &gpio_iocfg_map[i];
		}
	}
	return NULL;
}

static const struct func_entry *__init lookup_mux(const char *gpio, const char *func)
{
	int i, j;

	for (i = 0; i < PIN_FUNC_MUX_COUNT; i++) {
		if (strcmp(pin_func_mux[i].gpio, gpio)) {
			continue;
		}
		for (j = 0; j < pin_func_mux[i].count; j++) {
			if (!strcmp(pin_func_mux[i].funcs[j].func, func)) {
				return &pin_func_mux[i].funcs[j];
			}
		}
	}
	return NULL;
}

static const struct drive_entry *__init lookup_drive(const char *name)
{
	int i;

	for (i = 0; i < DRIVE_ENCODE_COUNT; i++) {
		if (!strcmp(drive_encode[i].name, name)) {
			return &drive_encode[i];
		}
	}
	return NULL;
}

/*
 * Helper: parse "GPIOX_Y" into (chip, pin).
 */
static int __init parse_gpio_chip_pin(const char *gpio, int *chip, int *pin)
{
	const char *us;

	if (!gpio || strncmp(gpio, "GPIO", 4)) {
		return -EINVAL;
	}
	us = strchr(gpio + 4, '_');
	if (!us) {
		return -EINVAL;
	}
	if (kstrtoint(gpio + 4, 10, chip) || *chip > 7) {
		return -EINVAL;
	}
	if (kstrtoint(us + 1, 10, pin) || *pin > 7) {
		return -EINVAL;
	}
	return 0;
}

/*
 * set_gpio_init_level — 设置 GPIO 初始电平
 * @gpio:   GPIO 名称，格式 "GPIOX_Y"
 * @level:  初始电平（0 = 低，非 0 = 高）
 *
 * 读-改-写 PL061 DATA/DIR 寄存器，不影响同 bank 其它引脚。
 */
static void __init set_gpio_init_level(const char *gpio, u32 level)
{
	int chip, pin;
	void __iomem *pl061;
	u32 mask;
	u32 val;

	if (parse_gpio_chip_pin(gpio, &chip, &pin)) {
		return;
	}

	pl061 = ioremap(pl061_base[chip], SZ_4K);
	if (!pl061) {
		pr_err("board_io_cfg: %s: ioremap pl061 chip%d failed\n", gpio, chip);
		return;
	}

	mask = BIT(pin);

	val = readl(pl061 + PL061_DATA);
	if (level) {
		val |= mask;
	} else {
		val &= ~mask;
	}
	writel(val, pl061 + PL061_DATA);

	val = readl(pl061 + PL061_DIR);
	val |= mask;
	writel(val, pl061 + PL061_DIR);

	pr_info("board_io_cfg: %s init=%u (chip%d pin%d)\n", gpio, level, chip, pin);
	iounmap(pl061);
}

/*
 * board_io_cfg_compute_reg_value — 计算 IOCFG 寄存器值
 * @mux:        管脚复用编码 (bits[3:0])
 * @drive_code: 驱动档位编码 (bits[7:4])
 * @pull:       上下拉字符串
 * @slew:       边沿字符串
 *
 * 与 xlsx_to_dtsi.py compute_reg_value() 保持一致的位运算逻辑。
 *
 * 返回值: IOCFG 寄存器值（低 16 位有效）
 */
static u32 __init board_io_cfg_compute_reg_value(u8 mux, u8 drive_code,
					  const char *pull, const char *slew)
{
	u32 val;

	val = mux & 0xF;
	val |= (drive_code & 0xF) << 4;

	if (!strcmp(pull, "pull-up")) {
		val |= BIT(8);
	} else if (!strcmp(pull, "pull-down")) {
		val |= BIT(9);
	}

	if (!strcmp(slew, "slow")) {
		val |= BIT(10);
	}

	val |= IOCFG_IE;

	return val;
}

/*
 * board_io_cfg_config_pin — 一站式配置单个引脚的 IOCFG 寄存器
 * @gpio:  GPIO 名称，如 "GPIO0_0"
 * @func:  功能名，如 "UART0_RXD"
 * @pull:  上下拉，如 "pull-up" / "pull-down" / "none"
 * @drive: 驱动档位名，如 "I02_level4"
 * @slew:  边沿，如 "fast" / "slow"
 *
 * 内部完成：查三张映射表 → 计算寄存器值 → ioremap（带缓存）→ iowrite32。
 * 所有引脚配置完成后须调用 board_io_cfg_release_iocfg() 释放 ioremap。
 *
 * 返回值: 0 成功，-EINVAL 映射表未找到，-ENOMEM ioremap 失败
 */
static int __init board_io_cfg_config_pin(const char *gpio, const char *func,
					  const char *pull, const char *drive,
					  const char *slew)
{
	const struct gpio_iocfg_entry *ge;
	const struct func_entry *fe;
	const struct drive_entry *de;
	u32 val, phys_addr;
	int block;

	ge = lookup_gpio(gpio);
	if (!ge) {
		pr_warn("board_io_cfg: %s: not in gpio_iocfg_map\n", gpio);
		return -EINVAL;
	}

	fe = lookup_mux(gpio, func);
	if (!fe) {
		pr_warn("board_io_cfg: %s: function \"%s\" not found\n", gpio, func);
		return -EINVAL;
	}

	de = lookup_drive(drive);
	if (!de) {
		pr_warn("board_io_cfg: %s: drive \"%s\" not found\n", gpio, drive);
		return -EINVAL;
	}

	val = board_io_cfg_compute_reg_value(fe->mux, de->code, pull, slew);

	block = ge->block;
	if (!iocfg_virt[block]) {
		iocfg_virt[block] = ioremap(iocfg_block_base[block], SZ_4K);
		if (!iocfg_virt[block]) {
			pr_err("board_io_cfg: ioremap failed for block %d\n", block);
			return -ENOMEM;
		}
	}

	phys_addr = iocfg_block_base[block] + ge->offset;
	iowrite32(val, iocfg_virt[block] + ge->offset);
	pr_info("board_io_cfg: %-10s %-20s @ 0x%08x = 0x%04x\n",
		gpio, func, phys_addr, val);

	return 0;
}

static void __init board_io_cfg_release_iocfg(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (iocfg_virt[i]) {
			iounmap(iocfg_virt[i]);
			iocfg_virt[i] = NULL;
		}
	}
}

static int __init board_io_cfg_init(void)
{
	struct device_node *np = NULL;
	struct device_node *child;
	int count = 0;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "vendor,board-io-config");
	if (!np) {
		pr_info("board_io_cfg: DT node not found, skipping\n");
		return 0;
	}

	/* 读取芯片型号信息 */
	child = of_get_child_by_name(np, "chip-info");
	if (child) {
		const char *chip_model = NULL, *package = NULL;

		of_property_read_string(child, "chip-model", &chip_model);
		of_property_read_string(child, "package", &package);
		pr_info("board_io_cfg: %s %s\n",
			chip_model ? chip_model : "unknown",
			package ? package : "");
		of_node_put(child);
	}

	for_each_child_of_node(np, child) {
		const char *gpio, *func, *pull_str, *drive, *slew;
		u32 init_level;

		if (of_property_read_string(child, "gpio-num", &gpio)) {
			continue;
		}
		if (of_property_read_string(child, "function", &func)) {
			continue;
		}
		if (of_property_read_string(child, "pull", &pull_str)) {
			pull_str = "none";
		}
		if (of_property_read_string(child, "drive", &drive)) {
			continue;
		}
		if (of_property_read_string(child, "slew", &slew)) {
			slew = "fast";
		}

		ret = board_io_cfg_config_pin(gpio, func, pull_str, drive, slew);
		if (ret == -ENOMEM) {
			pr_err("board_io_cfg: %s: ioremap failed, aborting\n", gpio);
			goto out;
		}
		if (ret) {
			continue;
		}

		if (!of_property_read_u32(child, "init-level", &init_level)) {
			set_gpio_init_level(gpio, init_level);
		}
		count++;
	}

out:
	of_node_put(np);
	board_io_cfg_release_iocfg();
	if (count) {
		pr_info("board_io_cfg: configured %d pins\n", count);
	} else {
		pr_info("board_io_cfg: no pin-cfg found, skipped (BSP defaults preserved)\n");
	}
	return 0;
}

static void __exit board_io_cfg_exit(void) { }

module_init(board_io_cfg_init);
module_exit(board_io_cfg_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Board IO Configuration - IOCFG + GPIO init");
MODULE_AUTHOR("Auto-generated");
