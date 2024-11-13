There's probably missing packages in these lists and some packages no longer needed....

Install these packages at minimum:
	libjson-perl libterm-readline-perl-perl libhash-merge-perl libjson-xs-perl libjson-perl libstring-crc32-perl

Install cpan modules (if on debian)
	cpan install Mojo::JSON::Pointer

Files
-----
	res/eeprom_layout.json		Contains machine readable layout data for patching eeprom.bin
	config/radio.json		User supplied, see doc/radio.json.example

Configuring
-----------
	See CONFIGURING for hints

Building
--------
	You can use CONFIG=name to pass a configuration, do NOT add the .json suffix, ex:
		make CONFIG=myradio world
	This will use ./myradio.json and place build artifacts in build/myradio/

	Edit configuration in radio.json as appropriate.
		joe radio.json

	Build the firmware and EEPROM:
		make world

	Optionally (recommended):
		make test


Installing
----------
	Holding DFU button, plug the radio into USB.
	Try 'make install' to use DFU flashing.

	If this doesnt work, see doc/BOOTLOADER.txt


Status
------

Right now I'm working on the radio management and CAT code, then i'll start
adding support for various i2c devices like DDS chips.

It's up to the user to implement bits of the reference hardware and
configure the software as they need. Feel free to request features via
github.com/pripyatautomations/
