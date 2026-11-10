# Wrapper around Buildroot for the Hi3516CV610 SDK.
#
#   make cove_610_defconfig   select the board
#   make                      build everything
#   make app-sdk              build the application SDK archive under
#                             V610-App-SDK/dist
#   make <target>             any other Buildroot target (menuconfig,
#                             linux-rebuild, hisi-osdrv-rebuild, sdk, ...)
#
# Variables: O=<output dir> (default: output), BR2_DL_DIR (default: dl)

TOPDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILDROOT_VERSION := 2026.02.3
BUILDROOT_SHA256 := 5a59e7501b0b4ec52c41f4bfa79412320e0b37eae5f719605a258e8d0c6fc7fb
BUILDROOT_TARBALL := buildroot-$(BUILDROOT_VERSION).tar.xz
BUILDROOT_URL := https://buildroot.org/downloads/$(BUILDROOT_TARBALL)
BUILDROOT_DIR := $(TOPDIR)/buildroot

O ?= $(TOPDIR)/output

# Buildroot refuses a PATH holding spaces, which WSL adds through the
# Windows directories. Drop those entries; none of them is a build tool.
export PATH := $(shell printf '%s' "$$PATH" | tr ':' '\n' | grep -v '[[:space:]]' | paste -sd: -)
export BR2_DL_DIR ?= $(TOPDIR)/dl

VENDOR_TAG := tag_V2.1.0_Beta
VENDOR_DIR := $(TOPDIR)/vendor
VENDOR_SDK := $(VENDOR_DIR)/Hi3516CV610_$(VENDOR_TAG).tgz
VENDOR_LINUX := $(VENDOR_DIR)/linux-5.10.y-$(VENDOR_TAG).tgz
VENDOR_TOOLCHAIN := $(VENDOR_DIR)/gcc-10.3-arm-musl-x86-linux-26.06.1-codesize.tgz

BR_MAKE = $(MAKE) -C $(BUILDROOT_DIR) O=$(abspath $(O)) BR2_EXTERNAL=$(TOPDIR)

.PHONY: all setup help app-sdk
all: setup
	$(BR_MAKE)

# Application SDK: build the image, export grouped vendor files and installed
# target packages from its host sysroot, then pack the developer archive.
# The compiler is installed independently and is not included.
# V610-App-SDK/ is a repository of its own, checked out inside this one and
# ignored by it.
APP_SDK_DIR := $(TOPDIR)/V610-App-SDK

app-sdk: setup
	@test -x $(APP_SDK_DIR)/scripts/install-platform.sh || { \
		echo "V610-App-SDK is a separate repository; clone it into $(APP_SDK_DIR) first" >&2; \
		exit 1; }
	$(BR_MAKE)
	$(APP_SDK_DIR)/scripts/install-platform.sh $(abspath $(O))/host
	$(APP_SDK_DIR)/scripts/package.sh

help:
	@sed -n '1,/^$$/s/^# \{0,1\}//p' $(TOPDIR)/Makefile
	@echo "Vendor inputs under vendor/ are fetched by scripts/fetch-vendor.sh (see README.md)."

setup: $(BUILDROOT_DIR)/Makefile $(VENDOR_SDK) $(VENDOR_LINUX) $(VENDOR_TOOLCHAIN)

$(BUILDROOT_DIR)/Makefile:
	mkdir -p $(BR2_DL_DIR) $(BUILDROOT_DIR)
	test -f $(BR2_DL_DIR)/$(BUILDROOT_TARBALL) || \
		curl -fL --retry 3 -o $(BR2_DL_DIR)/$(BUILDROOT_TARBALL) $(BUILDROOT_URL)
	echo "$(BUILDROOT_SHA256)  $(BR2_DL_DIR)/$(BUILDROOT_TARBALL)" | sha256sum -c -
	tar -xf $(BR2_DL_DIR)/$(BUILDROOT_TARBALL) -C $(BUILDROOT_DIR) --strip-components=1

# The vendor inputs are produced from the official SDK repository; the
# script skips whatever is already there.
$(VENDOR_SDK) $(VENDOR_LINUX) $(VENDOR_TOOLCHAIN):
	$(TOPDIR)/scripts/fetch-vendor.sh $(VENDOR_TAG)

# Keep make from remaking this file through the catch-all rule below.
$(TOPDIR)/Makefile Makefile: ;

%: setup
	$(BR_MAKE) $@
