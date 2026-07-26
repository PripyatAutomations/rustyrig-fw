#if	!defined(__rrclient_gtk_winmgr_h)
#define	__rrclient_gtk_winmgr_h
#include <librustyaxe/config.h>

extern gui_window_t *ui_new_window(GtkWidget *window, const char *name);
extern bool set_window_icon(GtkWidget *window, const char *icon_name);
extern gboolean on_window_state(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data);
extern gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name);
extern gui_window_t *gui_find_window(GtkWidget *gtk_win, const char *name);
extern bool gui_forget_window(gui_window_t *window, const char *name);
extern gui_window_t *gui_windows;

// Widget tracking (so we can find them without a mess of globals)
extern gui_widget_t *gui_find_widget(GtkWidget *widget, const char *name);
extern bool gui_forget_widget(gui_widget_t *gw, const char *name);
extern gui_widget_t *gui_store_widget(GtkWidget *widget, const char *name);

#endif	// !defined(__rrclient_gtk_winmgr_h)
