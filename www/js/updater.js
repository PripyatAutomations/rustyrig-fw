if (!"serial" in navigator) {
   alert("Web Serial is not supported. We can't do web updates. Sorry!");
}

// Download list of available model & profiles
// Present a selection of available models
// Download the list of available system images for model (global)
// Download the list of available EEPROM images for model&profile (per user
// Present a list of available system images & eeproms (if any)
// Confirm the checksum matches on the image and the EEPROM
// Send CLEAR EEPROM command to radio
// Send ENTER DFU command
// Flash SYSTEM image
// Wait for radio hearbeat
// Send FLASH EEPROM command
// Flash EEPROM image
// Send REBOOT command to radio
