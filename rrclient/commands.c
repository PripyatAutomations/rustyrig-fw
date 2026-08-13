//
// rrclient/commands.c: Chat stuff that isn't GUI dependent
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
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
#include <rrclient/ui.h>
#include <ev.h>

extern bool dying;
extern time_t now;
extern bool ws_connected;
extern bool rrclient_send_chat(const char *data);
extern void gui_show_help(const char *topic);            // ui.help.c
extern bool syslog_clear(void);
extern const char *server_name;	// remove this (connman.c)

#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
#endif

#if     defined(USE_GTK)
extern GtkWidget *chat_entry;
extern GtkWidget *rx_vol_slider;
extern GtkWidget *config_tab;
extern GtkWidget *main_notebook;
extern GtkWidget *main_tab;
extern GtkWidget *log_tab;
#endif // defined(USE_GTK)

//////////////////
bool cmd_chat(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), main_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
         gtk_widget_grab_focus( GTK_WIDGET(chat_entry) );
      }
#endif
   } else if (ui_mode == UI_MODE_TUI) {
   }
   return false;
}

bool cmd_config(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), config_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
#endif
   } else if (ui_mode == UI_MODE_TUI) {
   }
   return false;
}


bool cmd_clear(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      tui_clear_scrollback( tui_active_window() );
#if     defined(USE_GTK)
   } else {
      gtk_text_buffer_set_text(text_buffer, "", -1);
#endif
   }

   return false;
}

bool cmd_clearlog(int argc, char **args) {
   syslog_clear();
   return false;
}

bool cmd_die(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "die", VAL_STR, "talk.args", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );
   return false;
}

bool cmd_disconnect(int argc, char **args) {
   disconnect_server(server_name);
   return false;
}

bool cmd_join(int argc, char **args) {
   ui_print(NULL, "{yellow}JOIN is not supported over WebSocket{reset}");

   return false;
}

bool cmd_kick(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "kick", VAL_STR, "talk.reason", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );
   return false;
}

bool cmd_log(int argc, char **args) {
   if (ui_mode == UI_MODE_GTK) {
#if     defined(USE_GTK)
      int index = gtk_notebook_page_num(GTK_NOTEBOOK(main_notebook), log_tab);

      if (index != -1) {
         gtk_notebook_set_current_page(GTK_NOTEBOOK(main_notebook), index);
      }
#endif
   } else if (ui_mode == UI_MODE_TUI) {
   }
   return false;
}

bool cmd_me(int argc, char **args) {
   char buf[1024];
   memset(buf, 0, 1024);
   size_t pos = 0;

   for (int i = 1 ; i < argc ; i++) {
      int n = snprintf(buf + pos, sizeof(buf) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(buf) - pos) {
         break;
      }
      pos += n;
   }

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", buf, VAL_STR, "talk.msg_type",
      "action");

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

   for (int i = 2 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }

   ui_print(NULL, "-> %s %s", target, fullmsg);

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", fullmsg, VAL_STR, "talk.target",
      target);
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
#if     0
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

      for (int i = 2 ; i < argc ; i++) {
         int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 2 ? " " : ""), args[i] ? args[i] : "");

         if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
            break;
         }
         pos += n;
      }

      ui_print(wp, "-> *%s* %s", target, fullmsg);
      ui_print(wp, "{yellow}NOTICE is not supported over WebSocket{reset}");
   }
#endif

   return false;
}

bool cmd_part(int argc, char **args) {
   ui_print(NULL, "{yellow}PART is not supported over WebSocket{reset}");

   return false;
}

bool cmd_quit(int argc, char **args) {
   ui_print(NULL, "Goodbye!");

   const char *jp = dict2json_mkstr(VAL_STR, "auth.cmd", "quit", VAL_STR, "auth.msg", args[1]);
#if     defined(USE_MONGOOSE)
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif
   free( (char *)jp );

   // Set the dying flag so main loop with cleanly exit
   dying = true;

   return false;
}

