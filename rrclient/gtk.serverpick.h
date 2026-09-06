//
// rrclient/gtk.serverpick.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_gtk_serverpick_h)
#define	__rrclient_gtk_serverpick_h
#include <librustyaxe/config.h>

#if     defined(USE_GTK)
#include <gtk/gtk.h>

extern void on_connect_clicked(GtkButton *btn, gpointer user_data);
extern gboolean on_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data);
extern gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data);

#endif // defined(USE_GTK)

#endif // !defined(__rrclient_gtk_serverpick_h)
