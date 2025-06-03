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
extern void update_connection_button(bool connected, GtkWidget *btn);
extern void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text);
extern bool gui_init(void);
extern GtkTextBuffer *text_buffer;
extern GtkWidget *conn_button;
extern GtkWidget *text_view;
extern GtkWidget *freq_entry;
extern GtkWidget *mode_combo;

#endif	// !defined(__rrclient_gtk_gui_h)
