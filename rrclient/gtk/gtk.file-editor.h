//
// rrclient/gtk/gtk.file-editor.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_gtk_file_editor_h)
#define __rrclient_gtk_file_editor_h
#include <librustyaxe/config.h>
#include <glib.h>
void gtk_file_editor_shutdown(void);
void gui_file_editor_open(const char *filepath);
void gui_file_editor_open_config(const char *filepath);
bool gui_file_editor_open_remote(const char *, const char *, bool (*) (const char *, void *), void *, GDestroyNotify);

#endif // !defined(__rrclient_gtk_file_editor_h)
