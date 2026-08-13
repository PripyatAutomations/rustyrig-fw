librrprotocol: Rusty Rig protocol support library.

This library is quite a mess still. I split it out of rrserver and rrclient
in late July 2026.

Eventually you should be able to use it and librustyaxe to build a
functional client or server for the RustyRig protocol.


Please note that the protocol is subject to change at any time and
that you should always use the latest version of the library in your
projects.

To use this mess, link -lrustyaxe -lrrprotocol and all event_on() to
register your callbacks.

See rustyrig-fw/doc/EVENT_REGISTRY for a list of supported and their
expected json parameters.


We desperately need to make a mechanism to list which json parameters
an event allows and filter out anything not in that list. This will
improve security probably.

Don't expose this to the internet. Only use it over VPN, even with TLS
enabled. Do not trust the software to be secure. There are many bugs known
and unknown.... I'm only one person with limited time :(
