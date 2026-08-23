//
// inc/rrclient/ui.colors.h: Support for color related things in the UI
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_ui_colors_h)
#define	__rrclient_ui_colors_h
#include <librustyaxe/config.h>
#include <librrprotocol/rrprotocol.h>

#if     defined(USE_GTK)
#include <gtk/gtk.h>
#include <rrclient/gtk.core.h>
#endif

extern const char *pango_color_for_tag(const char *tag, bool *is_bg);

#endif	// __rrclient_ui_colors_h
