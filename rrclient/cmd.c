//
// rrclient/commands.c: Command parser
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
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <ev.h>

extern bool dying;
extern time_t now;
extern bool rrclient_send_chat(const char *data);
extern bool syslog_clear(void);
extern const char *server_name; // remove this (connman.c)

#ifdef	USE_MONGOOSE
extern struct mg_connection *ws_conn;
#endif

client_cmd_t client_cmds[] = {
   {
      .cmd = "admin", .cb = cmd_admin, .desc = "Focus the admin tab"
   },
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
      .cmd = "log", .cb = cmd_log, .desc = "Switch to log tab"
   },
   {
      .cmd = "me", .cb = cmd_me, .desc = "Send an action to the current channel"
   },
   {
      .cmd = "msg", .cb = cmd_msg, .desc = "Send a private message"
   },
/*
 *  {
 *     .cmd = "mute", .cb = cmd_mute, .desc = "Mute a user"
 *  },
 */
   {
      .cmd = "notice", .cb = cmd_notice, .desc = "Send a private notice"
   },
   {
      .cmd = "part", .cb = cmd_part, .desc = "Leave a channel"
   },
   {
      .cmd = "quit", .cb = cmd_quit, .desc = "Exit the program"
   },
   {
      .cmd = "quote", .cb = cmd_quote, .desc = "Send a raw command"
   },
   {
      .cmd = "restart", .cb = cmd_restart, .desc = "Restart the server"
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
 *  {
 *     .cmd = "unmute", .cb = cmd_unmute, .desc = "Unmute a user"
 *  },
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

bool parse_chat_input_real(const char *msg) {
   if (!msg || !*msg) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%p> is empty", msg);

      return true;
   }

#ifdef  USE_MONGOOSE
   if (msg[0] == '/') {
      Log(LOG_CRIT, "debug", "msg<%d>: %s", strlen(msg), msg);

      if (!msg[1]) {
         return true;
      }

      const char *mp = msg + 1;
      client_cmd_t *cmd = NULL;

      /* Find the command. */
      for (int i = 0 ; client_cmds[i].cmd ; i++) {
         const char *p = mp;
         const char *c = client_cmds[i].cmd;

         while (*p && *c &&
                !isspace( (unsigned char)*p ) &&
                tolower( (unsigned char)*p ) == tolower( (unsigned char)*c ) ) {
            p++;
            c++;
         }

         if (!*c && (!*p || isspace( (unsigned char)*p ) ) ) {
            cmd = &client_cmds[i];
            break;
         }
      }

      if (!cmd || !cmd->cb) {
         return true;
      }

      /*
       * Work on a writable copy since we're going to replace whitespace with NUL
       * terminators.
       */
      char *input = strdup(mp);

      if (!input) {
         Log(LOG_CRIT, "chat.cmd", "Out of memory parsing command");

         return true;
      }

      /*
       * Keep this reasonably sized. cmd_argv is only used for the duration of the
       * callback.
       */
      char *cmd_argv[32];
      int cmd_argc = 0;

      char *p = input;

      /* argv[0] is the command itself. */
      cmd_argv[cmd_argc++] = p;

      while (*p && !isspace( (unsigned char)*p ) ) {
         p++;
      }

      if (*p) {
         *p++ = '\0';
      }

      /*
       * max_args is the number of arguments after argv[0]. Zero means one argument
       * containing the remainder.
       */
      int max_args = cmd->max_args ? cmd->max_args : 1;

      while (*p && cmd_argc < (int)(sizeof(cmd_argv) / sizeof(cmd_argv[0]) ) ) {
         while (isspace( (unsigned char)*p ) ) {
            p++;
         }

         if (!*p) {
            break;
         }

         cmd_argv[cmd_argc++] = p;

         /*
          * If this is the last allowed argument, leave the remainder of the string
          * intact.
          */
         if (cmd_argc - 1 >= max_args) {
            break;
         }

         while (*p && !isspace( (unsigned char)*p ) ) {
            p++;
         }

         if (*p) {
            *p++ = '\0';
         }
      }
      Log(LOG_CRIT, "chat.cmd", "command=%s argc=%d max_args=%d", cmd_argv[0], cmd_argc, max_args);

      cmd->cb(cmd_argc, cmd_argv);

      free(input);
   } else {
#ifdef	USE_MONGOOSE
      if (!ws_connected) {
         ui_print(NULL, "{red}*** Not connected to server ***{reset}");

         return false;
      }
#endif
      const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", msg, VAL_STR, "talk.msg_type",
         "pub");

      if (ws_conn) {
         mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
      }

      free( (char *)jp );
   }
#endif /* USE_MONGOOSE */

   return false;
}

#ifdef	USE_GTK
bool parse_chat_input_gtk(GtkButton *button, gpointer entry) {
   if (!button || !entry) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: button:<%p> entry:<%p>", button, entry);

      return true;
   }
   const gchar *msg = gtk_entry_get_text( GTK_ENTRY(chat_entry) );

   return parse_chat_input_real(msg);
}
#endif // USE_GTK
