################################################################################
#
# hisi-osdrv
#
################################################################################

HISI_OSDRV_VERSION = tag_V2.1.0_Beta
HISI_OSDRV_TARBALL = $(call qstrip,$(BR2_PACKAGE_HISI_OSDRV_TARBALL))
HISI_OSDRV_SOURCE = $(notdir $(HISI_OSDRV_TARBALL))
HISI_OSDRV_SITE = file://$(patsubst %/,%,$(dir $(HISI_OSDRV_TARBALL)))
HISI_OSDRV_LICENSE = Apache-2.0 (scripts, samples), GPL-2.0 (kernel modules), HiSilicon EULA (libraries)
HISI_OSDRV_LICENSE_FILES = LICENSE NOTICE
HISI_OSDRV_REDISTRIBUTE = NO
HISI_OSDRV_DEPENDENCIES = linux
HISI_OSDRV_INSTALL_STAGING = YES

# The kernel and U-Boot are built by their own packages.
HISI_OSDRV_EXCLUDES = \
	Hi3516CV610/third_party/linux/*.tgz \
	Hi3516CV610/third_party/u-boot/*.tgz \
	Hi3516CV610/third_party/busybox/*.tgz

HISI_OSDRV_SENSOR_TREE = soc/drivers/mpp/cbb/isp/user/sensor/hi3516cv610
HISI_OSDRV_SENSORS_DIR = $(call qstrip,$(BR2_PACKAGE_HISI_OSDRV_SENSORS_DIR))

ifneq ($(HISI_OSDRV_SENSORS_DIR),)
define HISI_OSDRV_ADD_SENSORS
	cp -a $(HISI_OSDRV_SENSORS_DIR)/. $(@D)/$(HISI_OSDRV_SENSOR_TREE)/
endef
HISI_OSDRV_POST_EXTRACT_HOOKS += HISI_OSDRV_ADD_SENSORS
endif

# The sensor API header is patched in the driver tree; the public copy
# under soc/include is what samples and applications compile against.
define HISI_OSDRV_SYNC_SENSOR_HEADER
	cp -f $(@D)/soc/drivers/mpp/cbb/isp/include/arch/hi3516cv610/include/ot_sns_ctrl.h \
		$(@D)/soc/include/ot_sns_ctrl.h
endef
HISI_OSDRV_POST_PATCH_HOOKS += HISI_OSDRV_SYNC_SENSOR_HEADER

# The vendor makefiles take the compiler from CROSS/CROSS_COMPILE and
# the kernel tree from KERNEL_ROOT; everything else comes from
# soc/config/gcc/cfg.mak.
HISI_OSDRV_MAKE_OPTS = \
	KERNEL_ROOT=$(LINUX_DIR) \
	CROSS=$(TARGET_CROSS) \
	CROSS_COMPILE=$(TARGET_CROSS)

ifeq ($(BR2_PACKAGE_HISI_OSDRV_SAMPLES),y)
HISI_OSDRV_SAMPLE_TARGETS = all
else
HISI_OSDRV_SAMPLE_TARGETS = sdk_server
endif

# Vendor register tools (bspmm, bspmd, i2c_read, ...), all links to btools.
HISI_OSDRV_REGTOOLS_DIR = $(@D)/boards/dmeb/tools/board/reg-tools-1.0.0
HISI_OSDRV_REGTOOLS = bspmd bspmd.l bspmm bspddrs i2c_read i2c_write ssp_read ssp_write

define HISI_OSDRV_BUILD_REGTOOLS
	cp -rf $(@D)/soc/platform/securec/include/* $(HISI_OSDRV_REGTOOLS_DIR)/include/common/
	cp -f $(@D)/soc/libs/musl/libsecurec.so $(HISI_OSDRV_REGTOOLS_DIR)/source/common/
	$(TARGET_MAKE_ENV) $(MAKE1) -C $(HISI_OSDRV_REGTOOLS_DIR) \
		OSDRV_CROSS=$(patsubst %-,%,$(TARGET_CROSS)) all
endef

define HISI_OSDRV_INSTALL_REGTOOLS
	$(INSTALL) -D -m 0755 $(HISI_OSDRV_REGTOOLS_DIR)/bin/btools $(TARGET_DIR)/usr/bin/btools
	$(foreach t,$(HISI_OSDRV_REGTOOLS),ln -sf btools $(TARGET_DIR)/usr/bin/$(t)
	)
endef

define HISI_OSDRV_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/soc/drivers $(HISI_OSDRV_MAKE_OPTS)
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/samples/mpp $(HISI_OSDRV_MAKE_OPTS) \
		$(HISI_OSDRV_SAMPLE_TARGETS)
	$(HISI_OSDRV_BUILD_REGTOOLS)
endef

define HISI_OSDRV_INSTALL_STAGING_CMDS
	mkdir -p $(HOST_DIR)/share/v610-platform
	printf '%s\n' '{"sdk": "Hi3516CV610", "version": "$(HISI_OSDRV_VERSION)", "archive": "$(HISI_OSDRV_SOURCE)", "buildroot_package": "hisi-osdrv"}' > $(HOST_DIR)/share/v610-platform/hisilicon-source.json
	mkdir -p $(STAGING_DIR)/usr/include/hisi $(STAGING_DIR)/usr/lib/pkgconfig
	cp -a $(@D)/soc/include/. $(STAGING_DIR)/usr/include/hisi/
	$(INSTALL) -m 0644 $(@D)/soc/config/gcc/autoconf.h $(STAGING_DIR)/usr/include/hisi/
	mkdir -p $(STAGING_DIR)/usr/include/hisi/sensor_common
	$(INSTALL) -m 0644 -t $(STAGING_DIR)/usr/include/hisi/sensor_common \
		$(@D)/$(HISI_OSDRV_SENSOR_TREE)/common/*.h
	cp -a $(@D)/soc/libs/musl/*.a $(@D)/soc/libs/musl/*.so* $(STAGING_DIR)/usr/lib/
	$(INSTALL) -m 0644 -t $(STAGING_DIR)/usr/lib/pkgconfig \
		$(HISI_OSDRV_PKGDIR)/files/hisi-mpp.pc \
		$(HISI_OSDRV_PKGDIR)/files/hisi-mpp-media.pc \
		$(HISI_OSDRV_PKGDIR)/files/hisi-svp.pc
endef

ifeq ($(BR2_PACKAGE_HISI_OSDRV_SAMPLES),y)
define HISI_OSDRV_INSTALL_SAMPLES
	mkdir -p $(TARGET_DIR)/opt/hisi/samples
	find $(@D)/samples/mpp -type f -perm -u+x \
		-exec sh -c 'file -b "$$1" | grep -q "^ELF.*executable"' _ {} \; \
		-exec $(INSTALL) -m 0755 -t $(TARGET_DIR)/opt/hisi/samples {} +
endef
endif

define HISI_OSDRV_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/drv/ko/load_ko $(TARGET_DIR)/usr/lib
	$(INSTALL) -m 0644 -t $(TARGET_DIR)/drv/ko $(@D)/output/mpp/ko/*.ko
	$(TARGET_STRIP) --strip-debug $(TARGET_DIR)/drv/ko/*.ko
	$(INSTALL) -m 0755 -t $(TARGET_DIR)/drv/ko/load_ko \
		$(@D)/output/mpp/ko/load_ko/load3516cv610_20s \
		$(@D)/output/mpp/ko/load_ko/load3516cv610_20s_debug
	$(INSTALL) -m 0755 -t $(TARGET_DIR)/usr/lib $(@D)/soc/libs/musl/*.so*
	$(INSTALL) -D -m 0755 $(@D)/samples/mpp/sdk_server/sdk_server \
		$(TARGET_DIR)/usr/sbin/sdk_server
	$(HISI_OSDRV_INSTALL_REGTOOLS)
	$(HISI_OSDRV_INSTALL_SAMPLES)
endef

$(eval $(generic-package))
