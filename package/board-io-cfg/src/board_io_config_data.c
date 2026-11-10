// SPDX-License-Identifier: GPL-2.0-only
/*
 * board_io_config_data.c — 三张映射表（纯数据）
 *
 * 生成说明（AI 可执行）：
 *   打开 SoC datasheet xlsx 的"3.管脚控制寄存器"页签（文件名可能因版本而异），
 *   逐行提取以下三个映射关系，拼接为 C 数组：
 *
 *   表1 gpio_iocfg_map[]
 *     列: Register Name → GPIO名, Address → (块基地址差)
 *     规则: io0_cfg_* 块索引=0(iocfg0 0x10260000),
 *           io2_cfg_* 块索引=2(iocfg2 0x17940000),
 *           io3_cfg_* 块索引=3(iocfg3 0x11130000)
 *     偏移 = Address - 所在块基地址
 *     跳过无 GPIO 的引脚(如 power/模拟)
 *
 *   表2 pin_func_mux[]
 *     列: Register Name → GPIO名, bits[3:0] 说明 → (功能名, MUX)
 *     一个 GPIO 对应多个 (功能名, MUX) 对，按 GPIO 分组
 *
 *   表3 drive_encode[]
 *     列: bits[7:4] 说明中的档位表
 *     编码 = 0x0(最强档) ~ 0xF(最弱档)
 *
 *   pl061_base[]
 *     来自 dtsi hi3516cv610.dtsi 的 gpio_chip@ 节点 reg 地址
 */

#include "board_io_config.h"

/* ===== IOCFG Block Base Addresses ===== */
const u32 iocfg_block_base[] __initconst = {
	[0] = IOCFG0_BASE,
	[2] = IOCFG2_BASE,
	[3] = IOCFG3_BASE,
};

/* ===== PL061 GPIO Controller Base Addresses ===== */
const u32 pl061_base[] __initconst = {
	[0] = 0x11090000UL,
	[1] = 0x11091000UL,
	[2] = 0x11092000UL,
	[3] = 0x11093000UL,
	[4] = 0x11094000UL,
	[5] = 0x11095000UL,
	[6] = 0x11096000UL,
	[7] = 0x11097000UL,
};

/* ===== Table 1: GPIO -> (IOCFG block, offset) ===== */
const struct gpio_iocfg_entry gpio_iocfg_map[] __initconst = {
	{"GPIO0_0", 0, 0x0000},
	{"GPIO0_1", 0, 0x0004},
	{"SYS_RSTN_OUT", 0, 0x0008},
	{"GPIO2_3", 0, 0x000C},
	{"GPIO2_2", 0, 0x0010},
	{"GPIO2_6", 0, 0x0014},
	{"GPIO2_1", 0, 0x0018},
	{"GPIO2_4", 0, 0x001C},
	{"GPIO2_5", 0, 0x0020},
	{"GPIO3_5", 0, 0x0028},
	{"GPIO3_6", 0, 0x002C},
	{"GPIO3_7", 0, 0x0030},
	{"GPIO4_0", 0, 0x0034},
	{"GPIO4_1", 0, 0x0038},
	{"GPIO4_2", 0, 0x003C},
	{"GPIO4_3", 0, 0x0040},
	{"GPIO3_4", 0, 0x0044},
	{"GPIO5_6", 2, 0x0000},
	{"GPIO5_7", 2, 0x0004},
	{"GPIO6_2", 2, 0x0008},
	{"GPIO6_3", 2, 0x000C},
	{"GPIO6_0", 2, 0x0010},
	{"GPIO6_1", 2, 0x0014},
	{"GPIO5_0", 2, 0x0018},
	{"GPIO5_1", 2, 0x001C},
	{"GPIO5_2", 2, 0x0020},
	{"GPIO5_3", 2, 0x0024},
	{"GPIO5_4", 2, 0x0028},
	{"GPIO5_5", 2, 0x002C},
	{"GPIO6_5", 2, 0x0080},
	{"GPIO6_4", 2, 0x0084},
	{"GPIO7_3", 2, 0x0088},
	{"GPIO7_2", 2, 0x008C},
	{"GPIO7_0", 2, 0x0090},
	{"GPIO7_1", 2, 0x0094},
	{"GPIO6_6", 2, 0x0098},
	{"GPIO6_7", 2, 0x009C},
	{"GPIO1_5", 3, 0x0000},
	{"GPIO1_4", 3, 0x0004},
	{"GPIO0_6", 3, 0x0030},
	{"GPIO0_7", 3, 0x0034},
	{"GPIO7_7", 3, 0x0038},
	{"GPIO1_0", 3, 0x003C},
	{"GPIO1_2", 3, 0x0040},
	{"GPIO1_3", 3, 0x0044},
	{"GPIO1_1", 3, 0x0048},
	{"GPIO7_6", 3, 0x004C},
	{"GPIO7_5", 3, 0x0050},
	{"GPIO7_4", 3, 0x0054},
	{"GPIO0_2", 3, 0x0058},
	{"GPIO0_3", 3, 0x005C},
	{"GPIO2_0", 3, 0x0068},
};

