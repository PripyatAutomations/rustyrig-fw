//
// gtk-client/gtk.admin.c: Admin tab in user interface
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
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "common/client-flags.h"

extern dict *cfg;		// config.c
extern struct mg_mgr mgr;
extern bool ws_connected;
extern bool ws_tx_connected;
extern struct mg_connection *ws_conn, *ws_tx_conn;
extern bool cfg_show_pings;
extern time_t now;
extern time_t poll_block_expire, poll_block_delay;
extern GtkWidget *main_window;
extern GtkWidget *main_notebook;
GtkWidget *admin_view = NULL;
GtkWidget *admin_tab = NULL;

GtkWidget *init_admin_tab(void) {
   GtkWidget *nw = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(nw),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

   // add stuff to the window
   gtk_container_add(GTK_CONTAINER(nw), admin_view);
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, gtk_label_new("Admin"));
   return nw;
}
