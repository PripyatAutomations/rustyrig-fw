#if	!defined(__rrclient_gtk_core_h)
#define	__rrclient_gtk_core_h
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/posix.h>

#define	HTTP_USER_LEN		16		// username length (16 char)
#define	HTTP_PASS_LEN		40		// sha1: 40, sha256: 64
#define	HTTP_HASH_LEN		40		// sha1
#define	HTTP_TOKEN_LEN		14		// session-id / nonce length, longer moar secure
#define	HTTP_UA_LEN		512		// allow 128 bytes
#define	USER_PRIV_LEN		100		// privileges list
#define USER_EMAIL_LEN		128		// email address

// GUI (GTK) window
struct GuiWindow {
   char name[128];		// Window name
#if	defined(USE_GTK)
   GdkEventConfigure *move_evt;
   GtkWidget *gtk_win;		// GTK window widget
#endif
   int	x, y, w, h;		// Location and size
   int last_x, last_y, last_w, last_h;	// Previous locations (if moving)
   bool is_moving;		// Is the window being moved?
   bool win_raised;		// Raised?
   bool win_minimized;		// Is the window minimized?
   bool win_modal;		// Always on top
   bool win_hidden;		// Hidden from view
   bool win_nohide;		// Don't hide this window when main window is minimized
   bool win_stashed;		// Was the window hidden because main was minimized? This ensures it's restored
   struct GuiWindow *next;
};
typedef struct GuiWindow gui_window_t;

struct GuiWidget {
   char name[128];
   GtkWidget *gtk_widget;	// GtkWidget for the widget
   struct GuiWidget *next;	// next in list
};
typedef struct GuiWidget gui_widget_t;

extern bool ui_print(const char *fmt, ...);
extern void update_connection_button(bool connected, GtkWidget *btn);
extern void update_ptt_button_ui(GtkToggleButton *button, gboolean active);
extern void set_combo_box_text_active_by_string(GtkComboBoxText *combo, const char *text);
extern gboolean focus_main_later(gpointer data);
extern bool gui_init(void);
extern GtkWidget *conn_button;
extern bool place_window(GtkWidget *window);
extern void show_server_chooser(void);			// gtk.serverpick.c
extern bool log_print(logpriority_t priority, const char *subsys, const char *fmt, ...);
extern bool log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap);
extern void gui_edit_config(const char *filepath);
extern gboolean ui_scroll_to_end(gpointer data);
extern GtkWidget *init_config_tab(void);
extern GtkWidget *mode_combo;          // if mode_combo is a GtkWidget*
extern GtkWidget *width_combo;

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

// Call this *after* the GtkWindow is realized (has a GdkWindow)
extern void enable_windows_dark_mode_for_gtk_window(GtkWidget *window);
extern void disable_console_quick_edit(void);
extern bool set_window_icon(GtkWidget *window, const char *icon_name);

#endif

extern GtkWidget *ptt_button_create(void);
extern GtkWidget *create_codec_selector_vbox(GtkWidget **out_tx, GtkWidget **out_rx);		// gtk.codecpicker.c
extern void populate_codec_combo(GtkComboBoxText *combo, const char *codec_list, const char *default_id);
extern GtkWidget *ptt_button;
extern GtkWidget *freq_entry;
extern GtkWidget *mode_combo;
extern gulong mode_changed_handler_id;
extern gulong freq_changed_handler_id;
extern bool ui_print_gtk(const char *msg);
extern bool cfg_use_gtk;
extern gboolean handle_global_hotkey(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
extern gboolean is_widget_or_descendant_focused(GtkWidget *ancestor);

#include "mod.ui.gtk3/gtk.codecpicker.h"
#include "mod.ui.gtk3/gtk.editcfg.h"
#include "mod.ui.gtk3/gtk.freqentry.h"
#include "mod.ui.gtk3/gtk.fm-mode.h"
#include "mod.ui.gtk3/gtk.mode-box.h"
#include "mod.ui.gtk3/gtk.ptt-btn.h"
#include "mod.ui.gtk3/gtk.serverpick.h"
#include "mod.ui.gtk3/gtk.txpower.h"
#include "mod.ui.gtk3/gtk.vfo-box.h"
#include "mod.ui.gtk3/gtk.vol-box.h"
#include "mod.ui.gtk3/gtk.winmgr.h"
#include "mod.ui.gtk3/gtk.chat.h"
#include "mod.ui.gtk3/gtk.hotkey.h"
#include "mod.ui.gtk3/gtk.alertdialog.h"

#endif	// !defined(__rrclient_gtk_core_h)
