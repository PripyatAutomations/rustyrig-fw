pack-eeprom: tools/pack-eeprom.pl ${CF} ${CHANNELS}
	@echo "[pack-eeprom]" 
	set -e; ./tools/pack-eeprom.pl ${PROFILE}

build/${PROFILE}/eeprom_types.h: pack-eeprom