/* ===== Table 2: (GPIO, function) -> MUX value (grouped by GPIO) ===== */
static const struct func_entry GPIO0_0_funcs[] __initconst = {
	{"GPIO0_0", 0},
};

static const struct func_entry GPIO0_1_funcs[] __initconst = {
	{"GPIO0_1", 0},
	{"PWM0_OUT3", 1},
	{"BOOT_SEL1", 2},
};

static const struct func_entry SYS_RSTN_OUT_funcs[] __initconst = {
	{"SYS_RSTN_OUT", 1},
};

static const struct func_entry GPIO2_3_funcs[] __initconst = {
	{"GPIO2_3", 0},
	{"SFC_MOSI_IO0", 1},
};

static const struct func_entry GPIO2_2_funcs[] __initconst = {
	{"GPIO2_2", 0},
	{"SFC_CLK", 1},
	{"BOOT_SEL0", 4},
};

static const struct func_entry GPIO2_6_funcs[] __initconst = {
	{"GPIO2_6", 0},
	{"SFC_HOLD_IO3", 1},
};

static const struct func_entry GPIO2_1_funcs[] __initconst = {
	{"GPIO2_1", 0},
	{"SFC_CSN0", 1},
};

static const struct func_entry GPIO2_4_funcs[] __initconst = {
	{"GPIO2_4", 0},
	{"SFC_MISO_IO1", 1},
};

static const struct func_entry GPIO2_5_funcs[] __initconst = {
	{"GPIO2_5", 0},
	{"SFC_WP_IO2", 1},
};

static const struct func_entry GPIO3_5_funcs[] __initconst = {
	{"GPIO3_5", 0},
	{"SDIO0_CARD_DETECT", 1},
	{"EMMC_RST_N", 2},
	{"SFC_CSN1", 3},
	{"PWM1_OUT0", 5},
	{"I2C2_SDA", 6},
	{"UART2_RXD", 7},
};

static const struct func_entry GPIO3_6_funcs[] __initconst = {
	{"GPIO3_6", 0},
	{"SDIO0_CDATA1", 1},
	{"EMMC_DATA1", 1},
	{"SPI0_CSN1", 4},
	{"PWM1_OUT1", 5},
	{"I2C2_SCL", 6},
	{"UART2_TXD", 7},
};

static const struct func_entry GPIO3_7_funcs[] __initconst = {
	{"GPIO3_7", 0},
	{"SDIO0_CDATA0", 1},
	{"EMMC_DATA0", 1},
	{"SPI0_SCLK", 4},
	{"PWM1_OUT2", 5},
	{"SENSOR1_RSTN", 7},
};

static const struct func_entry GPIO4_0_funcs[] __initconst = {
	{"GPIO4_0", 0},
	{"SDIO0_CCLK_OUT", 1},
	{"EMMC_CLK_OUT", 1},
	{"TEST_MODE", 2},
	{"SPI0_SDO", 4},
	{"PWM1_OUT3", 5},
	{"I2S_MCLK", 6},
};

