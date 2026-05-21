SMARTBREAKER_VERSION = 1.0
SMARTBREAKER_SITE = $(HOME)/workspace/btl
SMARTBREAKER_SITE_METHOD = local
SMARTBREAKER_DEPENDENCIES = mosquitto linux

# Bươc 1: Build file app_main (Driver sẽ do kernel-module tự động build)
define SMARTBREAKER_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D) app
endef

# Bươc 2: Copy file app_main và Kich ban tu khoi dong S99 vao he dieu hanh
define SMARTBREAKER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/app_main $(TARGET_DIR)/usr/bin/app_main
	$(INSTALL) -D -m 0755 package/smartbreaker/S99smartbreaker $(TARGET_DIR)/etc/init.d/S99smartbreaker
endef

$(eval $(kernel-module))
$(eval $(generic-package))
