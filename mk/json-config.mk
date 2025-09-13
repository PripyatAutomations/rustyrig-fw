PROFILE ?= release
CF := config/${PROFILE}.config.json

ifeq (x$(wildcard ${CF}),x)
$(error ***ERROR*** Please create ${CF} first before building -- There is an example at doc/radio.json.example you can use)
endif

USE_GTK=true

OBJ_DIR := ./build

CHANNELS := config/${PROFILE}.channels.json
INSTALL_DIR = /usr/local
CONF_DIR := /etc/rustyrig

CFLAGS_WARN += $(strip $(shell cat ${CF} | jq -r ".build.cflags_warn"))
CFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.cflags"))
LDFLAGS += $(strip $(shell cat ${CF} | jq -r ".build.ldflags"))
TC_PREFIX := $(strip $(shell cat ${CF} | jq -r ".build.toolchain.prefix"))
EEPROM_SIZE := $(strip $(shell cat ${CF} | jq -r ".eeprom.size"))
#EEPROM_FILE := eeprom.bin
PLATFORM := $(strip $(shell cat ${CF} | jq -r ".build.platform"))
USE_GSTREAMER = $(strip $(shell cat ${CF} | jq -r '.features.gstreamer'))
USE_HAMLIB = $(strip $(shell cat ${CF} | jq -r '.backend.hamlib'))
USE_LIBUNWIND = $(strip $(shell cat ${CF} | jq -r ".features.libunwind"))
USE_SQLITE = $(strip $(shell cat ${CF} | jq -r '.features.sqlite'))
USE_SSL = $(strip $(shell cat ${CF} | jq -r ".net.http.tls_enabled"))
USE_GTK = $(strip $(shell cat ${CF} | jq -r ".features.gtk"))

${CF}:
	@echo "********************************************************"
	@echo "* PLEASE read README.txt and edit ${CF} as needed *"
	@echo "********************************************************"
	exit 1