static const struct func_entry GPIO4_1_funcs[] __initconst = {
	{"GPIO4_1", 0},
	{"SDIO0_CCMD", 1},
	{"EMMC_CMD", 1},
	{"SPI0_SDI", 4},
	{"PWM1_OUT4", 5},
	{"I2S_BCLK", 6},
	{"UART1_RXD", 7},
};

static const struct func_entry GPIO4_2_funcs[] __initconst = {
	{"GPIO4_2", 0},
	{"SDIO0_CDATA3", 1},
	{"EMMC_DATA3", 1},
	{"SPI0_CSN0", 4},
	{"PWM1_OUT5", 5},
	{"I2S_SD_TX", 6},
	{"UART1_TXD", 7},
};

static const struct func_entry GPIO4_3_funcs[] __initconst = {
	{"GPIO4_3", 0},
	{"SDIO0_CDATA2", 1},
	{"EMMC_DATA2", 1},
	{"UART2_RXD", 4},
	{"PWM1_OUT6", 5},
	{"I2S_WS", 6},
	{"UART1_RTSN", 7},
};

static const struct func_entry GPIO3_4_funcs[] __initconst = {
	{"GPIO3_4", 0},
	{"SDIO0_CARD_POWER_EN_N", 1},
	{"UART2_TXD", 4},
	{"PWM1_OUT7", 5},
	{"I2S_SD_RX", 6},
	{"UART1_CTSN", 7},
};

static const struct func_entry GPIO5_6_funcs[] __initconst = {
	{"MIPI_RX_CK1N", 0},
	{"GPIO5_6", 1},
	{"VI_BT1120_DATA8", 2},
	{"VI_DATA11", 3},
	{"I2C0_SDA", 4},
	{"PWM0_OUT1", 7},
};

static const struct func_entry GPIO5_7_funcs[] __initconst = {
	{"MIPI_RX_CK1P", 0},
	{"GPIO5_7", 1},
	{"VI_BT1120_DATA9", 2},
	{"VI_DATA10", 3},
	{"I2C0_SCL", 4},
	{"PWM0_OUT2", 7},
};

static const struct func_entry GPIO6_2_funcs[] __initconst = {
	{"MIPI_RX_D3N", 0},
	{"GPIO6_2", 1},
	{"VI_BT1120_DATA10", 2},
	{"VI_DATA9", 3},
	{"SENSOR0_RSTN", 7},
};

static const struct func_entry GPIO6_3_funcs[] __initconst = {
	{"MIPI_RX_D3P", 0},
	{"GPIO6_3", 1},
	{"VI_BT1120_DATA11", 2},
	{"VI_DATA8", 3},
	{"PWM0_OUT1", 7},
};

static const struct func_entry GPIO6_0_funcs[] __initconst = {
	{"MIPI_RX_D1N", 0},
	{"GPIO6_0", 1},
	{"VI_BT1120_DATA12", 2},
	{"VI_CLK", 3},
	{"PWM0_OUT2", 7},
};

static const struct func_entry GPIO6_1_funcs[] __initconst = {
	{"MIPI_RX_D1P", 0},
	{"GPIO6_1", 1},
	{"VI_BT1120_DATA13", 2},
	{"VI_DATA7", 3},
	{"PWM0_OUT3", 7},
};

static const struct func_entry GPIO5_0_funcs[] __initconst = {
	{"MIPI_RX_CK0N", 0},
	{"GPIO5_0", 1},
	{"VI_BT1120_DATA0", 2},
	{"VI_DATA5", 3},
	{"VI_CLK", 6},
	{"I2C0_SDA", 7},
};

static const struct func_entry GPIO5_1_funcs[] __initconst = {
	{"MIPI_RX_CK0P", 0},
	{"GPIO5_1", 1},
	{"VI_BT1120_DATA1", 2},
	{"VI_DATA6", 3},
	{"VI_DATA7", 6},
	{"I2C0_SCL", 7},
};

