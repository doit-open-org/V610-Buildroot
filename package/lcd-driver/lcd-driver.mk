################################################################################
#
# lcd-driver
#
################################################################################

LCD_DRIVER_VERSION = 1.0
LCD_DRIVER_SITE = $(LCD_DRIVER_PKGDIR)/src
LCD_DRIVER_SITE_METHOD = local
LCD_DRIVER_LICENSE = GPL-2.0
LCD_DRIVER_DEPENDENCIES = linux board-io-cfg

define LCD_DRIVER_BUILD_CMDS
	$(LINUX_MAKE_ENV) $(MAKE) $(LINUX_MAKE_FLAGS) -C $(LINUX_DIR) M=$(@D) modules
endef

ifeq ($(BR2_PACKAGE_LCD_DRIVER_AUTOLOAD),y)
define LCD_DRIVER_INSTALL_INIT
	$(INSTALL) -D -m 0755 $(LCD_DRIVER_PKGDIR)/files/S30lcd \
		$(TARGET_DIR)/etc/init.d/S30lcd
endef
endif

define LCD_DRIVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/st7789v.ko $(TARGET_DIR)/drv/ko/extdrv/st7789v.ko
	$(TARGET_STRIP) --strip-debug $(TARGET_DIR)/drv/ko/extdrv/st7789v.ko
	$(INSTALL) -D -m 0755 $(LCD_DRIVER_PKGDIR)/files/lcd_driver_ctl \
		$(TARGET_DIR)/usr/sbin/lcd_driver_ctl
	$(INSTALL) -D -m 0644 $(LCD_DRIVER_PKGDIR)/files/lcd-driver.conf \
		$(TARGET_DIR)/etc/lcd-driver.conf
	$(LCD_DRIVER_INSTALL_INIT)
endef

$(eval $(generic-package))
