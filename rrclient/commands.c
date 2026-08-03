//
// rrclient/commands.c: Chat stuff that isn't GUI dependent
//    This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/command.h>
#include <ev.h>

extern bool dying;
extern bool ui_mode_gui;
extern time_t now;

#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
extern bool ws_connected;
extern bool rrclient_send_chat(const char *data);
#endif

#if     defined(USE_GTK)
#include <gtk/gtk.h>
#include <mod.ui.gtk3/gtk.core.h>
extern GtkWidget *chat_entry;
extern GtkWidget *rx_vol_slider;
extern GtkWidget *config_tab;
extern GtkWidget *main_notebook;
extern GtkWidget *main_tab;
extern GtkWidget *log_tab;
#endif // defined(USE_GTK)

extern void gui_show_help(const char *topic);           // ui.help.c
extern bool syslog_clear(void);
extern const char *server_name;                         // connman.c XXX: to remove ASAP for multiserver

bool parse_chat_input(GtkButton *button, gpointer entry) {
   if (!button || !entry) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: button:<%p> entry:<%p>", button, entry);

      return true;
   }
   const gchar *msg = gtk_entry_get_text( GTK_ENTRY(chat_entry) );
   if (!msg || strlen(msg) < 1) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%p> is empty", msg);

      return true;
   }
   // These commands should always be available
   if (strncasecmp(msg, "/server", 6) == 0) {
      const char *server = msg + 8;
      if (server && strlen(server) > 1) {
         ui_print("%s * Changing server profile to %s", get_chat_ts(now), server);
         disconnect_server(server);
         if (server_name) {
            free( (char *)server_name );
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
#if     defined(USE_MONGOOSE)
      mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif
      free( (char *)jp );
      dying = true;
      // Switch tabs
   } else if (strncasecmp(msg, "/chat", 4) == 0) {
#if     defined(USE_GTK)
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), main_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         gtk_widget_grab_focus( GTK_WIDGET(chat_entry) );
      }
#endif
   } else if (strncasecmp(msg, "/clear", 5) == 0) {
      if (!ui_mode_gui) {
#if     defined(USE_GTK)
      } else {
         gtk_text_buffer_set_text(text_buffer, "", -1);
#endif
      }
   } else if (strncasecmp(msg, "/clearlog", 8) == 0) {
      syslog_clear();
   } else if (strncasecmp(msg, "/config", 6) == 0 || strcasecmp(msg, "/cfg") == 0) {
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), config_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
   } else if (strncasecmp(msg, "/log", 3) == 0 || strcasecmp(msg, "/syslog") == 0) {
#if     defined(USE_GTK)
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), log_tab);
      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
#endif
#if     defined(USE_MONGOOSE)
   } else if (ws_conn) {
      if (msg[0] == '/') { // Handle local commands
         if (strcasecmp(msg, "/ban") == 0) {
         } else if (strncasecmp(msg, "/die", 3) == 0) {
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "die", VAL_STR, "talk.args",
               msg + 5);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (char *)jp );
         } else if (strncasecmp(msg, "/edit", 4) == 0) {
         } else if (strncasecmp(msg, "/help", 4) == 0) {
            gui_show_help(NULL);
         } else if (strncasecmp(msg, "/kick", 4) == 0) {
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "kick", VAL_STR, "talk.reason",
               msg + 6);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (char *)jp );
         } else if (strncasecmp(msg, "/me", 2) == 0) {
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data",
               msg + 3, VAL_STR, "talk.msg_type", "action");
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         } else if (strncasecmp(msg, "/mute", 4) == 0) {
         } else if (strncasecmp(msg, "/names", 5) == 0) {
         } else if (strncasecmp(msg, "/restart", 7) == 0) {
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "restart", VAL_STR, "talk.reason",
               msg + 8);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (char *)jp );
         } else if (strncasecmp(msg, "/rxmute", 6) == 0) {
         } else if (strncasecmp(msg, "/rxvol", 5) == 0) {
            if (!ui_mode_gui) {
#if     defined(USE_GTK)
            } else {
               gdouble val = atoi(msg + 7) / 100;
               gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
               ui_print("* Set rx-vol to %f", val);
#endif
            }
         } else if (strncasecmp(msg, "/rxunmute", 8) == 0) {
         } else if (strncasecmp(msg, "/unmute", 6) == 0) {
         } else if (strncasecmp(msg, "/whois", 4) == 0) {
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "whois", VAL_STR, "talk.args",
               msg + 7);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (char *)jp );
         } else {
            char msgbuf[4096];
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", msg + 1);
            mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
            free( (char *)jp );
         }
      } else {
         // not a match
         const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", msg,
            VAL_STR, "talk.msg_type", "pub");
         mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         free( (char *)jp );
      }
