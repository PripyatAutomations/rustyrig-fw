//
// inc/rrclient/ui.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_ui_h)
#define	__rrclient_ui_h
#include <librustyaxe/config.h>

#if     defined(USE_GTK)
#include <gtk/gtk.h>
#include <mod.ui.gtk3/gtk.core.h>
#include <mod.ui.gtk3/gtk.alertdialog.h>
#endif	// defined(USE_GTK)

enum GuiMode {
   GUI_MODE_TUI = 0,
   GUI_MODE_GTK
};

#ifdef _WIN32
extern void win32_check_darkmode(void);
#endif	// WIN32
extern enum GuiMode ui_mode;	// in ui.c

#endif // !defined(__rrclient_ui_h)