static const struct func_entry GPIO5_2_funcs[] __initconst = {
	{"MIPI_RX_D0N", 0},
	{"GPIO5_2", 1},
	{"VI_BT1120_DATA2", 2},
	{"VI_DATA4", 3},
	{"SENSOR0_RSTN", 7},
};

static const struct func_entry GPIO5_3_funcs[] __initconst = {
	{"MIPI_RX_D0P", 0},
	{"GPIO5_3", 1},
	{"VI_BT1120_DATA3", 2},
	{"VI_DATA3", 3},
	{"VI_DATA5", 6},
	{"PWM0_OUT1", 7},
};

static const struct func_entry GPIO5_4_funcs[] __initconst = {
	{"MIPI_RX_D2N", 0},
	{"GPIO5_4", 1},
	{"VI_BT1120_DATA4", 2},
	{"VI_DATA2", 3},
	{"VI_DATA6", 6},
	{"PWM0_OUT2", 7},
};

static const struct func_entry GPIO5_5_funcs[] __initconst = {
	{"MIPI_RX_D2P", 0},
	{"GPIO5_5", 1},
	{"VI_BT1120_DATA5", 2},
	{"VI_DATA1", 3},
	{"VI_DATA3", 6},
	{"PWM0_OUT3", 7},
};

static const struct func_entry GPIO6_5_funcs[] __initconst = {
	{"GPIO6_5", 0},
	{"SENSOR1_CLK", 1},
	{"FAST_BOOT_MODE", 5},
	{"SENSOR0_RSTN", 7},
};

static const struct func_entry GPIO6_4_funcs[] __initconst = {
	{"TEST_CLK", 0},
	{"SENSOR0_CLK", 1},
	{"GPIO6_4", 3},
	{"SFC_EMMC_BOOT_MODE", 5},
};

static const struct func_entry GPIO7_3_funcs[] __initconst = {
	{"GPIO7_3", 0},
	{"VI_BT1120_DATA6", 1},
	{"PWM0_OUT1", 4},
	{"SENSOR1_RSTN", 5},
	{"VI_DATA0", 6},
};

static const struct func_entry GPIO7_2_funcs[] __initconst = {
	{"GPIO7_2", 0},
	{"VI_BT1120_DATA7", 1},
	{"PWM0_OUT2", 4},
	{"SENSOR0_RSTN", 5},
	{"VI_VS", 6},
};

static const struct func_entry GPIO7_0_funcs[] __initconst = {
	{"GPIO7_0", 0},
	{"VI_BT1120_CLK", 1},
	{"SPI0_CSN0", 3},
	{"I2C1_SDA", 5},
	{"VI_HS", 6},
	{"SENSOR0_HS", 7},
};

static const struct func_entry GPIO7_1_funcs[] __initconst = {
	{"GPIO7_1", 0},
	{"VI_BT1120_DATA14", 1},
	{"SPI0_SDI", 3},
	{"SENSOR0_RSTN", 4},
	{"I2C1_SCL", 5},
	{"SENSOR0_VS", 7},
};

static const struct func_entry GPIO6_6_funcs[] __initconst = {
	{"GPIO6_6", 0},
	{"VI_BT1120_DATA15", 1},
	{"ETH_LINK_STA_LED", 2},
	{"SPI0_SDO", 3},
	{"ETH_STA_ACT_LED", 4},
	{"I2C0_SDA", 5},
	{"VI_DATA1", 6},
	{"SENSOR1_HS", 7},
};

static const struct func_entry GPIO6_7_funcs[] __initconst = {
	{"GPIO6_7", 0},
	{"ETH_LINK_ACT_LED", 2},
	{"SPI0_SCLK", 3},
	{"I2C0_SCL", 5},
	{"VI_DATA2", 6},
	{"SENSOR1_VS", 7},
};

static const struct func_entry GPIO1_5_funcs[] __initconst = {
	{"GPIO1_5", 0},
	{"I2C1_SDA", 1},
	{"PWM0_OUT2", 2},
	{"UART2_RXD", 3},
	{"LSADC_CH1", 4},
};

