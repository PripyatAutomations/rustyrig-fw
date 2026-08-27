//
// rrclient/gtk.vol-box.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__rrclient_gtk_vol_box_h)
#define	__rrclient_gtk_vol_box_h
#include <librustyaxe/config.h>

extern GtkWidget *create_volbox(void);
extern GtkWidget *rx_vol_slider;
extern GtkWidget *rx_rig_vol_slider;

#endif // !defined(__rrclient_gtk_vol_box_h)
