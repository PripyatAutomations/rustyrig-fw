//
// rrclient/gtk.admin.c: Admin tab in user interface
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "librustyaxe/config.h"
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
#include "../ext/libmongoose/mongoose.h"
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/util.file.h"
#include "librustyaxe/ws.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/ui.speech.h"
#include "librustyaxe/client-flags.h"

extern dict *cfg;		// config.c

// main.c
extern GtkWidget *main_notebook;
GtkWidget *admin_view = NULL;
GtkWidget *admin_tab = NULL;

///////////////////////////////////////////
GtkWidget *init_admin_tab(void) {
   GtkWidget *nw = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nw),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

   // add stuff to the window
   gtk_container_add(GTK_CONTAINER(nw), admin_view);
   GtkWidget *admin_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(admin_tab_label), "(<u>2</u>) Admin");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, admin_tab_label);
   ui_speech_set(nw,
              "Admin Tab",                // name
              "Server administration",    // description
              UI_ROLE_BUTTON,             // role
              true);                      // focusable
   return nw;
}
