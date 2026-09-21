//
// rrclient/commands.c: Command parser
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
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
#include <rrclient/userlist.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
#include <rrclient/ui.h>
#include <rrclient/ui.h>
#ifdef USE_GTK
#include <rrclient/gtk.chat.h>
#endif

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;
extern dict *cfg;



bool cmd_reload(int argc, char **args) {
   cfg_reload(config_file);
   if (ui_mode == UI_MODE_TUI) tui_redraw_screen();
   return false;
}

///////////////////////////////////////////////
client_cmd_t client_cmds[] = {
   { .cmd = "chat", .cb = cmd_chat, .desc = "Focus the chat tab" },
   { .cmd = "clear", .cb = cmd_clear, .desc = "Clear the scrollback" },
   { .cmd = "config", .cb = cmd_config, .desc = "Focus the configuration tab" },
   { .cmd = "die", .cb = cmd_die, .admin = true, .desc = "Shutdown the server" },
   { .cmd = "disconnect", .cb = cmd_disconnect, .desc = "Disconnect from server" },
#ifdef	USE_GTK
   { .cmd = "clearlog", .cb = cmd_clearlog, .desc = "Clear the syslog tab" },
#endif	// USE_GTK
   { .cmd = "help", .cb = cmd_help, .desc = "Show help message" },
   { .cmd = "kick", .cb = cmd_kick, .admin = true, .desc = "Kick a user from the rig" },
   { .cmd = "join", .cb = cmd_join, .desc = "Join a channel" },
   { .cmd = "log", .cb = cmd_log, .desc = "Switch to log tab" },
   { .cmd = "me", .cb = cmd_me, .desc = "Send an action to the current channel" },
   { .cmd = "msg", .cb = cmd_msg, .desc = "Send a private message" },
   { .cmd = "mute", .cb = cmd_mute, .admin = true, .desc = "Mute a user" },
   { .cmd = "names", .cb = cmd_names, .desc = "List users with privilege flags" },
   { .cmd = "notice", .cb = cmd_notice, .desc = "Send a private notice" },
   { .cmd = "part", .cb = cmd_part, .desc = "Leave a channel" },
   { .cmd = "quit", .cb = cmd_quit, .desc = "Exit (/quit [-yes|-y|y|yes] skips confirm)" },
   { .cmd = "qrz", .cb = cmd_qrz, .max_args = 1, .desc = "Look up a callsign" },
   { .cmd = "grid", .cb = cmd_grid, .max_args = 1, .desc = "Look up a grid square or coordinates" },
   { .cmd = "raw", .cb = cmd_raw, .desc = "Send a raw command" },
   { .cmd = "media", .cb = cmd_media, .max_args = 2, .desc = "Media channels: LIST | SUBSCRIBE <uuid|#> | UNSUBSCRIBE <uuid|#>" },
#ifdef USE_GTK
   { .cmd = "webcam", .cb = cmd_webcam, .max_args = 1, .desc = "Toggle the webcam viewer window (SHOW | HIDE)" },
#endif
   { .cmd = "syslog", .cb = cmd_syslog, .desc = "Toggle server host log stream (/syslog on|off)" },
   { .cmd = "rehash", .cb = cmd_rehash, .admin = true, .desc = "Ask server to reload config & users" },
   { .cmd = "quota", .cb = cmd_quota, .max_args = 8, .admin = true, .desc = "TX quota admin (LIST|SHOW|ADD|RESET|SET)" },
   { .cmd = "reload", .cb = cmd_reload, .desc = "Reload config file" },
   { .cmd = "restart", .cb = cmd_restart, .admin = true, .desc = "Restart the server" },
   { .cmd = "rxvol", .cb = cmd_rxvol, .desc = "Set receive volume level" },
   { .cmd = "rxcodec", .cb = cmd_rxcodec, .max_args = 3, .desc = "RX codecs: [LIST | <codec>|NONE [uuid|#number]]" },
   { .cmd = "txcodec", .cb = cmd_txcodec, .max_args = 3, .desc = "TX codecs: [LIST | <codec>|NONE [uuid|#number]]" },
   { .cmd = "server", .cb = cmd_server, .desc = "Connect to a server" },
   { .cmd = "topic", .cb = cmd_topic, .desc = "Set channel topic (N/A over WS)" },
   { .cmd = "admin", .cb = cmd_admin, .desc = "Focus the admin tab" },
   { .cmd = "unmute", .cb = cmd_unmute, .admin = true, .desc = "Unmute a user" },
   { .cmd = "win", .cb = cmd_win, .desc = "Change windows" },
   { .cmd = "whois", .cb = cmd_whois, .desc = "Show client information" },
   { .cmd = NULL, .cb = NULL, .desc = NULL }
};
////////////////////////////////////////////////



bool parse_chat_input_real(const char *msg) {
   if (!msg || !*msg) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%p> is empty", msg);

      return true;
   }

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
         ui_print(NULL, "{red}Invalid command: /%s{reset}", mp);
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
      /*
       * When we break on the max_args limit the remainder is left intact, which
       * can leave trailing whitespace on the last argument (e.g. tab-completed
       * '/whois admin '). Strip it so arguments are clean.
       */
      char *last = cmd_argv[cmd_argc - 1];
      char *end = last + strlen(last);

      while (end > last && isspace( (unsigned char)end[-1] ) ) {
         end--;
      }
      *end = '\0';

      Log(LOG_CRAZY, "chat.cmd", "command=%s argc=%d max_args=%d", cmd_argv[0], cmd_argc, max_args);

      // Admin-only commands are rejected for non-staff users (and hidden
      // from /help); staff is set by the server from admin|owner privs.
      if (cmd->admin && !media_have_priv("admin|owner") ) {
         ui_print(NULL, "{red}You do not have enough privileges to use '/%s'{reset}", cmd_argv[0]);
         free(input);
         return false;
      }
      cmd->cb(cmd_argc, cmd_argv);
      free(input);
   } else {
      if (!ws_connected) {
         ui_print(NULL, "{red}*** Not connected to server ***{reset}");
         return false;
      }
      dict *d = dict_new();
      dict_add(d, "msg.type", "talk");
      dict_add(d, "talk.cmd", "msg");
      dict_add(d, "talk.data", msg);
      dict_add(d, "talk.msg_type", "pub");
#ifdef USE_GTK
      /* GTK chat tabs represent rooms.  Include the selected tab's room so
       * side-room messages are delivered there instead of defaulting to the
       * authoritative rig room on the server. */
      const char *room = gtk_chat_current_room();
      if (room && room[0]) {
         dict_add(d, "talk.target", room);
      }
#endif

      if (ws_conn) {
         ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
      }
      dict_free(d);
   }

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
