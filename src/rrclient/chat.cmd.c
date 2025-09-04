//
// rrclient/chat.cmd.c: Chat stuff that isn't GUI dependent
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/json.h"
#include "common/posix.h"
#include "common/util.string.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/connman.h"
#include "rrclient/ws.h"

extern bool dying;
extern struct mg_connection *ws_conn;
extern GtkWidget *chat_entry;
extern GtkWidget *rx_vol_slider;
extern GtkWidget *config_tab;
extern GtkWidget *main_notebook;
extern GtkWidget *main_tab;
extern GtkWidget *log_tab;
extern void show_help(const char *topic);		// ui.help.c
extern bool clear_syslog(void);
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
   if (strncasecmp(msg + 1, "server", 6) == 0) {
      const char *server = msg + 8;

      if (server && strlen(server) > 1) {
         ui_print("[%s] * Changing server profile to %s", get_chat_ts(), server);
         disconnect_server(server);
//         memset(active_server, 0, sizeof(active_server));
//         snprintf(active_server, sizeof(active_server), "%s", server);

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
   } else if (strncasecmp(msg + 1, "disconnect", 10) == 0) {
      disconnect_server(server_name);
   } else if (strncasecmp(msg + 1, "quit", 4) == 0) {
      char msgbuf[4096];
      prepare_msg(msgbuf, sizeof(msgbuf), 
         "{ \"auth\": { \"cmd\": \"quit\" } }", msg + 5);
      mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      dying = true;
   // Switch tabs
   } else if (strncasecmp(msg + 1, "chat", 4) == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), main_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
      }
   } else if (strncasecmp(msg + 1, "clear", 5) == 0) {
      gtk_text_buffer_set_text(text_buffer, "", -1);
   } else if (strncasecmp(msg + 1, "clearlog", 8) == 0) {
      clear_syslog();
   } else if (strncasecmp(msg + 1, "config", 6) == 0 || strcasecmp(msg + 1, "cfg") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), config_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
   } else if (strncasecmp(msg + 1, "log", 3) == 0 || strcasecmp(msg + 1, "syslog") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), log_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
   } else if (ws_conn) {
      if (msg[0] == '/') { // Handle local commands
         if (strcasecmp(msg + 1, "ban") == 0) {
         } else if (strncasecmp(msg + 1, "die", 3) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"args\": \"%s\" } }", msg + 5);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "edit", 4) == 0) {
         } else if (strncasecmp(msg + 1, "help", 4) == 0) {
            show_help(NULL);
         } else if (strncasecmp(msg + 1, "kick", 4) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"kick\", \"reason\": \"%s\", \"args\": { \"reason\": \"%s\" } } }", msg, "No reason given");
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "me", 2) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"msg\", \"data\": \"%s\", "
               "\"msg_type\": \"action\" } }", msg + 3);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "mute", 4) == 0) {
         } else if (strncasecmp(msg + 1, "names", 5) == 0) {
         } else if (strncasecmp(msg + 1, "restart", 7) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"reason\": \"%s\" } }", msg);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "rxmute", 6) == 0) {
         } else if (strncasecmp(msg + 1, "rxvol", 5) == 0) {
            gdouble val = atoi(msg + 7) / 100;
            gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
            ui_print("* Set rx-vol to %f", val);
         } else if (strncasecmp(msg + 1, "rxunmute", 8) == 0) {
         } else if (strncasecmp(msg + 1, "unmute", 6) == 0) {
         } else if (strncasecmp(msg + 1, "whois", 4) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"restart\": \"%s\", \"args\": { \"reason\": \"%s\" } } }", msg, "No reason given");
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"%s\" } }", msg + 1);
            
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         }
      } else {
         // not a match
         char msgbuf[4096];
         char *escaped_msg = json_escape(msg);

         prepare_msg(msgbuf, sizeof(msgbuf), 
            "{ \"talk\": { \"cmd\": \"msg\", \"data\": \"%s\", "
            "\"msg_type\": \"pub\" } }", escaped_msg);
         free(escaped_msg);
         mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      }
   }
   return false;
}
