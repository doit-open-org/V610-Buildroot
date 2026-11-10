################################################################################
#
# touch-driver
#
################################################################################

TOUCH_DRIVER_VERSION = 1.0
TOUCH_DRIVER_SITE = $(TOUCH_DRIVER_PKGDIR)/src
TOUCH_DRIVER_SITE_METHOD = local
TOUCH_DRIVER_LICENSE = GPL-2.0
TOUCH_DRIVER_DEPENDENCIES = linux board-io-cfg

# The driver source is taken from the kernel tree being built.
define TOUCH_DRIVER_BUILD_CMDS
	cp -f $(LINUX_DIR)/drivers/input/touchscreen/edt-ft5x06.c $(@D)/
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
endef

ifeq ($(BR2_PACKAGE_TOUCH_DRIVER_AUTOLOAD),y)
define TOUCH_DRIVER_INSTALL_INIT
	$(INSTALL) -D -m 0755 $(TOUCH_DRIVER_PKGDIR)/files/S31touch \
		$(TARGET_DIR)/etc/init.d/S31touch
endef
endif

define TOUCH_DRIVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/edt-ft5x06.ko $(TARGET_DIR)/drv/ko/extdrv/edt-ft5x06.ko
	$(TARGET_STRIP) --strip-debug $(TARGET_DIR)/drv/ko/extdrv/edt-ft5x06.ko
	$(INSTALL) -D -m 0755 $(TOUCH_DRIVER_PKGDIR)/files/touch_driver_ctl \
		$(TARGET_DIR)/usr/sbin/touch_driver_ctl
	$(INSTALL) -D -m 0644 $(TOUCH_DRIVER_PKGDIR)/files/touch-driver.conf \
		$(TARGET_DIR)/etc/touch-driver.conf
	$(TOUCH_DRIVER_INSTALL_INIT)
endef

$(eval $(generic-package))