static const struct func_entry GPIO1_4_funcs[] __initconst = {
	{"GPIO1_4", 0},
	{"I2C1_SCL", 1},
	{"PWM0_OUT3", 2},
	{"UART2_TXD", 3},
	{"LSADC_CH0", 4},
};

static const struct func_entry GPIO0_6_funcs[] __initconst = {
	{"JTAG_TRSTN", 0},
	{"ETH_LINK_STA_LED", 1},
	{"GPIO0_6", 2},
	{"I2C0_SCL", 3},
	{"UART1_RXD", 5},
	{"SPI0_CSN1", 6},
	{"PWM0_OUT1", 7},
};

static const struct func_entry GPIO0_7_funcs[] __initconst = {
	{"JTAG_TDI", 0},
	{"ETH_LINK_ACT_LED", 1},
	{"ETH_STA_ACT_LED", 2},
	{"I2C0_SDA", 3},
	{"USB2_PWREN", 4},
	{"UART1_TXD", 5},
	{"GPIO0_7", 6},
	{"PWM0_OUT2", 7},
};

static const struct func_entry GPIO7_7_funcs[] __initconst = {
	{"GPIO7_7", 0},
	{"SDIO1_CDATA2", 1},
	{"I2S_MCLK", 3},
	{"UART1_RTSN", 5},
	{"SPI1_SDO", 6},
	{"PWM1_OUT0", 7},
};

static const struct func_entry GPIO1_0_funcs[] __initconst = {
	{"GPIO1_0", 0},
	{"SDIO1_CDATA3", 1},
	{"I2S_SD_TX", 3},
	{"UART1_CTSN", 5},
	{"SPI1_SDI", 6},
	{"PWM1_OUT1", 7},
};

static const struct func_entry GPIO1_2_funcs[] __initconst = {
	{"GPIO1_2", 0},
	{"SDIO1_CCMD", 1},
	{"I2S_BCLK", 3},
	{"SENSOR1_RSTN", 5},
	{"SPI1_CSN", 6},
	{"PWM1_OUT2", 7},
};

static const struct func_entry GPIO1_3_funcs[] __initconst = {
	{"JTAG_TCK", 0},
	{"SDIO1_CCLK_OUT", 1},
	{"GPIO1_3", 2},
	{"I2S_WS", 3},
	{"SPI1_SCLK", 6},
	{"PWM1_OUT3", 7},
};

static const struct func_entry GPIO1_1_funcs[] __initconst = {
	{"GPIO1_1", 0},
	{"SDIO1_CDATA0", 1},
	{"I2S_SD_RX", 3},
	{"UART2_RXD", 5},
	{"SPI0_SDO", 6},
	{"PWM1_OUT4", 7},
};

static const struct func_entry GPIO7_6_funcs[] __initconst = {
	{"GPIO7_6", 0},
	{"SDIO1_CDATA1", 1},
	{"UART2_TXD", 5},
	{"SPI0_SDI", 6},
	{"PWM1_OUT5", 7},
};

static const struct func_entry GPIO7_5_funcs[] __initconst = {
	{"JTAG_TMS", 0},
	{"SDIO1_CARD_DETECT", 1},
	{"SENSOR1_RSTN", 2},
	{"I2C2_SDA", 3},
	{"GPIO7_5", 5},
	{"SPI0_SCLK", 6},
	{"PWM1_OUT6", 7},
};

static const struct func_entry GPIO7_4_funcs[] __initconst = {
	{"JTAG_TDO", 0},
	{"SDIO1_CARD_POWER_EN_N", 1},
	{"I2C2_SCL", 3},
	{"GPIO7_4", 5},
	{"SPI0_CSN0", 6},
	{"PWM1_OUT7", 7},
};

static const struct func_entry GPIO0_2_funcs[] __initconst = {
	{"GPIO0_2", 0},
	{"UART0_RXD", 1},
};

