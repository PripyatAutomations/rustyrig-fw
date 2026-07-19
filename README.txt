
This is the source for rustyrig remote station.


***Currently broken while I restructure things****
- Slowly making progress on this again (7/19)
  - All programs at least again run, need to repair libmongoose based builds
    and get back to working with it. We can make non-mongoose versions work
    again soon. Right now i want to try to support mongoose where possible.
I'm moving all protocol related stuff to a shared library, so the repo is broken for a bit while I work.

------------------------

For now it's easiest just to build/run it from this folder, but installing it should probably work.

Take a look at the deps in install-deps.sh or use it  or manually install them.
Currently it only supports debian-based systems. Contributions always welcome!

It consists of a few parts:
	fwdsp/			gstreamer based audio bridge
	rrgtk/		GTK3 + gstreamer based client
	rrserver/		backend server
	www/			WebUI (served by rrserver)


Config files go in config/ or ~/.config/

To build:
	Edit things in config/ as appropriate.
	Run ./build.sh

Configure:
	Be sure config/http.users has appropriate contents
		Try user-edit.sh - report bugs!

	Make sure config/radio.config.json is good for your build host (probably if linux or msys2)

	Copy config/rrgtk.cfg.example to ~/.config then edit the servers
		cp -i config/rrgtk.cfg.example ~/.config/rrgtk.cfg
		cp -i config/rrserver.cfg ~/.config/rrserver.conf

	Edit config/rrserver.cfg for the server (examples in config/release.config.json)
	This can go in ~/.config/rrserver.cfg or /etc/rustyrig/rrserver.cfg too.

To run server:
	./test-server

To run client:
	./test-client.sh

test-all.sh:
	If you have tmux or screen available, this script will launch a new session
	running the server and client in their own windows.

	You can watch the debug messages this way.

	Note you can filter the log messages by editing log.level in the appropriate config file ;)


Installing:	Probably BROKEN, but you can try it
	make install



Packaging
---------
Early work to package for arch and debian is present. Feel free to contribute to packaging/testing.

Pipelines
---------
You will want to configure your pipelines in rrserver.cfg and rrgtk.cfg

These configurations will use a 4 character ID such as mu08 or pc44 for mulaw 8khz or pcm 44.1khz

RX and TX do not refer to radio role, but rather the direction of the stream itself.


---------

Good luck!

- rustyaxe
