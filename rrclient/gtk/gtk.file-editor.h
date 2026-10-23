//
// rrclient/gtk/gtk.file-editor.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
