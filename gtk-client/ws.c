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
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"

extern dict *cfg;		// config.c
struct mg_mgr mgr;
bool ws_connected = false;
struct mg_connection *ws_conn = NULL;

void ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_CONNECT) {
      const char *url = dict_get(cfg, "server.url", NULL);

      // XXX: Need to figure out how to deal with the certificate authority file?
      if (c->is_tls) {
         struct mg_tls_opts opts = {.ca = mg_unpacked("/certs/ca.pem"),
                                 .name = mg_url_host(url)};
         mg_tls_init(c, &opts);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      const char *login_user = dict_get(cfg, "server.user", NULL);
      ws_connected = true;
      update_connection_button(true, conn_button);
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-active");
      gtk_style_context_remove_class(ctx, "ptt-idle");

      if (login_user == NULL) {
         Log(LOG_CRIT, "core", "server.user not set in config!");
         exit(1);
      }

      ui_print("ws opened, sending login");
      ws_send_login(c, login_user);

      Log(LOG_INFO, "core", "Sending login for user %s", login_user);
      ui_print("Login request sent for user %s", login_user);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
      char tmp_buf[wm->data.len + 2];
      memset(tmp_buf, 0, sizeof(tmp_buf));
      memcpy(tmp_buf, wm->data.buf, wm->data.len);
      tmp_buf[wm->data.len + 1] = '\n';
      Log(LOG_DEBUG, "ws", "Got: %s", tmp_buf);
      ui_print(tmp_buf);
   } else if (ev == MG_EV_ERROR) {
      ui_print("TLS error: %s", (char *)ev_data);
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = false;
      update_connection_button(false, conn_button);
      GtkStyleContext *ctx = gtk_widget_get_style_context(conn_button);
      gtk_style_context_add_class(ctx, "ptt-idle");
      gtk_style_context_remove_class(ctx, "ptt-active");
   }
}

void ws_init(void) {
   mg_log_set(MG_LL_DEBUG);  // or MG_LL_VERBOSE for even more
   mg_mgr_init(&mgr);
}

void ws_fini(void) {
   mg_mgr_free(&mgr);
}