#endif // defined(USE_MONGOOSE)
   }
   return false;
}

//////////////////
bool cmd_join(int argc, char **args) {
   (void)argc; (void)args;
   tui_print_win(tui_active_window(), "{yellow}JOIN is not supported over WebSocket{reset}");

   return false;
}

bool cmd_me(int argc, char **args) {
   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;
   for (int i = 1;i < argc;i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%s", (i > 1 ? " " : ""),
         args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", buf, VAL_STR,
      "talk.msg_type", "action");

#if defined(USE_MONGOOSE)
   if (ws_conn) {
      mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   }
#endif
   free( (void *)jp );

   return false;
}

bool cmd_msg(int argc, char **args) {
   if (argc < 2) {
      return true;
   }
   char *target = args[1];
   char fullmsg[502];
   memset( fullmsg, 0, sizeof(fullmsg) );
   size_t pos = 0;

   for (int i = 2;i < argc;i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""),
         args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }
   tui_window_t *wp = tui_active_window();
   tui_print_win(wp, "-> %s %s", target, fullmsg);

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", fullmsg,
      VAL_STR, "talk.target", target);
#if defined(USE_MONGOOSE)
   if (ws_conn) {
      mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   }
#endif
   free( (void *)jp );

   return false;
}

bool cmd_notice(int argc, char **args) {
   if (argc < 2) {
      // XXX: cry not enough args
      return true;
   }
   tui_window_t *wp = NULL;
   bool new_win = false;
   if (*args[1]) {
      wp = tui_window_find(args[1]);
      if (!wp) {
         new_win = true;
         wp = tui_window_create(args[1]);
         wp->cptr = tui_active_window()->cptr;
      }
   }
   if (!wp) {
      wp = tui_active_window();
   }
   // There's a window here at least...
   if (wp->cptr) {
      char *target = wp->title;
      if (args[1]) {
         target = args[1];
      }
      char fullmsg[502];
      memset( fullmsg, 0, sizeof(fullmsg) );
      size_t pos = 0;

      for (int i = 2;i < argc;i++) {
         int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""),
            args[i] ? args[i] : "");
         if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
            break;
         }
         pos += n;
      }
      tui_print_win(wp, "-> *%s* %s", target, fullmsg);
      tui_print_win(wp, "{yellow}NOTICE is not supported over WebSocket{reset}");
   }
   return false;
}

bool cmd_part(int argc, char **args) {
   (void)argc; (void)args;
   tui_print_win(tui_active_window(), "{yellow}PART is not supported over WebSocket{reset}");

   return false;
}

bool cmd_quit(int argc, char **args) {
   (void)argc; (void)args;
   tui_window_t *wp = tui_active_window();
   tui_print_win(wp, "Goodbye!");

   // Set the dying flag so main loop with cleanly exit
   dying = true;

   return false;
}

bool cmd_quote(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }
   tui_window_t *wp = tui_active_window();
   char fullmsg[502];
   memset( fullmsg, 0, sizeof(fullmsg) );
   size_t pos = 0;

   for (int i = 1;i < argc;i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 1 ? " " : ""),
         args[i] ? args[i] : "");
      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }
   tui_print_win(wp, "-raw-> %s", fullmsg);
   tui_print_win(wp, "{yellow}QUOTE is not supported over WebSocket{reset}");

   return false;
}

