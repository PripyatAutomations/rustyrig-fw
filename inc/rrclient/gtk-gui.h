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
#include "rrclient/gtk.freqinput.h"
#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	HTTP_UA_LEN		512		// allow 128 bytes
#define	USER_PRIV_LEN		100		// privileges list
#define USER_EMAIL_LEN		128		// email address

struct GuiWindow {
    char name[128];		// Window name
    GtkWidget *gtk_win;		// GTK window widget
    struct GuiWindow *next;
};
typedef struct GuiWindow gui_window_t;

extern gui_window_t *gui_store_window(GtkWidget *gtk_win, const char *name);
extern gui_window_t *gui_find_window(GtkWidget *gtk_win, const char *name);
extern bool gui_forget_window(gui_window_t *window, const char *name);
extern gui_window_t *gui_windows;
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
extern bool log_print(const char *fmt, ...);
extern void gui_edit_config(const char *filepath);
extern gboolean scroll_to_end(gpointer data);
extern GtkWidget *init_config_tab(void);
extern GtkWidget *create_codec_selector_vbox(GtkComboBoxText **out_tx, GtkComboBoxText **out_rx);		// gtk.codecpicker.c
extern void populate_codec_combo(GtkComboBoxText *combo, const char *codec_list, const char *default_id);
extern GtkComboBoxText *tx_combo;
extern GtkComboBoxText *rx_combo;

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

// Call this *after* the GtkWindow is realized (has a GdkWindow)
extern void enable_windows_dark_mode_for_gtk_window(GtkWidget *window);
extern void disable_console_quick_edit(void);
extern bool set_window_icon(GtkWidget *window, const char *icon_name);

#endif


#endif	// !defined(__rrclient_gtk_gui_h)
