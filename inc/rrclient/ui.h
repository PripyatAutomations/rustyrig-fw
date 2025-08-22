//
// inc/rrclient/ui.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rrclient_ui_h)
#define	__rrclient_ui_h
#include "common/config.h"

#if	defined(USE_GTK)
#include <gtk/gtk.h>
#endif	// defined(USE_GTK)

#ifdef _WIN32
extern void win32_check_darkmode(void);
#endif

#endif	// !defined(__rrclient_ui_h)
