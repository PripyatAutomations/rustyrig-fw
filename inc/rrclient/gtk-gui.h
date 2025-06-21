#if	!defined(__rrclient_gtk_gui_h)
#define	__rrclient_gtk_gui_h
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
//#include "../ext/libmongoose/mongoose.h"
//#include "rustyrig/http.h"
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	HTTP_UA_LEN		512		// allow 128 bytes
#define	USER_PRIV_LEN		100		// privileges list
#define USER_EMAIL_LEN		128		// email address

#define GTK_TYPE_FREQ_INPUT (gtk_freq_input_get_type())
G_DECLARE_FINAL_TYPE(GtkFreqInput, gtk_freq_input, GTK, FREQ_INPUT, GtkBox)

GtkWidget *gtk_freq_input_new(void);
void gtk_freq_input_set_value(GtkFreqInput *fi, unsigned long freq);
unsigned long gtk_freq_input_get_value(GtkFreqInput *fi);

inline gpointer cast_func_to_gpointer(void (*f)(GtkToggleButton *, gpointer)) {
   union {
      void (*func)(GtkToggleButton *, gpointer);
      gpointer ptr;
   } u = { .func = f };
   return u.ptr;
}

extern bool ui_print(const char *fmt, ...);
extern void update_connection_button(bool connected, GtkWidget *btn);
extern void update_ptt_button_ui(GtkToggleButton *button, gboolean active);
extern void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text);
extern gboolean handle_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
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
extern GtkWidget *config_tab;
extern gulong mode_changed_handler_id;
extern gulong freq_changed_handler_id;
extern bool place_window(GtkWidget *window);
extern void show_server_chooser(void);			// gtk.serverpick.c
extern void gtk_freq_input_set_value(GtkFreqInput *fi, unsigned long freq);
extern unsigned long gtk_freq_input_get_value(GtkFreqInput *fi);
extern bool log_print(const char *fmt, ...);
extern void gui_edit_config(const char *filepath);
extern gboolean scroll_to_end_idle(gpointer data);

#endif	// !defined(__rrclient_gtk_gui_h)