static const struct func_entry GPIO0_3_funcs[] __initconst = {
	{"GPIO0_3", 0},
	{"UART0_TXD", 1},
};

static const struct func_entry GPIO2_0_funcs[] __initconst = {
	{"GPIO2_0", 0},
	{"SVB_PWM", 1},
};

const struct gpio_mux_group pin_func_mux[] __initconst = {
	{"GPIO0_0", GPIO0_0_funcs, ARRAY_SIZE(GPIO0_0_funcs)},
	{"GPIO0_1", GPIO0_1_funcs, ARRAY_SIZE(GPIO0_1_funcs)},
	{"SYS_RSTN_OUT", SYS_RSTN_OUT_funcs, ARRAY_SIZE(SYS_RSTN_OUT_funcs)},
	{"GPIO2_3", GPIO2_3_funcs, ARRAY_SIZE(GPIO2_3_funcs)},
	{"GPIO2_2", GPIO2_2_funcs, ARRAY_SIZE(GPIO2_2_funcs)},
	{"GPIO2_6", GPIO2_6_funcs, ARRAY_SIZE(GPIO2_6_funcs)},
	{"GPIO2_1", GPIO2_1_funcs, ARRAY_SIZE(GPIO2_1_funcs)},
	{"GPIO2_4", GPIO2_4_funcs, ARRAY_SIZE(GPIO2_4_funcs)},
	{"GPIO2_5", GPIO2_5_funcs, ARRAY_SIZE(GPIO2_5_funcs)},
	{"GPIO3_5", GPIO3_5_funcs, ARRAY_SIZE(GPIO3_5_funcs)},
	{"GPIO3_6", GPIO3_6_funcs, ARRAY_SIZE(GPIO3_6_funcs)},
	{"GPIO3_7", GPIO3_7_funcs, ARRAY_SIZE(GPIO3_7_funcs)},
	{"GPIO4_0", GPIO4_0_funcs, ARRAY_SIZE(GPIO4_0_funcs)},
	{"GPIO4_1", GPIO4_1_funcs, ARRAY_SIZE(GPIO4_1_funcs)},
	{"GPIO4_2", GPIO4_2_funcs, ARRAY_SIZE(GPIO4_2_funcs)},
	{"GPIO4_3", GPIO4_3_funcs, ARRAY_SIZE(GPIO4_3_funcs)},
	{"GPIO3_4", GPIO3_4_funcs, ARRAY_SIZE(GPIO3_4_funcs)},
	{"GPIO5_6", GPIO5_6_funcs, ARRAY_SIZE(GPIO5_6_funcs)},
	{"GPIO5_7", GPIO5_7_funcs, ARRAY_SIZE(GPIO5_7_funcs)},
	{"GPIO6_2", GPIO6_2_funcs, ARRAY_SIZE(GPIO6_2_funcs)},
	{"GPIO6_3", GPIO6_3_funcs, ARRAY_SIZE(GPIO6_3_funcs)},
	{"GPIO6_0", GPIO6_0_funcs, ARRAY_SIZE(GPIO6_0_funcs)},
	{"GPIO6_1", GPIO6_1_funcs, ARRAY_SIZE(GPIO6_1_funcs)},
	{"GPIO5_0", GPIO5_0_funcs, ARRAY_SIZE(GPIO5_0_funcs)},
	{"GPIO5_1", GPIO5_1_funcs, ARRAY_SIZE(GPIO5_1_funcs)},
	{"GPIO5_2", GPIO5_2_funcs, ARRAY_SIZE(GPIO5_2_funcs)},
	{"GPIO5_3", GPIO5_3_funcs, ARRAY_SIZE(GPIO5_3_funcs)},
	{"GPIO5_4", GPIO5_4_funcs, ARRAY_SIZE(GPIO5_4_funcs)},
	{"GPIO5_5", GPIO5_5_funcs, ARRAY_SIZE(GPIO5_5_funcs)},
	{"GPIO6_5", GPIO6_5_funcs, ARRAY_SIZE(GPIO6_5_funcs)},
	{"GPIO6_4", GPIO6_4_funcs, ARRAY_SIZE(GPIO6_4_funcs)},
	{"GPIO7_3", GPIO7_3_funcs, ARRAY_SIZE(GPIO7_3_funcs)},
	{"GPIO7_2", GPIO7_2_funcs, ARRAY_SIZE(GPIO7_2_funcs)},
	{"GPIO7_0", GPIO7_0_funcs, ARRAY_SIZE(GPIO7_0_funcs)},
	{"GPIO7_1", GPIO7_1_funcs, ARRAY_SIZE(GPIO7_1_funcs)},
	{"GPIO6_6", GPIO6_6_funcs, ARRAY_SIZE(GPIO6_6_funcs)},
	{"GPIO6_7", GPIO6_7_funcs, ARRAY_SIZE(GPIO6_7_funcs)},
	{"GPIO1_5", GPIO1_5_funcs, ARRAY_SIZE(GPIO1_5_funcs)},
	{"GPIO1_4", GPIO1_4_funcs, ARRAY_SIZE(GPIO1_4_funcs)},
	{"GPIO0_6", GPIO0_6_funcs, ARRAY_SIZE(GPIO0_6_funcs)},
	{"GPIO0_7", GPIO0_7_funcs, ARRAY_SIZE(GPIO0_7_funcs)},
	{"GPIO7_7", GPIO7_7_funcs, ARRAY_SIZE(GPIO7_7_funcs)},
	{"GPIO1_0", GPIO1_0_funcs, ARRAY_SIZE(GPIO1_0_funcs)},
	{"GPIO1_2", GPIO1_2_funcs, ARRAY_SIZE(GPIO1_2_funcs)},
	{"GPIO1_3", GPIO1_3_funcs, ARRAY_SIZE(GPIO1_3_funcs)},
	{"GPIO1_1", GPIO1_1_funcs, ARRAY_SIZE(GPIO1_1_funcs)},
	{"GPIO7_6", GPIO7_6_funcs, ARRAY_SIZE(GPIO7_6_funcs)},
	{"GPIO7_5", GPIO7_5_funcs, ARRAY_SIZE(GPIO7_5_funcs)},
	{"GPIO7_4", GPIO7_4_funcs, ARRAY_SIZE(GPIO7_4_funcs)},
	{"GPIO0_2", GPIO0_2_funcs, ARRAY_SIZE(GPIO0_2_funcs)},
	{"GPIO0_3", GPIO0_3_funcs, ARRAY_SIZE(GPIO0_3_funcs)},
	{"GPIO2_0", GPIO2_0_funcs, ARRAY_SIZE(GPIO2_0_funcs)},
};

