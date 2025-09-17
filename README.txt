This is the source for rustyrig.

For now it's easiest just to build/run it from this folder, but installing it should probably work if you adjust paths

Take a look at the deps in install-deps.sh or use it (if on debian-like distros) or manually install them. Ignore the commented lines, they're for bare metal builds someday
It consists of a few parts:
	fwdsp/			gstreamer based audio bridge
	rrclient/		GTK3 + gstreamer based client
	rrserver/		backend server
	www/			WebUI (served by rrserver)


You can configure things in config/ or put your .cfg files in ~/.config/
and the program will find them.


To build:
	Edit things in config/ as appropriate.
	Run ./build.sh

Configure:
	Be sure config/http.users has appropriate contents
		Try user-edit.sh

	Make sure config/radio.config.json is good for your build host

	Edit config/rrserver.cfg for the server

To run:
	./test-run.sh or 'make run'

To run client:
	Either start the client and exit it, to create a default config or --
		cp config/rrclient.cfg.example ~/.config/rrclient.cfg
	Then run it
		./test-client.sh

Installing:
	make install


You can put server config in ~/.config/rrserver.cfg or /etc/rustyrig/rrserver.cfg

Pipelines
---------
You will want to configure your pipelines in rrserver.cfg and rrclient.cfg

These configurations will use a 4 character ID such as mu08 or pc44 for mulaw 8khz or pcm 44.1khz

RX and TX do not refer to radio role, but rather the direction of the stream itself.




test-all.sh
-----------
If you have tmux or screen available, this script will launch a new session
running the server and client in their own windows.

You can watch the debug messages this way.

Note you can filter the log messages by editing log.level in the appropriate config file ;)

---------

Good luck!
