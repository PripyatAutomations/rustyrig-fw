//
// rrclient/gtk/gtk.editcfg.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_gtk_editcfg_h)
#define __rrclient_gtk_editcfg_h
#include <librustyaxe/config.h>
#include <glib.h>
void gtk_editcfg_shutdown(void);
bool gui_edit_remote_config(const char *, const char *, bool (*)(const char *,void *), void *, GDestroyNotify);

#endif // !defined(__rrclient_gtk_editcfg_h)
