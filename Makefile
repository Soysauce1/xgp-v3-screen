include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=xgp-v3-screen
PKG_VERSION:=2.1
PKG_RELEASE:=1
PKG_LICENSE:=GPL-3.0-only

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

# Kernel module package for GC9307 driver
define KernelPackage/fb-tft-gc9307
	SUBMENU:=Video Support
	TITLE:=FB driver for the GC9307 LCD Controller
	FILES:=$(PKG_BUILD_DIR)/kmod-fb-tft-gc9307/fb_gc9307.ko
	AUTOLOAD:=$(call AutoLoad,09,fb_gc9307)
	DEPENDS:=+kmod-fb-tft
endef

define KernelPackage/fb-tft-gc9307/description
	Framebuffer driver for the GC9307 LCD Controller.
	Integrated with xgp-v3-screen project.
endef

# Application package
define Package/xgp-v3-screen
	SECTION:=utils
	CATEGORY:=Utilities
	TITLE:=NLnet XiGuaPi V3 TFT Screen
	DEPENDS:=+python3 +libpthread +libstdcpp +luci-base +kmod-fb-tft-gc9307
	URL:=https://github.com/zzzz0317/xgp-v3-screen
endef

define Package/xgp-v3-screen/description
	NLnet XiGuaPi V3 TFT Screen with integrated LuCI Web UI control.
	Includes GC9307 LCD framebuffer driver (kmod-fb-tft-gc9307).
endef

define Package/xgp-v3-screen/conffiles
/etc/config/xgp_screen
endef

define Build/Prepare
	$(call Build/Prepare/Default)
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
	$(CP) ./kmod-fb-tft-gc9307 $(PKG_BUILD_DIR)/
	(cd $(PKG_BUILD_DIR) && \
	 git clone --depth 1 -b release/v9.3 https://github.com/lvgl/lvgl.git lvgl)
endef

define Build/Compile
	# Compile kernel module
	$(KERNEL_MAKE) M="$(PKG_BUILD_DIR)/kmod-fb-tft-gc9307/src" \
		EXTRA_CFLAGS="$(BUILDFLAGS)" \
		modules
	$(CP) $(PKG_BUILD_DIR)/kmod-fb-tft-gc9307/src/fb_gc9307.ko $(PKG_BUILD_DIR)/kmod-fb-tft-gc9307/
	# Compile application
	$(call Build/Compile/Default)
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/zz
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/bin/zz_xgp_screen $(1)/usr/zz/zz_xgp_screen
	$(INSTALL_BIN) ./files/modem_info.py $(1)/usr/zz/modem_info.py
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/zz_xgp_screen.init $(1)/etc/init.d/zz_xgp_screen
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/xgp_screen.config $(1)/etc/config/xgp_screen
	# LuCI files
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/controller
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/model/cbi
	$(INSTALL_DATA) ./luasrc/controller/xgp_screen.lua $(1)/usr/lib/lua/luci/controller/
	$(INSTALL_DATA) ./luasrc/model/cbi/xgp_screen.lua $(1)/usr/lib/lua/luci/model/cbi/
	# UCI defaults
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/uci-defaults-xgp-screen $(1)/etc/uci-defaults/99-xgp-screen
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/zz_xgp_screen enable
	/etc/init.d/zz_xgp_screen start
}
endef

define Package/$(PKG_NAME)/prerm
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/zz_xgp_screen stop
	/etc/init.d/zz_xgp_screen disable
}
endef

$(eval $(call KernelPackage,fb-tft-gc9307))
$(eval $(call BuildPackage,$(PKG_NAME)))