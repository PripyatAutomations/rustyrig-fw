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
#include <rrclient/objects.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/sercom.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;
extern dict *cfg;



bool cmd_reload(int argc, char **args) {
   cfg_reload(config_file);

   if (ui_mode == UI_MODE_TUI) {
      tui_redraw_screen();
   }

   return false;
}

bool cmd_webcam(int argc, char **args) {
   const char *action = argc > 1 ? args[1] : "SHOW";
   bool show;

   if (!strcasecmp(action, "SHOW") || !strcasecmp(action, "OPEN")) {
      show = true;
   } else if (!strcasecmp(action, "HIDE") || !strcasecmp(action, "CLOSE")) {
      show = false;
   } else {
      ui_print(ui_active_window_name(), "Usage: /webcam [SHOW|HIDE]");

      return true;
   }

   if (!frontend_ops() || !frontend_ops()->webcam_show) {
      ui_print(ui_active_window_name(), "The webcam viewer is only available in the GTK client");

      return false;
   }

   frontend_ops()->webcam_show(show);

   return false;
}

///////////////////////////////////////////////
client_cmd_t client_cmds[] = {
   {
      .cmd = "disconnect", .help_section = "Connection", .cb = cmd_disconnect, .desc = "Disconnect selected server from status, or /disconnect <name>"
   },
   {
      .cmd = "help", .help_section = "Connection", .cb = cmd_help, .desc = "Show help message"
   },
   {
      .cmd = "quit", .help_section = "Connection", .cb = cmd_quit, .desc = "Exit (/quit [-yes|-y|y|yes] skips confirm)"
   },
   {
      .cmd = "server", .help_section = "Connection", .cb = cmd_server, .max_args = 3, .desc =
         "List configured/connected servers; connect: /server [-proxy socks5h://host[:port]] <name|URL>"
   },
   {
      .cmd = "j", .help_section = "Chat and rooms", .cb = cmd_join, .desc = "Alias for /join"
   },
   {
      .cmd = "join", .help_section = "Chat and rooms", .cb = cmd_join, .desc = "Join a channel"
   },
   {
      .cmd = "list", .help_section = "Chat and rooms", .cb = cmd_list, .desc = "List available rooms"
   },
   {
      .cmd = "me", .help_section = "Chat and rooms", .cb = cmd_me, .desc = "Send an action to the current channel"
   },
   {
      .cmd = "msg", .help_section = "Chat and rooms", .cb = cmd_msg, .max_args = 31, .desc = "Send a private message"
   },
   {
      .cmd = "names", .help_section = "Chat and rooms", .cb = cmd_names, .desc = "List users with privilege flags"
   },
   {
      .cmd = "part", .help_section = "Chat and rooms", .cb = cmd_part, .desc = "Leave a channel"
   },
   {
      .cmd = "query", .help_section = "Chat and rooms", .cb = cmd_query, .max_args = 1, .desc = "Open a private message tab"
   },
   {
      .cmd = "room", .help_section = "Chat and rooms", .cb = cmd_room, .max_args = 5, .desc =
         "/room list; add #room; remove #room [-f [-h]] [token]; #room vfo ..."
   },
   {
      .cmd = "topic", .help_section = "Chat and rooms", .cb = cmd_topic, .desc = "Get or set the current room topic"
   },
   {
      .cmd = "whois", .help_section = "Chat and rooms", .cb = cmd_whois, .desc = "Show client information"
   },
   {
      .cmd = "gps", .help_section = "Radio and discovery", .cb = cmd_gps, .max_args = 2,
      .desc = "GPS services: LIST | SUBSCRIBE <rig|station> | UNSUBSCRIBE <rig|station>"
   },
   {
      .cmd = "grid", .help_section = "Radio and discovery", .cb = cmd_grid, .max_args = 1, .desc = "Look up a grid square or coordinates"
   },
   {
      .cmd = "media", .help_section = "Media", .cb = cmd_media, .max_args = 2,
      .desc = "Media channels: LIST | SUBSCRIBE <name|uuid|#> | UNSUBSCRIBE <name|uuid|#>"
   },
   {
      .cmd = "webcam", .help_section = "Media", .max_args = 1, .cb = cmd_webcam,
      .desc = "Show or hide the webcam viewer"
   },
   {
      .cmd = "object", .help_section = "Radio and discovery", .cb = cmd_object, .desc = "Inspect objects: /object [rig0|rig0.A|uuid]"
   },
   {
      .cmd = "qrz", .help_section = "Radio and discovery", .cb = cmd_qrz, .max_args = 1, .desc = "Look up a callsign"
   },
   {
      .cmd = "rig", .help_section = "Radio and discovery", .cb = cmd_rig, .max_args = 1,
      .desc = "Radios and VFOs: LIST | SUBSCRIBE | UNSUBSCRIBE property updates"
   },
   {
      .cmd = "rxcodec", .help_section = "Media", .cb = cmd_rxcodec, .max_args = 3, .desc = "RX codecs: [LIST | <codec>|NONE [uuid|#number]]"
   },
   {
      .cmd = "rxvol", .help_section = "Media", .cb = cmd_rxvol, .desc = "Set receive volume level"
   },
   {
      .cmd = "txcodec", .help_section = "Media", .cb = cmd_txcodec, .max_args = 3, .desc = "TX codecs: [LIST | <codec>|NONE [uuid|#number]]"
   },
   {
      .cmd = "sercom", .help_section = "Serial", .cb = cmd_sercom, .max_args = 4,
      .desc = "Serial ports and attachments: LIST | REMOTE | ATTACH <name> <service> [device] | DISCONNECT <name>"
   },
   {
      .cmd = "clear", .help_section = "Client settings", .cb = cmd_clear, .desc = "Clear the scrollback"
   },
   {
      .cmd = "config", .help_section = "Client settings", .cb = cmd_config, .desc = "Focus the configuration tab"
   },
   {
      .cmd = "log", .help_section = "Client settings", .cb = cmd_log, .desc = "Switch to log tab"
   },
   {
      .cmd = "raw", .help_section = "Client settings", .cb = cmd_raw, .desc = "Send a raw command"
   },
   {
      .cmd = "reload", .help_section = "Client settings", .cb = cmd_reload, .desc = "Reload config file"
   },
   {
      .cmd = "save", .help_section = "Client settings", .cb = cmd_save, .max_args = 1, .desc = "Save config to ~/.config/rrclient.cfg"
   },
   {
      .cmd = "set", .help_section = "Client settings", .cb = cmd_set, .max_args = 31, .desc = "Set a typed configuration value"
   },
   {
      .cmd = "win", .help_section = "Client settings", .cb = cmd_win, .max_args = 2, .desc =
         "Switch: /win <number>; /win close [-force] parts/closes the current conversation; -force closes without waiting (status is protected)"
   },
   {
      .cmd = "admin", .help_section = "Administration", .cb = cmd_admin, .desc = "Focus the admin tab"
   },
   {
      .cmd = "die", .help_section = "Administration", .cb = cmd_die, .admin = true, .desc = "Shutdown the server"
   },
   {
      .cmd = "kick", .help_section = "Administration", .cb = cmd_kick, .admin = true, .desc = "Kick a user from the rig"
   },
   {
      .cmd = "mute", .help_section = "Administration", .cb = cmd_mute, .admin = true, .desc = "Mute a user"
   },
   {
      .cmd = "quota", .help_section = "Administration", .cb = cmd_quota, .max_args = 8, .admin = true, .desc = "TX/BW quota admin ([TX|BW] LIST|SHOW|ADD|RESET|SET)"
   },
   {
      .cmd = "rehash", .help_section = "Administration", .cb = cmd_rehash, .admin = true, .desc = "Ask server to reload config & users"
   },
   {
      .cmd = "restart", .help_section = "Administration", .cb = cmd_restart, .admin = true, .desc = "Restart the server"
   },
   {
      .cmd = "syslog", .help_section = "Administration", .cb = cmd_syslog, .admin = true, .desc = "Toggle server host log stream (/syslog on|off)"
   },
   {
      .cmd = "unmute", .help_section = "Administration", .cb = cmd_unmute, .admin = true, .desc = "Unmute a user"
   },
   {
      .cmd = "user", .help_section = "Administration", .cb = cmd_user, .max_args = 4,
      .desc = "PASS <your-user> <password>; admin/owner: LIST|ADD|REMOVE|LOCK|UNLOCK|PRIVS|OLDPW|RESETPW"
   },
   {
      .cmd = NULL, .cb = NULL, .desc = NULL
   }
};
////////////////////////////////////////////////



