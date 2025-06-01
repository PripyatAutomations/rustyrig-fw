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

#define	RR_HOST		"chiraq.istabpeople.com:4420"

struct mg_mgr mg_mgr;

int my_argc = -1;
char **my_argv = NULL;
bool dying = 0;                 // Are we shutting down?
bool restarting = 0;            // Are we restarting?
time_t now = -1;                // time() called once a second in main loop to update
bool ptt_active = false;

// config.c
extern bool config_load(const char *path);
extern dict *cfg;

void shutdown_app(int signum) {
   Log(LOG_INFO, "core", "Shutting down %s%d", (signum > 0 ? "with signal " : ""), signum);
}

static struct mg_mgr mgr;
static struct mg_connection *ws_conn = NULL;
static GtkTextBuffer *text_buffer;

static void on_ptt_toggled(GtkToggleButton *button, gpointer user_data) {
   ptt_active = gtk_toggle_button_get_active(button);

   const gchar *label = ptt_active ? "PTT ON " : "PTT OFF";
   gtk_button_set_label(GTK_BUTTON(button), label);

   GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(button));
   if (ptt_active) {
      gtk_style_context_add_class(context, "ptt-active");
      gtk_style_context_remove_class(context, "ptt-idle");
   } else {
      gtk_style_context_add_class(context, "ptt-idle");
      gtk_style_context_remove_class(context, "ptt-active");
   }
}

static void on_send_button_clicked(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(entry));
   if (ws_conn && msg && *msg)
      mg_ws_send(ws_conn, msg, strlen(msg), WEBSOCKET_OP_TEXT);
}

static void ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_WS_OPEN) {
      char login_buf[512];
      const char *login_user = dict_get(cfg, "server.user", NULL);

      if (login_user == NULL) {
         Log(LOG_CRIT, "core", "server.user not set in config!");
         exit(1);
      }

      memset(login_buf, 0, 512);
      snprintf(login_buf, 512,
                    "{ \"auth\": {"
                    "     \"cmd\": \"login\", "
                    "     \"user\": \"%s\""
                    "   }"
                    "}", login_user);
      Log(LOG_INFO, "core", "Sending login for user %s", login_user);
      mg_ws_send(c, login_buf, strlen(login_buf), WEBSOCKET_OP_TEXT);
      const char *lms = "Login message sent";
      gtk_text_buffer_insert_at_cursor(text_buffer, lms, strlen(lms));
      gtk_text_buffer_insert_at_cursor(text_buffer, "\n", 1);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
//      const char *login_pass = dict_get(cfg, "server.pass"));
      gtk_text_buffer_insert_at_cursor(text_buffer, wm->data.buf, wm->data.len);
      gtk_text_buffer_insert_at_cursor(text_buffer, "\n", 1);
   }
}

static gboolean poll_mongoose(gpointer user_data) {
   mg_mgr_poll(&mgr, 0);
   return G_SOURCE_CONTINUE;
}

int main(int argc, char *argv[]) {
   logfp = stdout;
   config_load("rrclient.cfg");
   gtk_init(&argc, &argv);
   GtkCssProvider *provider = gtk_css_provider_new();
   gtk_css_provider_load_from_data(provider,
      ".ptt-active { background: red; color: white; }"
      ".ptt-idle { background: #0fc00f; color: white; }",
      -1, NULL);

   GtkStyleContext *screen_ctx = gtk_style_context_new();
   gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_USER);

   mg_mgr_init(&mgr);

   // Connect to WebSocket server
   ws_conn = mg_ws_connect(&mgr, dict_get(cfg, "server.url", NULL), ws_handler, NULL, NULL);

   // GTK UI setup
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "rustyrig remote client");
   gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   GtkWidget *control_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(vbox), control_box, FALSE, FALSE, 0);

   // PTT button
   GtkWidget *ptt_button = gtk_toggle_button_new_with_label("PTT OFF");
   gtk_box_pack_start(GTK_BOX(control_box), ptt_button, FALSE, FALSE, 0);
   g_signal_connect(ptt_button, "toggled", G_CALLBACK(on_ptt_toggled), NULL);
   GtkStyleContext *ctx = gtk_widget_get_style_context(ptt_button);
   gtk_style_context_add_class(ctx, "ptt-idle");

   // Frequency input
   GtkWidget *freq_label = gtk_label_new("Freq (KHz):");
   GtkWidget *freq_entry = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(freq_entry), 13);
   gtk_entry_set_text(GTK_ENTRY(freq_entry), "7,074.000");

   gtk_box_pack_start(GTK_BOX(control_box), freq_label, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(control_box), freq_entry, FALSE, FALSE, 0);

   // Mode dropdown
   GtkWidget *mode_combo = gtk_combo_box_text_new();
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "CW");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "AM");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "LSB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "USB");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-L");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "D-U");
   gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "FM");
   gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 3);

   gtk_box_pack_start(GTK_BOX(control_box), mode_combo, FALSE, FALSE, 6);

   // Volume slider
   GtkWidget *volume_label = gtk_label_new("Volume:");
   GtkWidget *volume_slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
   const char *cfg_vol = dict_get(cfg, "ui.volume.rx", "30");
   gtk_range_set_value(GTK_RANGE(volume_slider), atoi(cfg_vol));

   gtk_box_pack_start(GTK_BOX(control_box), volume_label, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(control_box), volume_slider, TRUE, TRUE, 0);

   /////// Text box
   GtkWidget *text_view = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
   gtk_box_pack_start(GTK_BOX(vbox), text_view, TRUE, TRUE, 0);

   GtkWidget *entry = gtk_entry_new();
   gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

   GtkWidget *button = gtk_button_new_with_label("Send");
   gtk_box_pack_start(GTK_BOX(vbox), button, FALSE, FALSE, 0);

   g_signal_connect(button, "clicked", G_CALLBACK(on_send_button_clicked), entry);
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

   g_timeout_add(10, poll_mongoose, NULL);  // Poll Mongoose every 10ms

   gtk_widget_show_all(window);
   gtk_main();

   mg_mgr_free(&mgr);
   return 0;
}
