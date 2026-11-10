################################################################################
#
# aic8800d-sdio
#
################################################################################

AIC8800D_SDIO_VERSION = V5.0_2026_0509
AIC8800D_SDIO_SOURCE = aic8800d_sdio_sdk_$(AIC8800D_SDIO_VERSION).tar.gz
AIC8800D_SDIO_SITE = file://$(AIC8800D_SDIO_PKGDIR)
AIC8800D_SDIO_LICENSE = GPL-2.0 (driver), proprietary (firmware)
AIC8800D_SDIO_REDISTRIBUTE = NO
AIC8800D_SDIO_DEPENDENCIES = linux

AIC8800D_SDIO_DRIVER_DIR = $(@D)/driver/aic8800

# The BLE transport is only compiled for the driver's "Ubuntu" platform.
ifeq ($(BR2_PACKAGE_AIC8800D_SDIO_BLE),y)
AIC8800D_SDIO_BLE_OPT = y
else
AIC8800D_SDIO_BLE_OPT = n
endif

# Driver build switches for the Hi3516CV610: SDIO transport, firmware under
# /lib/firmware/aic8800, no preallocated buffers or reserved memory.
AIC8800D_SDIO_MAKE_OPTS = \
	$(LINUX_MAKE_FLAGS) \
	PWD=$(AIC8800D_SDIO_DRIVER_DIR) \
	KDIR=$(LINUX_DIR) \
	CONFIG_PLATFORM_UBUNTU=$(AIC8800D_SDIO_BLE_OPT) \
	CONFIG_SDIO_BT=$(AIC8800D_SDIO_BLE_OPT) \
	CONFIG_AIC8800_BTLPM_SUPPORT=n \
	CONFIG_AIC8800_WLAN_SUPPORT=m \
	CONFIG_AIC_WLAN_SUPPORT=m \
	CONFIG_AIC_FW_PATH=/lib/firmware/aic8800 \
	CONFIG_PREALLOC_RX_SKB=n \
	CONFIG_PREALLOC_TXQ=n \
	CONFIG_RESV_MEM_SUPPORT=n

define AIC8800D_SDIO_BUILD_CMDS
	$(LINUX_MAKE_ENV) $(MAKE) -C $(AIC8800D_SDIO_DRIVER_DIR) $(AIC8800D_SDIO_MAKE_OPTS)
endef

# S51wireless loads the kernel Bluetooth stack from the same directory as
# the driver, and takes the presence of bluetooth.ko there as the switch
# to the combined Wi-Fi/BLE firmware.
ifeq ($(BR2_PACKAGE_AIC8800D_SDIO_BLE),y)
define AIC8800D_SDIO_INSTALL_BLE
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/drv/ko/extdrv \
		$(LINUX_DIR)/crypto/ecc.ko \
		$(LINUX_DIR)/crypto/ecdh_generic.ko \
		$(LINUX_DIR)/net/bluetooth/bluetooth.ko
	$(INSTALL) -D -m 0755 $(AIC8800D_SDIO_PKGDIR)/files/ble-service \
		$(TARGET_DIR)/usr/sbin/ble-service
endef
endif

# The modules live next to the MPP ones under /drv/ko, where the
# start-up script loads them with insmod and explicit parameters.
define AIC8800D_SDIO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(AIC8800D_SDIO_DRIVER_DIR)/aic8800_bsp/aic8800_bsp.ko \
		$(TARGET_DIR)/drv/ko/extdrv/aic8800_bsp.ko
	$(INSTALL) -D -m 0644 $(AIC8800D_SDIO_DRIVER_DIR)/aic8800_fdrv/aic8800_fdrv.ko \
		$(TARGET_DIR)/drv/ko/extdrv/aic8800_fdrv.ko
	$(AIC8800D_SDIO_INSTALL_BLE)
	$(TARGET_STRIP) --strip-debug $(TARGET_DIR)/drv/ko/extdrv/*.ko
	mkdir -p $(TARGET_DIR)/lib/firmware/aic8800
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/lib/firmware/aic8800 \
		$(AIC8800D_SDIO_PKGDIR)/firmware/*
	$(INSTALL) -D -m 0755 $(AIC8800D_SDIO_PKGDIR)/files/S51wireless \
		$(TARGET_DIR)/etc/init.d/S51wireless
	$(INSTALL) -D -m 0755 $(AIC8800D_SDIO_PKGDIR)/files/wifi-service \
		$(TARGET_DIR)/usr/sbin/wifi-service
	$(INSTALL) -D -m 0755 $(AIC8800D_SDIO_PKGDIR)/files/start_wifi.sh \
		$(TARGET_DIR)/root/start_wifi.sh
	$(INSTALL) -D -m 0644 $(AIC8800D_SDIO_PKGDIR)/files/wpa_supplicant.conf \
		$(TARGET_DIR)/etc/wireless/wpa_supplicant.conf
endef

$(eval $(generic-package))
