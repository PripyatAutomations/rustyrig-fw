// RustyRig integrated GStreamer pipeline editor.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRPIPELINES_EDITOR_H
#define RRPIPELINES_EDITOR_H
#include <gtk/gtk.h>

GtkWidget *rr_pipeline_editor_new(const gchar *json,
                                  gboolean (*apply)(const gchar *, gpointer),
                                  gpointer apply_data,
                                  void (*focus)(const gchar *, gpointer),
                                  gpointer focus_data, GError **error);
void rr_pipeline_editor_select(GtkWidget *widget, const gchar *id);
#endif
