Install these packages at minimum:
	libjson-perl libterm-readline-perl-perl libhash-merge-perl libjson-xs-perl libjson-perl

Install cpan modules (if on debian)
	cpan install Mojo::JSON::Pointer

Files
-----
	res/eeprom_layout.json	Contains machine readable layout data for patching eeprom.bin
	radio.json		User supplied, see doc/radio.json.example

Building
--------
	Edit configuration in radio.json as appropriate.
		joe radio.json

	Build the firmware and EEPROM:
		make world

	Optionally (recommended):
		make test


Installing
----------
	Plug the radio into USB
	Try 'make install' to use DFU flashing.

	If this doesnt work, see doc/BOOTLOADER.txt
