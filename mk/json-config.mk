
LDFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.ldflags"))
TC_PREFIX := $(strip $(shell cat ${CF} | jq -r ".build.toolchain.prefix"))
EEPROM_SIZE := $(strip $(shell cat ${CF} | jq -r ".eeprom.size"))
EEPROM_FILE := ${BUILD_DIR}/eeprom.bin
PLATFORM := $(strip $(shell cat ${CF} | jq -r ".build.platform"))
USE_GSTREAMER = $(strip $(shell cat ${CF} | jq -r '.features.gstreamer'))
USE_HAMLIB = $(strip $(shell cat ${CF} | jq -r '.backend.hamlib'))
USE_LIBUNWIND = $(strip $(shell cat ${CF} | jq -r ".features.libunwind"))
USE_SQLITE = $(strip $(shell cat ${CF} | jq -r '.features.sqlite'))
USE_SSL = $(strip $(shell cat ${CF} | jq -r ".net.http.tls_enabled"))
USE_GTK = $(strip $(shell cat ${CF} | jq -r ".features.gtk"))
