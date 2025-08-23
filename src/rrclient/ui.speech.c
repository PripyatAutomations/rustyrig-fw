// 
// gtk-client/ui.speech.c: Support for screen readers
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <atk/atk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"
#include "rrclient/ui.speech.h"

extern dict *cfg;		// config.c

static void apply_gtk(GtkWidget *widget, const ui_speech_hint_t *hint) {
   AtkObject *a11y = gtk_widget_get_accessible(widget);

   if (!a11y) {
      return;
   }

   if (hint->name) {
      atk_object_set_name(a11y, hint->name);
   }

   if (hint->description) {
      atk_object_set_description(a11y, hint->description);
   }

   switch (hint->role) {
      case UI_ROLE_BUTTON:   atk_object_set_role(a11y, ATK_ROLE_PUSH_BUTTON); break;
      case UI_ROLE_ENTRY:    atk_object_set_role(a11y, ATK_ROLE_ENTRY); break;
      case UI_ROLE_LABEL:    atk_object_set_role(a11y, ATK_ROLE_LABEL); break;
      case UI_ROLE_CHECKBOX: atk_object_set_role(a11y, ATK_ROLE_CHECK_BOX); break;
      case UI_ROLE_SLIDER:   atk_object_set_role(a11y, ATK_ROLE_SLIDER); break;
      case UI_ROLE_COMBOBOX: atk_object_set_role(a11y, ATK_ROLE_COMBO_BOX); break;
      default:               atk_object_set_role(a11y, ATK_ROLE_UNKNOWN); break;
   }
}

void ui_speech_apply(GtkWidget *widget, const ui_speech_hint_t *hint) {
   if (!widget || !hint) {
      return;
   }

   apply_gtk(widget, hint);
}

void ui_speech_set(GtkWidget *widget,
                   const char *name,
                   const char *description,
                   ui_role_t role,
                   bool focusable) {
   ui_speech_hint_t hint = {
      .name = name,
      .description = description,
      .role = role,
      .focusable = focusable
   };
   ui_speech_apply(widget, &hint);
}
