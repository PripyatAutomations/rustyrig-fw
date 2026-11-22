/* Reusable local file browser with caller-defined toolbar actions.
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_GTK_LOCAL_BROWSER_H
#define RRCLIENT_GTK_LOCAL_BROWSER_H
#include <gtk/gtk.h>

typedef struct rr_local_browser_action {
   const char *label;
   const char *icon;
   gboolean (*enabled)(const char *relative, gpointer user);
   void (*activate)(GtkWidget *browser, const char *relative, gpointer user);
} rr_local_browser_action_t;

/* Status callback returns a borrowed, static string (or NULL). The browser
 * owns the selected relative filename, never a path provided by the model. */
GtkWidget *rr_local_browser_new(const char *directory, const char *suffix,
                                const rr_local_browser_action_t *actions,
                                guint count,
                                const char *(*status)(const char *, gpointer),
                                gpointer user);
void rr_local_browser_refresh(GtkWidget *browser);
char *rr_local_browser_selected(GtkWidget *browser);
void rr_local_browser_message(GtkWidget *browser, const char *message);
/* Selection updates for callers with a live preview (e.g. IRC artwork). */
void rr_local_browser_on_selection(GtkWidget *browser,
   void (*callback)(GtkWidget *browser, const char *relative, gpointer user), gpointer user);
#endif