bool cmd_quote(int argc, char **args) {
   if (argc < 1) {
      // XXX: cry not enough args
      return true;
   }
   char fullmsg[502];
   memset( fullmsg, 0, sizeof(fullmsg) );
   size_t pos = 0;

   for (int i = 1 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 1 ? " " : ""), args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }

   ui_print(NULL, "-raw-> %s", fullmsg);
   ui_print(NULL, "{yellow}QUOTE is not supported over WebSocket{reset}");

   return false;
}

bool cmd_restart(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "restart", VAL_STR, "talk.reason", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );

   return false;
}

bool cmd_rxvol(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      // do stuff
#if     defined(USE_GTK)
   } else if (ui_mode == UI_MODE_GTK) {
      gdouble val = atoi(args[1]) / 100;
      gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
      ui_print(NULL, "* Set rx-vol to %f", val);
#endif
   }
   return false;
}

bool cmd_server(int argc, char **args) {
   const char *server = args[1];

   if (server && strlen(server) > 1) {
      ui_print(NULL, "%s * Changing server profile to %s", get_chat_ts(now), server);
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
      ui_print(NULL, "Try /server servername to connect");
      show_server_chooser();
   }

   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "restart", VAL_STR, "talk.reason", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );

   return false;
}

bool cmd_topic(int argc, char **args) {
   ui_print(NULL, "{yellow}TOPIC is not supported over WebSocket{reset}");

   return false;
}

bool cmd_whois(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "whois", VAL_STR, "talk.args", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );

   return false;
}

bool cmd_win(int argc, char **args) {
   if (argc < 1) {
      return true;
   }

   if (ui_mode == UI_MODE_TUI) {
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

      ui_print(NULL, "ID: %s", args[1]);

      if (id < 1 || id > TUI_MAX_WINDOWS) {
         ui_print(NULL, "Invalid window %d, must be between 1 and %d", id, TUI_MAX_WINDOWS);

         return true;
      }
      tui_window_focus_id(id);
   }

   return false;
}


// This is below, it's a bit large and needs access to the client_cmds table anyways
bool cmd_help(int argc, char **args);

client_cmd_t client_cmds[] = {
   {
      .cmd = "chat", .cb = cmd_chat, .desc = "Focus the chat tab"
   },
   {
      .cmd = "clear", .cb = cmd_clear, .desc = "Clear the scrollback"
   },
   {
      .cmd = "clearlog", .cb = cmd_clearlog, .desc = "Clear the syslog tab"
   },
   {
      .cmd = "config", .cb = cmd_config, .desc = "Focus the configuration tab"
   },
   {
      .cmd = "die", .cb = cmd_die, .desc = "Shutdown the server"
   },
   {
      .cmd = "disconnect", .cb = cmd_disconnect, .desc = "Disconnect from server"
   },
   {
      .cmd = "help", .cb = cmd_help, .desc = "Show help message"
   },
   {
      .cmd = "kick", .cb = cmd_kick, .desc = "Kick a user from the rig"
   },
   {
      .cmd = "join", .cb = cmd_join, .desc = "Join a channel"
   },
   {
      .cmd = "me", .cb = cmd_me, .desc = "\tSend an action to the current channel"
   },
   {
      .cmd = "msg", .cb = cmd_msg, .desc = "Send a private message"
   },
/*
   {
      .cmd = "mute", .cb = cmd_mute, .desc = "Mute a user"
   },
*/
   {
      .cmd = "notice", .cb = cmd_notice, .desc = "Send a private notice"
   },
   {
      .cmd = "part", .cb = cmd_part, .desc = "leave a channel"
   },
   {
      .cmd = "quit", .cb = cmd_quit, .desc = "Exit the program"
   },
   {
      .cmd = "quote", .cb = cmd_quote, .desc = "Send a raw command"
   },
   {
      .cmd = "restart", .cb = cmd_restart, .desc = "Restart"
   },
   {
      .cmd = "rxvol", .cb = cmd_rxvol, .desc = "Set receive volume level"
   },
   {
      .cmd = "server", .cb = cmd_server, .desc = "Connect to a server"
   },
   {
      .cmd = "topic", .cb = cmd_topic, .desc = "Set channel topic (N/A over WS)"
   },
/*
   {
      .cmd = "unmute", .cb = cmd_unmute, .desc = "Unmute a user"
   },
*/
   {
      .cmd = "win", .cb = cmd_win, .desc = "Change windows"
   },
   {
      .cmd = "whois", .cb = cmd_whois, .desc = "Show client information"
   },
   {
      .cmd = NULL, .cb = NULL, .desc = NULL
   }
};

