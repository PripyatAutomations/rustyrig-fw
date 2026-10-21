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
#include <glib.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
#include <rrclient/connman.h>
#include <rrclient/rooms.h>
#include <rrclient/dcc.h>
#include <rrclient/config.layers.h>
#include <rrclient/userlist.h>
#include <rrclient/cmd.h>
#include <rrclient/media.h>
#include <rrclient/objects.h>
#include <rrclient/files.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/sercom.h>

extern bool dying;
extern time_t now;
extern rrconn_t *ws_conn;
extern dict *cfg;



bool cmd_reload(int argc, char **args) {
   rrclient_config_reload(config_file);

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
      .cmd = "quit", .help_section = "Connection", .cb = cmd_quit, .desc = "Exit: /quit [-yes|-y|y|yes] [message] (leading flag skips confirm)"
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
      .cmd = "users", .help_section = "Chat and rooms", .cb = cmd_users, .max_args = 0, .desc = "List online users and their owning servers"
   },
   {
      .cmd = "names", .help_section = "Chat and rooms", .cb = cmd_names, .desc = "List users with privilege flags"
   },
   {
      .cmd = "part", .help_section = "Chat and rooms", .cb = cmd_part, .max_args = 2, .desc = "Leave a channel or IRC room"
   },
   {
      .cmd = "nick", .help_section = "Chat and rooms", .cb = cmd_nick, .max_args = 1, .desc = "Change nickname on IRC"
   },
   {
      .cmd = "mode", .help_section = "Chat and rooms", .cb = cmd_mode, .max_args = 31, .desc = "Set IRC channel modes (not rig mode)"
   },
   {
      .cmd = "ban", .help_section = "Chat and rooms", .cb = cmd_ban, .max_args = 1, .desc = "Ban an IRC user from a room"
   },
   {
      .cmd = "query", .help_section = "Chat and rooms", .cb = cmd_query, .max_args = 2, .desc =
         "/query user [message]: open a private tab and optionally send text"
   },
   {
      .cmd = "quote", .help_section = "Chat and rooms", .cb = cmd_raw, .desc = "Send a raw IRC command (alias for /raw)"
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
      .cmd = "dcc", .help_section = "Chat and rooms", .cb = cmd_dcc, .max_args = 4,
      .desc = "DCC: AUDIO|CHAT|SEND; /dcc get ID [filename] receives into ~/Downloads/DCC/ by default; /dcc lists sessions"
   }, {
      .cmd = "media", .help_section = "Media", .cb = cmd_media, .max_args = 4,
      .desc = "Media channels: DIRECT [on|off|status] | LIST | SUBSCRIBE [-direct|-proxy] <name|uuid|#> | UNSUBSCRIBE <name|uuid|#>"
   },
   {
      .cmd = "exec", .help_section = "Client", .cb = cmd_exec, .desc = "Local shell: [-o send stdout|-e parse stdout] <command>; -stop [id|all] (default all)"
   },
   {
      .cmd = "webcam", .help_section = "Media", .max_args = 1, .cb = cmd_webcam,
      .desc = "Show or hide the webcam viewer"
   },
   {
      .cmd = "files", .help_section = "Radio and discovery", .cb = cmd_files, .desc = "Browse server shares and transfer files"
   },
   {
      .cmd = "object", .help_section = "Radio and discovery", .cb = cmd_object, .desc = "Resource tree; /object <rig0|rig0.A|uuid> inspects cached properties"
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
      .cmd = "receivers", .help_section = "Radio and discovery", .cb = cmd_receivers, .max_args = 2, .desc =
         "Receiver directory: [refresh | import <JSON> | connect <URL> | add <URL>]"
   },
   {
      .cmd = "rxroute", .help_section = "Media", .cb = cmd_rxroute, .max_args = 1, .desc = "Route room RX audio: L | R | S (both ears) | M"
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
      .cmd = "raw", .help_section = "Client settings", .cb = cmd_raw, .desc = "Send a raw IRC command (alias: /quote)"
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
         "Switch: /win <number> (GTK: current tab order); /win close [-f|-force] parts/closes the current conversation; -force closes without waiting (status and log tabs are protected; GTK tabs also offer a right-click menu)"
   },
   {
      .cmd = "admin", .help_section = "Administration", .cb = cmd_admin, .max_args = 2, .desc =
         "Focus admin tab; USERS | ROOMS | QUOTAS | AUDIT | PTT [before-id]"
   },
   {
      .cmd = "die", .help_section = "Administration", .cb = cmd_die, .admin = true, .desc = "Shutdown the server"
   },
   {
      .cmd = "kick", .help_section = "Administration", .cb = cmd_kick, .max_args = 2, .admin = true, .desc = "Kick a user from the current room/IRC channel"
   },
   {
      .cmd = "mute", .help_section = "Administration", .cb = cmd_mute, .max_args = 2, .admin = true, .desc = "Mute a user"
   },
   {
      .cmd = "quota", .help_section = "Administration", .cb = cmd_quota, .max_args = 8, .admin = true, .desc =
         "TX/BW quota admin ([TX|BW] LIST|SHOW|ADD|RESET|SET)"
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
      .cmd = "supervise", .help_section = "Radio", .cb = cmd_supervise, .max_args = 3,
      .desc = "ACCEPT|DECLINE <request-id> <rig-room>; accepting assumes control-operator responsibility"
   }, {
      .cmd = "link", .help_section = "Administration", .cb = cmd_link, .max_args = 2, .admin = true,
      .desc = "LIST | OPEN <stored-peer> | CLOSE <stored-peer>"
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



/* Client-side chat pacing, shared by native and IRC plain-text sends.
 * A bounded queue preserves the destination chosen when the user hit Enter. */
typedef struct {
   rrconn_t *connection;
   char *profile, *window;
   dict *message;
   bool irc;
} chat_pending_t;
static GQueue *chat_queue;
static guint chat_timer;
static double chat_tokens = 4.0;
static int chat_burst(void) {
   int burst = cfg_get_int("chat.send.burst", 4);
   return burst < 1 ? 1 : burst > 100 ? 100 : burst;
}
static int chat_interval(void) {
   int interval = cfg_get_int("chat.send.interval-ms", 2000);
   return interval < 100 ? 100 : interval > 60000 ? 60000 : interval;
}
static int chat_queue_limit(void) {
   int limit = cfg_get_int("chat.send.queue-limit", 64);
   return limit < 1 ? 1 : limit > 1000 ? 1000 : limit;
}
static gint64 chat_updated;

static gboolean chat_flush(gpointer unused) {
   (void)unused;
   gint64 stamp = g_get_monotonic_time();
   if (chat_updated && stamp >= chat_updated)
      chat_tokens = MIN((double)chat_burst(), chat_tokens + (stamp - chat_updated) / ((double)chat_interval() * 1000.0));
   chat_updated = stamp;
   chat_tokens = MIN(chat_tokens, (double)chat_burst());
   while (chat_queue && !g_queue_is_empty(chat_queue) && chat_tokens >= 1.0) {
      chat_pending_t *next = g_queue_pop_head(chat_queue);
      chat_tokens -= 1.0;
      if (next->connection && next->profile &&
          rrclient_connection_find(next->profile) == next->connection &&
          next->connection->authenticated) {
         if (next->irc)
            irc_send_dict(next->connection, next->message);
         else
            ws_send_dict(NULL, next->connection, next->message, WEBSOCKET_OP_TEXT);
      } else {
         ui_print(next->window, "Queued chat discarded: server disconnected.");
      }
      dict_free(next->message);
      g_free(next->profile);
      g_free(next->window);
      g_free(next);
   }
   if (!chat_queue || g_queue_is_empty(chat_queue)) {
      chat_timer = 0;
      return G_SOURCE_REMOVE;
   }
   return G_SOURCE_CONTINUE;
}
static bool chat_send_paced(rrconn_t *connection, dict *message, bool irc) {
   if (!connection || !message) return false;
   if (!chat_queue) chat_queue = g_queue_new();
   if (g_queue_get_length(chat_queue) >= chat_queue_limit()) {
      ui_print(ui_active_window_name(), "Chat send queue full (%d lines); message not sent.", chat_queue_limit());
      return false;
   }
   chat_pending_t *next = g_new0(chat_pending_t, 1);
   next->connection = connection;
   next->profile = g_strdup(rrclient_connection_name(connection));
   next->window = g_strdup(ui_active_window_name());
   next->message = message;
   next->irc = irc;
   g_queue_push_tail(chat_queue, next);
   chat_flush(NULL);
   if (!g_queue_is_empty(chat_queue) && !chat_timer)
      chat_timer = g_timeout_add(100, chat_flush, NULL);
   return true;
}
static bool parse_chat_input(const char *msg, bool commands) {
   /* Selecting a TUI conversation selects its server before any command. */
   (void)ui_active_window_name();

   rrconn_t *active_connection = rrclient_active_connection();

   if (!msg || !*msg) {
      Log(LOG_CRAZY, "chat.cmd", "parse_chat_input: msg:<%p> is empty", msg);

      return true;
   }

   if (rrclient_chat_input_blocked(commands ? msg : "text")) {
      ui_print(ui_active_window_name(), "Room is OFFLINE; message not sent");

      return true;
   }

   if (commands && msg[0] == '/') {

      if (!msg[1]) {
         return true;
      }

      const char *mp = msg + 1;
      client_cmd_t *cmd = NULL;

      /* Find the command. */
      for (int i = 0 ; client_cmds[i].cmd ; i++) {
         const char *p = mp;
         const char *c = client_cmds[i].cmd;

         while (*p && *c && !isspace((unsigned char)*p) && tolower((unsigned char)*p) == tolower((unsigned char)*c)) {
            p++;
            c++;
         }

         if (!*c && (!*p || isspace((unsigned char)*p))) {
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

      while (*p && !isspace((unsigned char)*p)) {
         p++;
      }

      if (*p) {
         *p++ = '\0';
      }

      /*
       * max_args is the number of arguments after argv[0]. Zero means one argument containing the remainder.
       */
      int max_args = cmd->max_args ? cmd->max_args : 1;

      while (*p && cmd_argc < (int)(sizeof(cmd_argv) / sizeof(cmd_argv[0]))) {
         while (isspace((unsigned char)*p)) {
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

         while (*p && !isspace((unsigned char)*p)) {
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

      while (end > last && isspace((unsigned char)end[-1])) {
         end--;
      }
      *end = '\0';

      Log(LOG_CRAZY, "chat.cmd", "command=%s argc=%d max_args=%d", cmd_argv[0], cmd_argc, max_args);

      // Admin-only commands are rejected for non-staff users (and hidden
      // from /help); staff is set by the server from admin|owner privs.
      if (cmd->admin && (!active_connection || active_connection->is_ws) && !media_have_priv("admin|owner")) {
         ui_print(ui_active_window_name(), "\00304Command /%s requires account admin or owner privilege\017", cmd_argv[0]);
         free(input);

         return false;
      }
      cmd->cb(cmd_argc, cmd_argv);
      free(input);
   } else {
      const char *active_room = rrclient_active_room();

      if (active_connection && !active_connection->is_ws) {
         if (!ws_connected || !active_connection->authenticated) {
            ui_print(ui_active_window_name(), "\00304Not connected to IRC server\017");

            return false;
         }

         if (!active_room || !*active_room) {
            ui_print(ui_active_window_name(), "\00308Select an IRC channel or query tab before sending a message\017");

            return false;
         }
         const char *target = active_room;

         if (target[0] != '#' && target[0] != '&') {
            target = rrclient_window_room(target);
         }
         dict *request = dict_new();

         if (!request) {
            return true;
         }
         dict_add(request, "msg.type", "talk");
         dict_add(request, "talk.cmd", "msg");
         dict_add(request, "talk.target", target);
         dict_add(request, "talk.msg_type", target[0] == '#' || target[0] == '&' ? "pub" : "priv");
         dict_add(request, "talk.data", msg);
         bool sent = chat_send_paced(active_connection, request, true);
         if (!sent) dict_free(request);

         return !sent;
      }

      if (!ws_connected) {
         ui_print(ui_active_window_name(), "\00304*** Not connected to server ***\017");

         return false;
      }
      dict *d = dict_new();
      dict_add(d, "msg.type", "talk");
      dict_add(d, "talk.cmd", "msg");
      dict_add(d, "talk.data", msg);
      dict_add(d, "talk.msg_type", "pub");

      /* PARITY: rustyrig-www/js/webui.chat.js chat_send_command uses the issuing room. */
      if (active_room && *active_room && strcasecmp(active_room, "status")) {
         dict_add(d, "talk.target", active_room);

         if (active_room[0] != '#' && active_room[0] != '&') {
            dict_add(d, "talk.msg_type", "priv");
         }
      } else if (ui_mode == UI_MODE_TUI) {
         if (cfg_get_bool("tui.status-chat", false)) {
            dict_add(d, "talk.target", ws_authoritative_room());
         } else {
            ui_print(ui_active_window_name(), "\00308Select a room tab before sending a message (status is for client logs and commands)\017");
            dict_free(d);

            return false;
         }
      }

      if (!chat_send_paced(ws_conn, d, false)) {
         dict_free(d);
      }
   }

   return false;
}

bool parse_chat_input_real(const char *msg) {
   return parse_chat_input(msg, true);
}
bool parse_chat_input_text(const char *msg) {
   return parse_chat_input(msg, false);
}