bool cmd_topic(int argc, char **args) {
   (void)argc; (void)args;
   tui_print_win(tui_active_window(), "{yellow}TOPIC is not supported over WebSocket{reset}");

   return false;
}

bool cmd_whois(int argc, char **args) {
   (void)argc; (void)args;
   tui_print_win(tui_active_window(), "{yellow}WHOIS is not supported over WebSocket{reset}");

   return false;
}

bool cmd_win(int argc, char **args) {
   if (argc < 1) {
      return true;
   }
   if (!ui_mode_gui) {
      if (strcasecmp(args[1], "close") == 0) {
         Log(LOG_CRIT, "test", "argc: %d args0: %s args1: %s", argc, args[0], args[1]);
         if (argc < 2) {
            return true;
         }
         int id = -1;
         if (argc >= 3) {
            id = atoi(args[2]);
         } else {
            return tui_window_destroy( tui_active_window() );
         }
         if (id > 0) {
            tui_window_destroy_id(id);

            return false;
         }
         return true;
      }
      int id = atoi(args[1]);

      tui_print_win(tui_active_window(), "ID: %s", args[1]);
      if (id < 1 || id > TUI_MAX_WINDOWS) {
         tui_print_win(tui_active_window(), "Invalid window %d, must be between 1 and %d", id,
            TUI_MAX_WINDOWS);

         return true;
      }
      tui_window_focus_id(id);
   }
   return false;
}

bool cmd_clear(int argc, char **args) {
   if (!ui_mode_gui) {
      tui_clear_scrollback( tui_active_window() );
   }
   return false;
}

extern bool cmd_help(int argc, char **args);
client_cmd_t client_cmds[] = {
   { .cmd = "/clear", .cb = cmd_clear, .desc = "Clear the scrollback" },
   { .cmd = "/help", .cb = cmd_help, .desc = "Show help message" },
   { .cmd = "/join", .cb = cmd_join, .desc = "Join a channel (N/A over WS)" },
   { .cmd = "/me", .cb = cmd_me, .desc = "\tSend an action to the current channel" },
   { .cmd = "/msg", .cb = cmd_msg, .desc = "Send a private message" },
   { .cmd = "/notice", .cb = cmd_notice, .desc = "Send a private notice (N/A over WS)" },
   { .cmd = "/part", .cb = cmd_part, .desc = "leave a channel (N/A over WS)" },
   { .cmd = "/quit", .cb = cmd_quit, .desc = "Exit the program" },
   { .cmd = "/quote", .cb = cmd_quote, .desc = "Send a raw command (N/A over WS)" },
   { .cmd = "/topic", .cb = cmd_topic, .desc = "Set channel topic (N/A over WS)" },
   { .cmd = "/win", .cb = cmd_win, .desc = "Change windows" },
   { .cmd = "/whois", .cb = cmd_whois, .desc = "Show client information (N/A over WS)" },
   { .cmd = NULL, .cb = NULL, .desc = NULL }
};

bool cmd_help(int argc, char **args) {
   if (!ui_mode_gui) {
      tui_window_t *wp = tui_active_window();
      if (!wp) {
         return true;
      }
      tui_print_win(wp, "*** Available commands ***");
      for (int i = 0;client_cmds[i].cmd;i++) {
         tui_print_win(wp, "   %s\t\t%s", client_cmds[i].cmd, client_cmds[i].desc);
      }
      tui_print_win(wp, "");
      tui_print_win(wp, "*** Keyboard Shortcuts ***");
      tui_print_win(wp, "   alt-X (1-0)\t\tSwitch to window 1-10");
      tui_print_win(wp, "   alt-left\t\tSwitch to previous win");
      tui_print_win(wp, "   alt-right\t\tSwitch to next win");
      tui_print_win(wp, "   F12\t\t\tPTT toggle");
   }
   return false;
}
