// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef RR_GTK_WINDOW_H
#define RR_GTK_WINDOW_H
#include <gtk/gtk.h>
#include <stdbool.h>
void frontend_gtk_config_refresh(const char *key);
bool frontend_gtk_config_refresh_cb(const char *key);
bool gui_init(void);
#endif
