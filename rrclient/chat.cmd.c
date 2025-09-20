//
// rrclient/chat.cmd.c: Chat stuff that isn't GUI dependent
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include <rrclient/auth.h>
#include <mod.ui.gtk3/gtk.core.h>
#include <rrclient/connman.h>

extern bool dying;
extern time_t now;
extern struct mg_connection *ws_conn;
extern GtkWidget *chat_entry;
extern GtkWidget *rx_vol_slider;
extern GtkWidget *config_tab;
extern GtkWidget *main_notebook;
extern GtkWidget *main_tab;
extern GtkWidget *log_tab;
extern void show_help(const char *topic);		// ui.help.c
extern bool syslog_clear(void);
extern const char *server_name;				// connman.c XXX: to remove ASAP for multiserver

bool parse_chat_input(GtkButton *button, gpointer entry) {
   if (!button || !entry) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: button:<%x> entry:<%x>", button, entry);
      return true;
   }

   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(chat_entry));

   if (!msg || strlen(msg) < 1) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%x> is empty", msg);
      return true;
   }

   // These commands should always be available
   if (strncasecmp(msg, "/server", 6) == 0) {
      const char *server = msg + 8;

      if (server && strlen(server) > 1) {
         ui_print("[%s] * Changing server profile to %s", get_chat_ts(now), server);
         disconnect_server(server);

         if (server_name) {
            free((char *)server_name);
            server_name = strdup(server);

            if (!server_name) {
               fprintf(stderr, "OOM in parse_chat_input /server\n");
               return true;
            }
         }
         Log(LOG_DEBUG, "gtk.core", "Set server profile to %s by console cmd", server);
         connect_server(server);
      } else {
         ui_print("Try /server servername to connect");
         show_server_chooser();
      }
   } else if (strncasecmp(msg, "/disconnect", 10) == 0) {
      disconnect_server(server_name);
   } else if (strncasecmp(msg, "/quit", 4) == 0) {
      const char *jp = dict2json_mkstr(VAL_STR, "auth.cmd", "quit", VAL_STR, "auth.msg", msg + 5);
      mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
      free((char *)jp);
      dying = true;
      // Switch tabs
   } else if (strncasecmp(msg, "/chat", 4) == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), main_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
      }
   } else if (strncasecmp(msg, "/clear", 5) == 0) {
      gtk_text_buffer_set_text(text_buffer, "", -1);
   } else if (strncasecmp(msg, "/clearlog", 8) == 0) {
      syslog_clear();
   } else if (strncasecmp(msg, "/config", 6) == 0 || strcasecmp(msg, "/cfg") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), config_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
   } else if (strncasecmp(msg, "/log", 3) == 0 || strcasecmp(msg, "/syslog") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), log_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
   } else if (ws_conn) {
      if (msg[0] == '/') { // Handle local commands
         if (strcasecmp(msg, "/ban") == 0) {
         } else if (strncasecmp(msg, "/die", 3) == 0) {
            const char *jp = dict2json_mkstr(
               VAL_STR, "talk.cmd", "die",
               VAL_STR, "talk.args", msg + 5);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free((char *)jp);
         } else if (strncasecmp(msg, "/edit", 4) == 0) {
         } else if (strncasecmp(msg, "/help", 4) == 0) {
            show_help(NULL);
         } else if (strncasecmp(msg, "/kick", 4) == 0) {
            const char *jp = dict2json_mkstr(
               VAL_STR, "talk.cmd", "kick",
               VAL_STR, "talk.reason", msg + 6);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free((char *)jp);
         } else if (strncasecmp(msg, "/me", 2) == 0) {
            const char *jp = dict2json_mkstr(
               VAL_STR, "talk.cmd", "msg",
               VAL_STR, "talk.data", msg + 3,
               VAL_STR, "talk.msg_type", "action");
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg, "/mute", 4) == 0) {
         } else if (strncasecmp(msg, "/names", 5) == 0) {
         } else if (strncasecmp(msg, "/restart", 7) == 0) {
            const char *jp = dict2json_mkstr(
               VAL_STR, "talk.cmd", "restart",
               VAL_STR, "talk.reason", msg + 8);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free((char *)jp);
         } else if (strncasecmp(msg, "/rxmute", 6) == 0) {
         } else if (strncasecmp(msg, "/rxvol", 5) == 0) {
            gdouble val = atoi(msg + 7) / 100;
            gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
            ui_print("* Set rx-vol to %f", val);
         } else if (strncasecmp(msg, "/rxunmute", 8) == 0) {
         } else if (strncasecmp(msg, "/unmute", 6) == 0) {
         } else if (strncasecmp(msg, "/whois", 4) == 0) {
            const char *jp = dict2json_mkstr(
               VAL_STR, "talk.cmd", "whois",
               VAL_STR, "talk.args", msg + 7);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free((char *)jp);
         } else {
            char msgbuf[4096];
            const char *jp = dict2json_mkstr(
               VAL_STR, "talk.cmd", msg + 1);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free((char *)jp);
         }
      } else {
         // not a match
         const char *jp = dict2json_mkstr(
            VAL_STR, "talk.cmd", "msg",
            VAL_STR, "talk.data", msg,
            VAL_STR, "talk.msg_type", "pub");
         mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         free((char *)jp);
      }
   }
   return false;
}
