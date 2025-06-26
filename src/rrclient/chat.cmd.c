//
// rrclient/chat.cmd.c: Core of GTK gui
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
#include "common/posix.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern bool dying;
extern bool ws_connected;
extern struct mg_connection *ws_conn;
extern GtkWidget *chat_entry;
extern GtkWidget *rx_vol_slider;
extern GtkWidget *config_tab;
extern GtkWidget *notebook;
extern GtkWidget *main_tab;
extern GtkWidget *log_tab;
extern void show_help(void);						// ui.help.c
extern bool clear_syslog(void);
extern const char *get_chat_ts(void);

bool parse_chat_input(GtkButton *button, gpointer entry) {
   const gchar *msg = gtk_entry_get_text(GTK_ENTRY(chat_entry));

   // These commands should always be available
   if (strncasecmp(msg + 1, "server", 6) == 0) {
      const char *server = msg + 8;

      if (server && strlen(server) > 1) {
         ui_print("[%s] * Changing server profile to %s", get_chat_ts(), server);
         disconnect_server();
         memset(active_server, 0, sizeof(active_server));
         snprintf(active_server, sizeof(active_server), "%s", server);
         Log(LOG_DEBUG, "gtk-gui", "Set server profile to %s by console cmd", active_server);
         connect_server();
      } else {
         show_server_chooser();
      }
   } else if (strncasecmp(msg + 1, "disconnect", 10) == 0) {
      disconnect_server();
   } else if (strncasecmp(msg + 1, "quit", 4) == 0) {
      char msgbuf[4096];
      prepare_msg(msgbuf, sizeof(msgbuf), 
         "{ \"auth\": { \"cmd\": \"quit\" } }", msg + 5);
      mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      dying = true;
   // Switch tabs
   } else if (strncasecmp(msg + 1, "chat", 4) == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), main_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), index);
         gtk_widget_grab_focus(GTK_WIDGET(chat_entry));
      }
   } else if (strncasecmp(msg + 1, "clear", 5) == 0) {
      gtk_text_buffer_set_text(text_buffer, "", -1);
   } else if (strncasecmp(msg + 1, "clearlog", 8) == 0) {
      clear_syslog();
   } else if (strncasecmp(msg + 1, "config", 6) == 0 || strcasecmp(msg + 1, "cfg") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), config_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), index);
      }
   } else if (strncasecmp(msg + 1, "log", 3) == 0 || strcasecmp(msg + 1, "syslog") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), log_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), index);
      }
   } else if (ws_conn && msg && *msg) {			// These commands only work when online
      if (msg[0] == '/') { // Handle local commands
         if (strcasecmp(msg + 1, "ban") == 0) {
         } else if (strncasecmp(msg + 1, "die", 3) == 0) {
            char msgbuf[4096];
            prepare_msg(msgbuf, sizeof(msgbuf), 
               "{ \"talk\": { \"cmd\": \"die\", \"args\": \"%s\" } }", msg + 5);
            mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg + 1, "edit", 4) == 0) {
         } else if (strncasecmp(msg + 1, "help", 4) == 0) {
            show_help();
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
           // Invalid command
           ui_print("Invalid command: %s", msg);
         }
      } else {
         char msgbuf[4096];
         prepare_msg(msgbuf, sizeof(msgbuf), 
            "{ \"talk\": { \"cmd\": \"msg\", \"data\": \"%s\", "
            "\"msg_type\": \"pub\" } }", msg);
         
         mg_ws_send(ws_conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      }
   }
   return false;
}
