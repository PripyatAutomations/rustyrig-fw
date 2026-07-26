build/${PROFILE}/eeprom_types.h build/${PROFILE}/eeprom.bin: tools/pack-eeprom.pl ${CF} ${CHANNELS}
	@echo "[pack-eeprom]" 
	set -e; ./tools/pack-eeprom.pl ${PROFILE}