bool parse_chat_input_real(const char *msg) {
   /* Selecting a TUI conversation selects its server before any command. */
   (void)ui_active_window_name();

   if (!msg || !*msg) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%p> is empty", msg);

      return true;
   }

   if (msg[0] == '/') {

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
            !isspace( (unsigned char)*p) &&
            tolower( (unsigned char)*p) == tolower( (unsigned char)*c) ) {
            p++;
            c++;
         }

         if (!*c && (!*p || isspace( (unsigned char)*p) ) ) {
            cmd = &client_cmds[i];
            break;
         }
      }

      if (!cmd || !cmd->cb) {
         ui_print(ui_active_window_name(), "\00304*** Invalid command: /%s\017", mp);

         return true;
      }

      /*
       * Work on a writable copy since we're going to replace whitespace with NUL terminators.
       */
      char *input = strdup(mp);

      if (!input) {
         Log(LOG_CRIT, "chat.cmd", "Out of memory parsing command");

         return true;
      }

      /*
       * Keep this reasonably sized. cmd_argv is only used for the duration of the callback.
       */
      char *cmd_argv[32];
      int cmd_argc = 0;

      char *p = input;

      /* argv[0] is the command itself. */
      cmd_argv[cmd_argc++] = p;

      while (*p && !isspace( (unsigned char)*p) ) {
         p++;
      }

      if (*p) {
         *p++ = '\0';
      }

      /*
       * max_args is the number of arguments after argv[0]. Zero means one argument containing the remainder.
       */
      int max_args = cmd->max_args ? cmd->max_args : 1;

      while (*p && cmd_argc < (int)(sizeof(cmd_argv) / sizeof(cmd_argv[0]) ) ) {
         while (isspace( (unsigned char)*p) ) {
            p++;
         }

         if (!*p) {
            break;
         }

         cmd_argv[cmd_argc++] = p;

         /*
          * If this is the last allowed argument, leave the remainder of the string intact.
          */
         if (cmd_argc - 1 >= max_args) {
            break;
         }

         while (*p && !isspace( (unsigned char)*p) ) {
            p++;
         }

         if (*p) {
            *p++ = '\0';
         }
      }
      /*
       * When we break on the max_args limit the remainder is left intact, which can leave trailing whitespace on the last argument (e.g. tab-completed
       * '/whois admin '). Strip it so arguments are clean.
       */
      char *last = cmd_argv[cmd_argc - 1];
      char *end = last + strlen(last);

      while (end > last && isspace( (unsigned char)end[-1]) ) {
         end--;
      }
      *end = '\0';

      Log(LOG_CRAZY, "chat.cmd", "command=%s argc=%d max_args=%d", cmd_argv[0], cmd_argc, max_args);

      // Admin-only commands are rejected for non-staff users (and hidden
      // from /help); staff is set by the server from admin|owner privs.
      if (cmd->admin && !media_have_priv("admin|owner") ) {
         ui_print(ui_active_window_name(), "\00304Command /%s requires account admin or owner privilege\017", cmd_argv[0]);
         free(input);

         return false;
      }
      cmd->cb(cmd_argc, cmd_argv);
      free(input);
   } else {
      if (!ws_connected) {
         ui_print(ui_active_window_name(), "\00304*** Not connected to server ***\017");

         return false;
      }
      dict *d = dict_new();
      dict_add(d, "msg.type", "talk");
      dict_add(d, "talk.cmd", "msg");
      dict_add(d, "talk.data", msg);
      dict_add(d, "talk.msg_type", "pub");

      if (frontend_ops() ) {
         /* Frontend chat tabs represent rooms.  Include the selected tab's room so side-room messages are delivered there instead of defaulting to the
          * authoritative rig room on the server. */
         const char *room = frontend_ops()->chat_current_room();

         if (room && room[0]) {
            dict_add(d, "talk.target", room);

            if (room[0] != '#' && room[0] != '&') {
               dict_add(d, "talk.msg_type", "priv");
            }
         }
      } else if (ui_mode == UI_MODE_TUI) {
         /* TUI windows represent both rooms and private conversations.  The status window is the client log, so target the authoritative room when it is
          * active. */
         tui_window_t *window = tui_active_window();

         if (window && window->title[0] &&
            strcasecmp(window->title, "status") != 0) {
            dict_add(d, "talk.target", rrclient_window_room(window->title));

            if (window->title[0] != '#' && window->title[0] != '&') {
               dict_add(d, "talk.msg_type", "priv");
            }
         } else if (cfg_get_bool("tui.status-chat", false) ) {
            const char *room = ws_authoritative_room();

            if (room && *room) {
               dict_add(d, "talk.target", room);
            }
         } else {
            ui_print(ui_active_window_name(), "\00308Select a room tab before sending a message (status is for client logs and commands)\017");
            dict_free(d);

            return false;
         }
      }

      if (ws_conn) {
         ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
      }
      dict_free(d);
   }

   return false;
}
