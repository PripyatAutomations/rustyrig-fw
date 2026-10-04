//
// rrclient/module.c: GTK frontend module entry points
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Loaded by core rrclient via rr_load_module("rrgtk") when $DISPLAY is set.
// The module talks to the core client only through the event bus and the
// frontend ops host interface (rrclient/frontend.h); it never dereferences
// core client state.
//
// Lifecycle:
//   rr_module_init()      gtk_init, register ops, register event tokens,
//                         build windows; on any failure unwind and return
//                         true so the loader dlclose()s us again.
//   rr_module_shutdown()  unregister every event token, remove GLib sources,
//                         tear down widgets, unregister frontend ops. After
//                         this returns the loader may dlclose() the module.
//
// Unload safety: GTK keeps GLib sources (timeouts/idles/GSources) and widget
// signal handlers pointing into module code. rr_module_shutdown() removes
// every source the module created and destroys all windows before returning.
// We deliberately never hot-unload a running module: gtk_main() would be
// executing module code as the unload request arrives. The core only unloads
// the frontend during rrclient_cleanup(), after the main loop has returned,
// so no module code is on the stack at that point. Hot reload is NOT
// supported; see doc/client-modules.md.
//
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <rrclient/frontend.h>
#include "gtk.core.h"

static rr_event_token_t *module_tokens = NULL;   // owned event registrations
static size_t module_token_count = 0;
static size_t module_token_capacity = 0;
static bool module_running = false;

/* Defined in gtk.core.c: builds the UI and runs gtk_main() from run(). */
extern const rr_frontend_ops_t gtk_frontend_ops;
extern void gtk_frontend_stop(void);

bool rr_module_token_add(rr_event_token_t token) {
   if (!token) {
      return true;
   }
   if (module_token_count == module_token_capacity) {
      size_t cap = module_token_capacity ? module_token_capacity * 2 : 16;
      rr_event_token_t *grown = realloc(module_tokens, cap * sizeof(*grown));
      if (!grown) {
         event_off_token(token);
         return true;
      }
      module_tokens = grown;
      module_token_capacity = cap;
   }
   module_tokens[module_token_count++] = token;
   return false;
}

static void rr_module_tokens_release(void) {
   for (size_t i = 0; i < module_token_count; i++) {
      event_off_token(module_tokens[i]);
   }
   free(module_tokens);
   module_tokens = NULL;
   module_token_count = 0;
   module_token_capacity = 0;
}

bool rr_module_init(void) {
   if (module_running) {
      Log(LOG_CRIT, "module", "rrgtk: init called twice");
      return true;
   }
   if (frontend_ops_register(&gtk_frontend_ops)) {
      return true;
   }
   // gtk_init must see the real argc/argv: the host calls ops->init() right
   // after loading, so registration alone is safe here even if GTK is not
   // initialized yet.
   module_running = true;
   Log(LOG_INFO, "module", "rrgtk: frontend registered");
   return false;
}

void rr_module_shutdown(void) {
   if (module_running) {
      module_running = false;
      gtk_frontend_stop();
   }
   rr_module_tokens_release();
   frontend_ops_unregister();
}
