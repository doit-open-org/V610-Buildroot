################################################################################
#
# board-io-cfg
#
################################################################################

BOARD_IO_CFG_VERSION = 1.0
BOARD_IO_CFG_SITE = $(BOARD_IO_CFG_PKGDIR)/src
BOARD_IO_CFG_SITE_METHOD = local
BOARD_IO_CFG_LICENSE = GPL-2.0
BOARD_IO_CFG_DEPENDENCIES = linux

define BOARD_IO_CFG_BUILD_CMDS
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
endef

# Next to the MPP modules, under the name the control scripts load.
define BOARD_IO_CFG_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/board_io_cfg.ko $(TARGET_DIR)/drv/ko/ot_board_io_cfg.ko
	$(TARGET_STRIP) --strip-debug $(TARGET_DIR)/drv/ko/ot_board_io_cfg.ko
endef

$(eval $(generic-package))
