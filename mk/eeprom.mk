# Desktop/server builds need a build configuration header, not an EEPROM image.
# The old pack-eeprom.pl pipeline remains opt-in for embedded firmware builds.
.PHONY: build-options-check
${BUILD_DIR}/.build-options: build-options-check
	@mkdir -p "${BUILD_DIR}"
	@printf 'eeprom=%s\nmongoose=%s\n' '$(USE_EEPROM)' '$(USE_MONGOOSE)' > "$@.tmp"
	@cmp -s "$@.tmp" "$@" || mv -f "$@.tmp" "$@"
	@rm -f "$@.tmp"

ifeq ($(USE_EEPROM),true)
EEPROM_GENERATED := ${BUILD_DIR}/build_config.h ${BUILD_DIR}/filter_tables.h ${BUILD_DIR}/atu_tables.h ${BUILD_DIR}/eeprom_types.h ${BUILD_DIR}/eeprom_layout.h ${EEPROM_FILE}
EEPROM_REQUIRED := ${EEPROM_FILE}
# Grouped targets: only one packer invocation with parallel make -j.
${EEPROM_GENERATED} &: tools/pack-eeprom.pl .version ${CF} ${BUILD_DIR}/.build-options $(wildcard res/*.json)
	@echo "[pack-eeprom] PROFILE=$(PROFILE)"
	@RUSTYRIG_USE_MONGOOSE='$(USE_MONGOOSE)' RUSTYRIG_USE_EEPROM='$(USE_EEPROM)' ./tools/pack-eeprom.pl '$(PROFILE)'
else
EEPROM_GENERATED := ${BUILD_DIR}/build_config.h ${BUILD_DIR}/filter_tables.h ${BUILD_DIR}/atu_tables.h
EEPROM_REQUIRED :=
# The Python generator writes all three headers in one pass. Grouped targets
# prevent -j builds from racing the server's generated includes.
${EEPROM_GENERATED} &: tools/gen-build-config.py .version ${CF} ${BUILD_DIR}/.build-options
	@echo "[gen-build-config] PROFILE=$(PROFILE), Mongoose=$(USE_MONGOOSE), EEPROM=disabled"
	@python3 tools/gen-build-config.py '$(PROFILE)' --mongoose '$(USE_MONGOOSE)' --output "${BUILD_DIR}/build_config.h"
endif
