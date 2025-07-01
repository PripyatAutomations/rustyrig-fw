This is the source for rustyrig.

It consists of a few parts:
	src/fwdsp		gstreamer based audio bridge
	src/rrclient		GTK3 + gstreamer based client
	src/rrserver		backend server
	www/			WebUI (served by rrserver)


You can configure things in config/ or put your .cfg files in ~/.config/
and the program will find them.


To build:
	Edit things in config/ as appropriate.
	Run ./build.sh

To run:
	./test-run.sh or 'make run'

To run client:
	./build/client/rrclient


Installing:
	make install



Pipelines
---------
You will want to configure your pipelines in rrserver.cfg and rrclient.cfg

These configurations will use a 4 character ID such as mu08 or pc44 for mulaw 8khz or pcm 44.1khz

RX and TX do not refer to radio role, but rather the direction of the stream itself.



---------

Good luck!
