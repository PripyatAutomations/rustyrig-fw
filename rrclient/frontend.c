//
// rrclient/frontend.c: frontend module host interface
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdlib.h>
#include <librustyaxe/core.h>
#include <rrclient/frontend.h>
#include <rrclient/ui.h>

static const rr_frontend_ops_t *registered_ops = NULL;

bool frontend_ops_register(const rr_frontend_ops_t *ops) {
   if (!ops || !ops->name || !ops->init || !ops->run || !ops->quit) {
      Log(LOG_CRIT, "frontend", "frontend_ops_register: incomplete ops table");
      return true;
   }
   if (registered_ops) {
      Log(LOG_CRIT, "frontend", "frontend_ops_register: %s already registered",
         registered_ops->name);
      return true;
   }
   registered_ops = ops;
   Log(LOG_INFO, "frontend", "Frontend module registered: %s", ops->name);
   return false;
}

void frontend_ops_unregister(void) {
   if (!registered_ops) {
      return;
   }
   Log(LOG_INFO, "frontend", "Frontend module unregistered: %s",
      registered_ops->name);
   registered_ops = NULL;
   if (ui_mode == UI_MODE_GTK) {
      ui_mode = UI_MODE_TUI;
   }
}

const rr_frontend_ops_t *frontend_ops(void) {
   return registered_ops;
}

bool frontend_present(void) {
   return registered_ops != NULL;
}