////////////////
// Help stuff //
////////////////
const char *help_msg[] = {
   "*** Keyboard Shortcuts ***",
   "   alt-X (1-0)\t\tSwitch to window 1-10",
   "   alt-left\t\tSwitch to previous win",
   "   alt-right\t\tSwitch to next win",
   "   F12\t\t\tPTT toggle",
   NULL
};

bool cmd_help(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      ui_print(NULL, "*** Available commands ***");

      for (int i = 0 ; client_cmds[i].cmd ; i++) {
         ui_print(NULL, "   %s\t\t%s", client_cmds[i].cmd, client_cmds[i].desc);
      }

      for (int i = 0 ; i < sizeof(help_msg) / sizeof(help_msg[0]) ; i++) {
         if (help_msg[i]) {
            ui_print(NULL, "%s", help_msg[i]);
         }
      }
   } else if (ui_mode == UI_MODE_GTK) {
      gui_show_help(NULL);
   }

   return false;
}

//////////////////////////////
bool parse_chat_input_real(const char *msg) {
   if (!msg || !*msg) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%p> is empty", msg);
      return true;
   }

#if defined(USE_MONGOOSE)
   if (msg[0] == '/') {
      if (!msg[1]) {
         return true;
      }

      char *input = strdup(msg + 1);
      if (!input) {
         return true;
      }

      char *cmd_argv[32];
      int cmd_argc = 0;
      const int max_argv = sizeof(cmd_argv) / sizeof(cmd_argv[0]);

      char *p = input;

      /* cmd_argv[0] = command */
      cmd_argv[cmd_argc++] = p;

      while (*p && !isspace((unsigned char)*p)) {
         p++;
      }

      if (*p) {
         *p++ = '\0';
      }

      /* Find command */
      client_cmd_t *cmd = NULL;

      for (int i = 0; client_cmds[i].cmd; i++) {
         if (strcasecmp(cmd_argv[0], client_cmds[i].cmd) == 0) {
            cmd = &client_cmds[i];
            break;
         }
      }

      if (!cmd) {
         free(input);
         return true;
      }

      int max_args = cmd->max_args ? cmd->max_args : 1;

      while (*p && cmd_argc < max_argv && cmd_argc <= max_args) {
         while (isspace((unsigned char)*p)) {
            p++;
         }

         if (!*p) {
            break;
         }

         cmd_argv[cmd_argc++] = p;

         /*
          * If this is the last permitted argument, leave the rest
          * intact as one space-separated argument.
          */
         if (cmd_argc == max_args + 1) {
            break;
         }

         while (*p && !isspace((unsigned char)*p)) {
            p++;
         }

         if (*p) {
            *p++ = '\0';
         }
      }

      Log(LOG_DEBUG, "chat.cmd", "command=%s argc=%d", cmd_argv[0], cmd_argc);

      if (cmd->cb) {
         cmd->cb(cmd_argc, cmd_argv);
      }

      free(input);
   } else {
      const char *jp = dict2json_mkstr(
         VAL_STR, "talk.cmd", "msg",
         VAL_STR, "talk.data", msg,
         VAL_STR, "talk.msg_type", "pub"
      );

      if (ws_conn) {
         mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
      }

      free((char *)jp);
   }
#endif

   return false;
}

bool parse_chat_input(GtkButton *button, gpointer entry) {
   if (!button || !entry) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: button:<%p> entry:<%p>", button, entry);

      return true;
   }
   const gchar *msg = gtk_entry_get_text( GTK_ENTRY(chat_entry) );

   return parse_chat_input_real(msg);
}
