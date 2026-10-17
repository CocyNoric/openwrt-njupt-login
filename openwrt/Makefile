include $(TOPDIR)/rules.mk

PKG_NAME:=openwrt-njupt-login
PKG_VERSION:=0.1.0
PKG_RELEASE:=4
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE
PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/openwrt-njupt-login
  SECTION:=net
  CATEGORY:=Network
  TITLE:=NJUPT portal login and WAN heartbeat
  URL:=https://github.com/CocyNoric/openwrt-njupt-login
  DEPENDS:=+libstdcpp +libcurl +ca-bundle +ip-full +flock
endef

define Package/openwrt-njupt-login/description
 Native C++ client for the NJUPT ACSetting/eportal HTTP protocol.
 Includes UCI/procd, WAN policy routing and OpenClash socket mark exclusions.
endef

define Package/openwrt-njupt-login/conffiles
/etc/config/njupt-login
endef

# Copy the source tree to SDK/package/openwrt-njupt-login; this is the package entry.
define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(CURDIR)/src $(CURDIR)/CMakeLists.txt $(CURDIR)/LICENSE $(PKG_BUILD_DIR)/
endef

CMAKE_OPTIONS += -DBUILD_TESTING=OFF

define Package/openwrt-njupt-login/install
	$(INSTALL_DIR) $(1)/usr/bin $(1)/usr/libexec/njupt-login $(1)/etc/init.d $(1)/etc/config $(1)/etc/hotplug.d/iface
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/njupt-login $(1)/usr/bin/
	$(INSTALL_BIN) $(CURDIR)/openwrt/files/njupt-login.init $(1)/etc/init.d/njupt-login
	$(INSTALL_BIN) $(CURDIR)/openwrt/files/network.sh $(1)/usr/libexec/njupt-login/network
	$(INSTALL_BIN) $(CURDIR)/openwrt/files/openclash-bypass.sh $(1)/usr/libexec/njupt-login/openclash-bypass
	$(INSTALL_DATA) $(CURDIR)/openwrt/files/njupt-login.hotplug $(1)/etc/hotplug.d/iface/95-njupt-login
	$(INSTALL_CONF) $(CURDIR)/openwrt/files/njupt-login.config $(1)/etc/config/njupt-login
	chmod 600 $(1)/etc/config/njupt-login
endef

$(eval $(call BuildPackage,openwrt-njupt-login))
