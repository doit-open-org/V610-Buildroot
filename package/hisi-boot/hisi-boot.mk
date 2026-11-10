################################################################################
#
# hisi-boot
#
################################################################################

# Same vendor SDK tarball as hisi-osdrv.
HISI_BOOT_VERSION = tag_V2.1.0_Beta
HISI_BOOT_TARBALL = $(call qstrip,$(BR2_PACKAGE_HISI_OSDRV_TARBALL))
HISI_BOOT_SOURCE = $(notdir $(HISI_BOOT_TARBALL))
HISI_BOOT_SITE = file://$(patsubst %/,%,$(dir $(HISI_BOOT_TARBALL)))
HISI_BOOT_DL_SUBDIR = hisi-osdrv
HISI_BOOT_LICENSE = GPL-2.0+ (U-Boot), Apache-2.0 (GSL, tools)
HISI_BOOT_LICENSE_FILES = LICENSE NOTICE
HISI_BOOT_REDISTRIBUTE = NO
HISI_BOOT_INSTALL_TARGET = NO
HISI_BOOT_INSTALL_IMAGES = YES
HISI_BOOT_DEPENDENCIES = toolchain

HISI_BOOT_EXCLUDES = \
	Hi3516CV610/third_party/linux/*.tgz \
	Hi3516CV610/third_party/busybox/*.tgz \
	Hi3516CV610/soc/libs/*

define HISI_BOOT_EXTRACT_UBOOT
	$(TAR) -xzf $(@D)/third_party/u-boot/u-boot-2022.07.tgz \
		-C $(@D)/third_party/u-boot
endef
HISI_BOOT_POST_EXTRACT_HOOKS += HISI_BOOT_EXTRACT_UBOOT

HISI_BOOT_BSP_DIR = $(@D)/boards/dmeb
HISI_BOOT_REGBIN_XLSM = $(call qstrip,$(BR2_PACKAGE_HISI_BOOT_REGBIN_XLSM))

# An absolute path selects a board specific table kept outside the SDK.
ifneq ($(filter /%,$(HISI_BOOT_REGBIN_XLSM)),)
define HISI_BOOT_INSTALL_REGBIN_XLSM
	cp -f $(HISI_BOOT_REGBIN_XLSM) $(HISI_BOOT_BSP_DIR)/tools/pc/boot_tools/
endef
HISI_BOOT_POST_PATCH_HOOKS += HISI_BOOT_INSTALL_REGBIN_XLSM
endif

HISI_BOOT_DEBUG = $(if $(BR2_PACKAGE_HISI_BOOT_DEBUG),1,0)
HISI_BOOT_PUB_IMAGE = hi3516cv610$(if $(BR2_PACKAGE_HISI_BOOT_MEDIA_EMMC),_emmc)_image_$(if $(BR2_PACKAGE_HISI_BOOT_DEBUG),debug_)musl

# The vendor BSP makefile is bash only and not parallel safe. Its python
# tools need openpyxl and pycryptodome, which Buildroot's host python
# does not carry, so the build machine's interpreter is used.
# GSL and U-Boot are bare-metal: they are built with the toolchain's own
# compiler rather than Buildroot's wrapper, whose added -mcpu/-mfpu
# flags would change the code the vendor validated for early boot.
# An empty TOOLCHAIN_VERSION skips the vendor version check, which would
# otherwise stop and wait for a key press on any other compiler.
HISI_BOOT_MAKE_OPTS = \
	SHELL=/bin/bash \
	LIB_TYPE=musl \
	CHIP=hi3516cv610 \
	DEBUG=$(HISI_BOOT_DEBUG) \
	BOOT_MEDIA=$(call qstrip,$(BR2_PACKAGE_HISI_BOOT_MEDIA)) \
	OSDRV_CROSS=$(TOOLCHAIN_EXTERNAL_BIN)/$(TOOLCHAIN_EXTERNAL_PREFIX) \
	TOOLCHAIN_VERSION= \
	REGBIN_XLSM=$(notdir $(HISI_BOOT_REGBIN_XLSM)) \
	PYTHON_CMD=/usr/bin/python3

define HISI_BOOT_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE1) -C $(HISI_BOOT_BSP_DIR) $(HISI_BOOT_MAKE_OPTS) \
		prepare boot gslboot_build
endef

define HISI_BOOT_INSTALL_IMAGES_CMDS
	$(INSTALL) -D -m 0644 \
		$(HISI_BOOT_BSP_DIR)/pub/$(HISI_BOOT_PUB_IMAGE)/boot_image.bin \
		$(BINARIES_DIR)/boot_image.bin
endef

$(eval $(generic-package))
