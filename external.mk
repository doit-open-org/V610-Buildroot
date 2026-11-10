include $(sort $(wildcard $(BR2_EXTERNAL_CV610_PATH)/package/*/*.mk))

# With a merged /usr the staging /lib is a link to usr/lib. A toolchain whose
# lib/ld-musl-arm.so.1 is a relative link out of that directory (such as
# ../usr/lib/libc.so) is left with a dangling loader link once copied there.
ifeq ($(BR2_ROOTFS_MERGED_USR)$(BR2_TOOLCHAIN_EXTERNAL_CUSTOM_MUSL),yy)
define CV610_FIX_MUSL_LOADER_LINK
	if [ -L $(STAGING_DIR)/usr/lib/ld-musl-arm.so.1 ] && \
	   [ ! -e $(STAGING_DIR)/usr/lib/ld-musl-arm.so.1 ]; then \
		ln -sf libc.so $(STAGING_DIR)/usr/lib/ld-musl-arm.so.1; \
	fi
endef
TOOLCHAIN_EXTERNAL_CUSTOM_POST_INSTALL_STAGING_HOOKS += CV610_FIX_MUSL_LOADER_LINK
endif

# The HiSilicon v01c02 toolchain replaces some musl facilities with separate
# static libraries (GNU libiconv, argp, fts, obstack, ...) and ships each one
# twice: lib<x>.a without PIC and lib<x>_pic.a. Packages link plain -l<x>,
# which fails for shared libraries ("read-only segment has dynamic
# relocations"), so the staging copy uses the PIC variant under both names.
ifeq ($(BR2_TOOLCHAIN_EXTERNAL_CUSTOM),y)
define CV610_USE_PIC_TOOLCHAIN_LIBS
	for pic in $(STAGING_DIR)/usr/lib/lib*_pic.a; do \
		[ -f "$$pic" ] || continue; \
		cp -f "$$pic" "$${pic%_pic.a}.a"; \
	done
endef
TOOLCHAIN_EXTERNAL_CUSTOM_POST_INSTALL_STAGING_HOOKS += CV610_USE_PIC_TOOLCHAIN_LIBS
endif
