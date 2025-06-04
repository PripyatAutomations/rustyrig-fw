#if	!defined(__rrclient_gtk_gui_h)
#define	__rrclient_gtk_gui_h
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"

extern bool ui_print(const char *fmt, ...);
extern bool log_print(const char *fmt, ...);
extern void update_connection_button(bool connected, GtkWidget *btn);
extern void update_ptt_button_ui(GtkToggleButton *button, gboolean active);
extern void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text);
extern bool gui_init(void);
extern void on_ptt_toggled(GtkToggleButton *button, gpointer user_data);
extern GtkTextBuffer *text_buffer;
extern GtkWidget *conn_button;
extern GtkWidget *text_view;
extern GtkWidget *freq_entry;
extern GtkWidget *mode_combo;
extern GtkWidget *userlist_window;
extern GtkWidget *log_view;
extern GtkTextBuffer *log_buffer;
extern GtkWidget *ptt_button;

#endif	// !defined(__rrclient_gtk_gui_h)