/* ===== Table 3: Drive string -> code ===== */
const struct drive_entry drive_encode[] __initconst = {
	{"I02_level4", 0x0},
	{"I02_level3", 0x1},
	{"I02_level2", 0x2},
	{"I02_level1", 0x3},
	{"I06_level8", 0x0},
	{"I06_level7", 0x1},
	{"I06_level6", 0x2},
	{"I06_level5", 0x3},
	{"I06_level4", 0x4},
	{"I06_level3", 0x5},
	{"I06_level2", 0x6},
	{"I06_level1", 0x7},
	{"EMMC I0_level16", 0x0},
	{"EMMC I0_level15", 0x1},
	{"EMMC I0_level14", 0x2},
	{"EMMC I0_level13", 0x3},
	{"EMMC I0_level12", 0x4},
	{"EMMC I0_level11", 0x5},
	{"EMMC I0_level10", 0x6},
	{"EMMC I0_level9", 0x7},
	{"EMMC I0_level8", 0x8},
	{"EMMC I0_level7", 0x9},
	{"EMMC I0_level6", 0xA},
	{"EMMC I0_level5", 0xB},
	{"EMMC I0_level4", 0xC},
	{"EMMC I0_level3", 0xD},
	{"EMMC I0_level2", 0xE},
	{"EMMC I0_level1", 0xF},
};
