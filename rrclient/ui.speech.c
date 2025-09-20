// 
// rrclient/ui.speech.c: Support for screen readers/speech
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/core.h>
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
#include <rrclient/auth.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.speech.h>
#include <librrprotocol/rrprotocol.h>

extern dict *cfg;		// config.c

static void apply_gtk(GtkWidget *widget, const ui_speech_hint_t *hint) {
   if (!widget || !hint) {
      return;
   }
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

   // XXX: Why dont we just store teh ATK_ROLE??
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
   if (!widget || !name || !description) {
      return;
   }

   ui_speech_hint_t hint = {
      .name = name,
      .description = description,
      .role = role,
      .focusable = focusable
   };
   ui_speech_apply(widget, &hint);
}
