Client frontend modules
=======================

The rrclient core is headless: it never links a GUI toolkit. Graphical
frontends load at startup as shared modules and register a small ops table
(rrclient/frontend.h). Today the only frontend is GTK, shipped as
rrclient-gtk.so.

Configuration
-------------

Modules are listed in a [modules] section of the client config:

    ; Where to find loadable modules
    path.modules=/usr/lib/rustyrig/modules/rrclient

    [modules]
    rrclient-gtk.so=

The module name may include .so or not; the value is an options string
passed to the module (empty is fine). Multiple modules may be listed, one
per line. Run with -T (or remove the [modules] section) for the headless
TUI client; -T never loads a module.

Distro packages split this: rustyrig-client is the headless core;
rustyrig-client-gtk installs the module into /usr/lib/rustyrig/modules/rrclient and
is the only package depending on GTK.

Module lifecycle
----------------

    dlopen (rr_load_module)
      |
      v
    rr_module_init()              -- register ops, register event tokens
      |
      v
    host calls ops->init()        -- gtk_init etc. (needs real argc/argv)
      |
      v
    host calls ops->run()         -- module owns the main loop (gtk_main)
      |
      v
    main loop returns
      |
      v
    rr_unload_module
      |
      v
    rr_module_shutdown()          -- remove GLib sources, destroy windows,
      |                              unregister every event token,
      |                              frontend_ops_unregister()
      v
    dlclose

A failed rr_module_init() or ops->init() unloads the module; the core logs
and falls back to the TUI. A module must never leave a registration behind:
every event subscription goes through event_on_token() and the module glue
records each token (rr_module_token_add) so shutdown can release them all.

Event safety
------------

The event bus hands out opaque tokens (event_on_token/event_off_token).
Unregistering is idempotent and safe from inside a callback; a stale token
(a listener already removed by a wildcard event_off) cannot remove a
different listener, because removal matches the specific registration, not
just (event, callback).

GTK/GLib callbacks
------------------

The module removes, in rr_module_shutdown -> gtk_frontend_stop:
  - the 1hz update_now timeout
  - the per-room userlist refresh timeouts (gtk.userlist.c)
  - pending window configure debounce sources (gtk.winmgr.c)
  - all windows (destroying them removes their signal handlers)

Event-bus tokens are released before the module is unmapped, so no event
dispatch can reach unloaded code.

Hot unload/reload
-----------------

Not supported. Unloading a module whose gtk_main() is running would unload
code executing on the stack. The core only unloads the frontend during
cleanup, after the main loop has returned, so nothing in the module is
active at that point. Changing frontends requires restarting the client.

Writing a module
----------------

Link against nothing from rrclient: the core executable is built with
-rdynamic and the module resolves client API (event bus, cfg, frontend ops
host) at dlopen time. Export:

    bool rr_module_init(void);      // return false on success
    void rr_module_shutdown(void);  // full teardown, see above

Register your rr_frontend_ops_t via frontend_ops_register() in init (only
one frontend may hold the slot), and follow the teardown order in
rr_module_shutdown().
