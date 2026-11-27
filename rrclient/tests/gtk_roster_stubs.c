// SPDX-License-Identifier: GPL-2.0-only
/* UI chrome is not part of isolated GTK roster layout tests. */
#include <rrclient/gtk/gtk.floating-panel.h>
__attribute__((weak)) void rr_floating_panel_set_title(RRFloatPanel *panel, const char *title) {
   (void)panel; (void)title;
}
__attribute__((weak)) void rr_floating_panel_hide(RRFloatPanel *panel) {
   (void)panel;
}

__attribute__((weak)) const char *gtk_chat_current_server(void) { return NULL; }
__attribute__((weak)) const char *gtk_chat_current_room(void) { return NULL; }
