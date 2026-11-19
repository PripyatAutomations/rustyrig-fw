// RustyRig integrated GStreamer pipeline editor.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_GTK_PIPELINE_EDITOR_H
#define RRCLIENT_GTK_PIPELINE_EDITOR_H
#include <gtk/gtk.h>

GtkWidget *rr_pipeline_editor_new(const gchar *json, gboolean (*apply) (const gchar *, gpointer), gpointer apply_data, void (*focus) (const gchar *, gpointer),
   gpointer focus_data, GError **error);
void rr_pipeline_editor_select(GtkWidget *widget, const gchar *id);

/* LV2 GStreamer effects are configured here; execution stays in fwdsp. */
void rr_pipeline_editor_add_effect(GtkWidget *widget);

#endif
