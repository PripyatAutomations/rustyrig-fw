What is this?
It's a project to make a firmware that will run on various homebrew radios.

It's nowhere near ready to use but feel free to jump in and contribute
changes to help it get closer!

Initially we're targetting devices running linux, with hopes to port to esp32 and
maybe stm32. Of course, the features available will be reduced on lower-end hardware.

See QUICKSTART.txt for a quick guide to getting running.

Be sure to pull submodules:
	git submodule init
	git submodule pull

There's probably missing packages in these lists and some packages no longer needed....

	* Debian *
	make installdep

	*others*
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
	This will use ./config/myradio.config.json and place build artifacts in build/myradio/

	Edit configuration in config/radio.config.json as appropriate.
		joe radio/radio.config.json

	Build the firmware and EEPROM:
		make world

	Optionally run it, if on posix (recommended):
		make run

Installing
----------

	Linux
	-----
	There is no installing, just run from the source folder. You can
	copy the firmware wherever, just make sure you edit paths to
	eeprom.bin as needed.

	ESP32/STM32
	-----------
	Holding DFU button, plug the radio into USB.
	Try 'make install' to use DFU flashing.

	If this doesnt work, see doc/BOOTLOADER.txt
	Possibly use the web flashers in www/ when they are tested!


Running
-------
	make run

If compiling gives an error such as:
	make: *** No rule to make target 'build/radio/obj/amp.o', needed by 'build/radio/firmware.bin'.  Stop.
Just try re-running 'make'!

Status
------

Right now I'm working on the radio management and CAT code, then i'll start
adding support for various i2c devices like DDS chips.

It's up to the user to implement bits of the reference hardware and
configure the software as they need. Feel free to request features via
github.com/pripyatautomations/


Known issues
------------
* eeprom corruption *
   If you encounter problems with an eeprom checksum error after restarting
the program, try setting eeprom.readonly to true and using 'make gdb' instead
of 'make run' - if it crashes, please type 'bt' then paste the output into a
bug report. - This can happen when a bug causes the eeprom to be accidentally
modified. Since on linux we mmap the eeprom image, this isn't going to cause
wear. On real EEPROM, we will be using a delayed write system. 
