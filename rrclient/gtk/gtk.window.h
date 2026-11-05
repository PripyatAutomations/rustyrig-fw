#ifndef RR_GTK_WINDOW_H
#define RR_GTK_WINDOW_H
#include <gtk/gtk.h>
#include <stdbool.h>
void frontend_gtk_config_refresh(const char *key);
bool frontend_gtk_config_refresh_cb(const char *key);
bool gui_init(void);
#endif
