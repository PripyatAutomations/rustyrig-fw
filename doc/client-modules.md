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

Logger callbacks
----------------

Unloadable modules register with `log_add_callback_token()` and retain the
returned registration. Remove it with `log_remove_callback()` before destroying
widgets or returning from module shutdown. GTK does this before its code is
unmapped; even the loader's post-`dlclose()` log message must not call a module
callback. The existing boolean `log_add_callback()` API remains compatible for
host callbacks whose code lives for the entire process.

The GTK clock exits the GTK loop when shutdown is requested. Core cleanup runs
after the loop returns, so no module callback unloads its own executing code.

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

    bool rr_module_init(void);      // return true on success
    void rr_module_shutdown(void);  // full teardown, see above

Register your rr_frontend_ops_t via frontend_ops_register() in init (only
one frontend may hold the slot), and follow the teardown order in
rr_module_shutdown().


## Native identity runtime and future master-password vault

Native private-key state belongs to the core `rrclient/identity.store`
component shared by GTK and TUI. Unlocking one identity must not lock another.
Unlocked keys remain in process memory until explicitly locked or process exit;
the client clears the cache on cleanup. GTK may manage key files, but must not
own an independent key cache.

**Vault requirement (not yet implemented):** the master-password vault is an
authenticated, encrypted collection of the passphrases needed to unlock
individual encrypted Ed25519 keys. It must never contain private-key plaintext
or store the master password/verifier as a reusable secret. Use a modern
password-based KDF with per-vault random salt and authenticated encryption
with a fresh nonce; persist KDF parameters and a version with the ciphertext.
No password-complexity restrictions are imposed. Write updates atomically with
restrictive file/directory permissions; never truncate a live vault on failure.
Cache vault secrets only in memory while unlocked and clear them at lock/exit.
A key locked explicitly must not be silently re-unlocked by a later operation.

Both GTK and TUI must run the same vault-unlock gate **before creating any
network connections**. If a configured vault exists but is inaccessible, has
invalid authentication, uses an unknown format or the operator cancels, fail
closed: show a clear error and exit. Never silently proceed with saved
passwords, legacy password login or an empty vault. There must also be a
recovery path for intentionally removing/replacing a vault without deleting
the underlying encrypted keys. Backup/export must remain separately
password-protected.

Client auth currently still uses legacy passwords; no claim is made that
the future vault or key-authentication handshake is functional.
