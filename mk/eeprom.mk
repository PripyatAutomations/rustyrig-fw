
pack-eeprom: tools/pack-eeprom.pl ${EEPROM_FILE}
	@echo "[pack-eeprom]" 
	set -e; ./tools/pack-eeprom.pl ${PROFILE}
