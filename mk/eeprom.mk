# pack-eeprom.pl generates build_config.h, eeprom_types.h, eeprom_layout.h
# and eeprom.bin in one run.  Every generated file is a target so that the
# build graph always routes through this single recipe before anything that
# includes build_config.h (directly or via librustyaxe/rrprotocol headers)
# is compiled, even under a parallel make -j build.
${BUILD_DIR}/eeprom_types.h ${BUILD_DIR}/eeprom_layout.h ${BUILD_DIR}/build_config.h ${EEPROM_FILE}: tools/pack-eeprom.pl ${CF} ${CHANNELS}
	@echo "[pack-eeprom]" 
	set -e; ./tools/pack-eeprom.pl ${PROFILE}
