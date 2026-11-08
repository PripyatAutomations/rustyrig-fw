//
// rrclient/gtk/gtk.vfo-box.h:
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#if     !defined(__rrclient_gtk_vfo_box_h)
#define __rrclient_gtk_vfo_box_h
#include <librustyaxe/config.h>
#include <rrclient/local.bridge.h>

extern GtkWidget *create_vfo_box(void);
extern void vfo_controls_fit_width(int width);
extern void vfo_controls_set_local_sdr(bool local);
extern void vfo_controls_set_sdr_driver(const rrclient_local_bridge_active_t *active);
extern void vfo_controls_set_ptt_state(bool any_tx, bool own_tx);

#endif // !defined(__rrclient_gtk_vfo_box_h)
