//
// rrclient/gtk/gtk.vol-box.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_gtk_vol_box_h)
#define __rrclient_gtk_vol_box_h
#include <librustyaxe/config.h>

extern GtkWidget *create_volbox(void);
extern GtkWidget *rx_vol_slider;
extern GtkWidget *rx_rig_vol_slider;

#endif // !defined(__rrclient_gtk_vol_box_h)
