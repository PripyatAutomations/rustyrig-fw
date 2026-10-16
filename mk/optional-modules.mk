# Validate profile-selected optional SDKs for native and Debian builds.
include packaging/optional-modules.mk
PKG_CONFIG ?= pkg-config
OPTIONAL_CONFIG := $(if $(CF),$(CF),config/$(if $(PROFILE),$(PROFILE),radio).config.json)
ifeq ($(wildcard $(OPTIONAL_CONFIG)),)
$(error Build profile not found: $(OPTIONAL_CONFIG))
endif

# auto follows the profile; yes/no are explicit build-plan overrides.
$(foreach m,$(OPTIONAL_MODULES),$(eval $(OPTIONAL_$(m)_SWITCH) ?= auto))
# Retain explicit USE_FOO command-line overrides used by older build scripts.
$(foreach m,$(OPTIONAL_MODULES),$(if $(and $(filter command line,$(origin $(OPTIONAL_$(m)_FEATURE))),$(filter auto,$($(OPTIONAL_$(m)_SWITCH)))),$(eval $(OPTIONAL_$(m)_SWITCH) := $($(OPTIONAL_$(m)_FEATURE)))))
$(foreach m,$(OPTIONAL_MODULES),$(if $(filter auto yes no true false 1 0,$($(OPTIONAL_$(m)_SWITCH))),,$(error $(OPTIONAL_$(m)_SWITCH) must be auto, yes or no)))
$(foreach m,$(OPTIONAL_MODULES),$(eval OPTIONAL_$(m)_AVAILABLE := $(shell $(PKG_CONFIG) --exists $(OPTIONAL_$(m)_PKGCONFIG) 2>/dev/null && echo true || echo false)))
$(foreach m,$(OPTIONAL_MODULES),$(eval OPTIONAL_$(m)_REQUESTED := $(shell jq -r '$(OPTIONAL_$(m)_CONFIG) // false' $(OPTIONAL_CONFIG))))
$(foreach m,$(OPTIONAL_MODULES),$(if $(filter true false,$(OPTIONAL_$(m)_REQUESTED)),,$(error $(OPTIONAL_$(m)_CONFIG) in $(OPTIONAL_CONFIG) must be true or false)))
$(foreach m,$(OPTIONAL_MODULES),$(eval OPTIONAL_$(m)_ENABLED := $(if $(filter no false 0,$($(OPTIONAL_$(m)_SWITCH))),false,$(if $(filter yes true 1,$($(OPTIONAL_$(m)_SWITCH))),true,$(if $(filter true,$(OPTIONAL_$(m)_REQUESTED)),true,false)))))
# Cleaning must work even on a host whose build dependencies have been removed.
OPTIONAL_CLEAN_GOALS := clean distclean override_dh_auto_clean
ifneq ($(strip $(MAKECMDGOALS)),)
ifeq ($(strip $(filter-out $(OPTIONAL_CLEAN_GOALS),$(MAKECMDGOALS))),)
OPTIONAL_SKIP_PREFLIGHT := true
endif
endif
ifneq ($(OPTIONAL_SKIP_PREFLIGHT),true)
$(foreach m,$(OPTIONAL_MODULES),$(if $(and $(filter true,$(OPTIONAL_$(m)_ENABLED)),$(filter false,$(OPTIONAL_$(m)_AVAILABLE))),$(warning $(OPTIONAL_$(m)_LABEL) was selected for $(OPTIONAL_CONFIG), but its development package is missing: $(OPTIONAL_$(m)_DEB_DEPS))$(error Install it with 'sudo apt install $(OPTIONAL_$(m)_DEB_DEPS)' or disable $(OPTIONAL_$(m)_CONFIG) in $(OPTIONAL_CONFIG). Stopping before building an incomplete package set)))
endif
ENABLED_OPTIONAL_MODULES := $(foreach m,$(OPTIONAL_MODULES),$(if $(filter true,$(OPTIONAL_$(m)_ENABLED)),$(m)))
DISABLED_OPTIONAL_MODULES := $(filter-out $(ENABLED_OPTIONAL_MODULES),$(OPTIONAL_MODULES))
OPTIONAL_MAKE_FLAGS := $(foreach m,$(OPTIONAL_MODULES),$(OPTIONAL_$(m)_FEATURE)=$(OPTIONAL_$(m)_ENABLED))
OPTIONAL_DH_EXCLUDES := $(foreach m,$(DISABLED_OPTIONAL_MODULES),-N$(OPTIONAL_$(m)_PACKAGE))
