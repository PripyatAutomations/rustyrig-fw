#include <librrprotocol/irc.dcc.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
/* Native commands/devices consume the reusable IRC DCC session API. */
#include <rrclient/dcc.h>
#include <rrclient/cmd.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <librrprotocol/irc.audio.session.h>
#include <librrprotocol/rtp.h>
#include <string.h>
extern rrconn_t *ws_conn;
/* Only window routing is client state; the library owns every DCC session. */
static struct {
   rrconn_t *client;
   unsigned id;
   char window[512];
} dcc_windows[IRC_DCC_MAX_SESSIONS];
static const char *dcc_window(rrconn_t *client, unsigned id, bool bind_current) {
   unsigned empty = IRC_DCC_MAX_SESSIONS;

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      if (dcc_windows[i].id == id && dcc_windows[i].client == client) {
         if (bind_current) {
            snprintf(dcc_windows[i].window, sizeof(dcc_windows[i].window), "%s", ui_active_window_name() ? ui_active_window_name() : "STATUS");
         }

         return dcc_windows[i].window;
      }

      if (!dcc_windows[i].id && empty == IRC_DCC_MAX_SESSIONS) {
         empty = i;
      }
   }

   const char *active = ui_active_window_name();
   const char *window = active ? active : "STATUS";

   if (empty == IRC_DCC_MAX_SESSIONS) {
      return window;
   }
   dcc_windows[empty].client = client;
   dcc_windows[empty].id = id;
   snprintf(dcc_windows[empty].window, sizeof(dcc_windows[empty].window), "%s", window);

   return dcc_windows[empty].window;
}
static void offered(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   dict *notice = data ? json2dict(data) : NULL;

   if (!notice) {
      return;
   }
   const char *nick = dict_get(notice, "dcc.nick", "");
   const char *profile = rrclient_connection_name(client);
   ui_print(ui_active_window_name(), "DCC AUDIO %s offer from %s on %s; /dcc audio accept %s to connect", dict_get_bool(notice, "dcc.tx", false) ? "TX" : "RX",
      nick, profile ? profile : "IRC", nick);
   dict_free(notice);
}
static unsigned list_audio(void) {
   unsigned count = 0;
   irc_audio_session_t session;

   for (unsigned i = 0 ; irc_audio_session_get(i, &session) ; i++) {
      const char *profile = rrclient_connection_name(session.control);
      ui_print(ui_active_window_name(), "DCC AUDIO %s %s (%s, %s): %s [%s]", session.nick, session.tx ? "TX" : "RX", session.outgoing ? "outgoing" : "incoming",
         session.active ? "active" : session.accepted ? "waiting for peer" : "awaiting acceptance", session.endpoint, profile ? profile : "IRC");
      count++;
   }

   return count;
}
static bool cmd_audio(int argc, char **args) {
   if (argc < 1 || !args) {
      return true;
   }
   bool action = argc > 2 && (!strcasecmp(args[2], "accept") || !strcasecmp(args[2], "close"));
   int nick_arg = action ? 3 : 2;

   if (strcasecmp(args[1], "audio") || argc < nick_arg + 1 || argc > nick_arg + 2 ||
      (argc == nick_arg + 2 && strcasecmp(args[nick_arg + 1], "tx") && strcasecmp(args[nick_arg + 1], "rx"))) {
      ui_print(ui_active_window_name(), "Usage: /dcc | /dcc audio [accept|close] NICK [tx|rx]");

      return true;
   }

   if (!ws_conn || ws_conn->is_ws || !ws_conn->authenticated) {
      ui_print(ui_active_window_name(), "DCC voice requires a connected IRC server");

      return true;
   }
   unsigned directions = argc == nick_arg + 1 ? IRC_AUDIO_BOTH : !strcasecmp(args[nick_arg + 1], "tx") ? IRC_AUDIO_TX : IRC_AUDIO_RX;
   const char *nick = args[nick_arg];
   bool result;

   if (action && !strcasecmp(args[2], "accept")) {
      result = irc_audio_accept(ws_conn, nick, directions);
   } else if (action) {
      result = irc_audio_close(ws_conn, nick, directions) != 0;
   } else {
      irc_audio_options_t options = {
         .bind = dict_get(cfg, "dcc.audio.bind", "0.0.0.0"), .host = dict_get(cfg, "dcc.audio.host", ""),
         .port_min = cfg_get_int("dcc.audio.port-min", 10000), .port_max = cfg_get_int("dcc.audio.port-max", 10099),
         .encrypt = cfg_get_bool("dcc.audio.encrypt", false)
      };

      if (!rr_rtp_supported(options.encrypt)) {
         ui_print(ui_active_window_name(), "DCC RTP transport is unavailable in this build");

         return true;
      }
      result = irc_audio_offer(ws_conn, nick, directions, &options);
   }

   if (result) {
      ui_print(ui_active_window_name(), "DCC AUDIO %s: %s", nick, action ? args[2] : "offer sent");
   } else {
      ui_print(ui_active_window_name(), action ? "No usable DCC AUDIO session for %s" :
         "Unable to offer DCC AUDIO to %s; check the advertised IP and UDP port range", nick);
   }

   return !result;
}
static unsigned list_sessions(void) {
   unsigned count = list_audio();
   irc_dcc_session_t session;

   for (unsigned i = 0 ; irc_dcc_session_get(i, &session) ; i++) {
      const char *profile = rrclient_connection_name(session.control);
      const char *state = session.state == IRC_DCC_PENDING ? "awaiting acceptance" :
         session.state == IRC_DCC_LISTENING ? "waiting for peer" : session.state == IRC_DCC_CONNECTING ? "connecting" : "active";
      ui_print(ui_active_window_name(), "DCC #%u %s %s (%s, %s) %s: %llu/%s [%s]", session.id, session.type == IRC_DCC_CHAT ? "CHAT" : "SEND", session.nick,
         session.outgoing ? "outgoing" : "incoming", state, session.filename, (unsigned long long)session.transferred, session.size_known ? "known size" :
         "unknown size", profile ? profile : "IRC");

      if (session.type == IRC_DCC_SEND && session.size_known) {
         ui_print(ui_active_window_name(), "  Total: %llu bytes; acknowledged: %llu", (unsigned long long)session.size, (unsigned long long)session.acknowledged
            );
      }
      count++;
   }

   if (!count) {
      ui_print(ui_active_window_name(), "No active or pending DCC sessions");
   }

   return count;
}
static char *command_rest(int argc, char **args, int first) {
   size_t size = 1;

   for (int i = first ; i < argc ; i++) {
      size += strlen(args[i]) + 1;
   }

   char *result = malloc(size);

   if (!result) {
      return NULL;
   }
   result[0] = 0;

   for (int i = first ; i < argc ; i++) {
      if (i != first) {
         strcat(result, " ");
      }
      strcat(result, args[i]);
   }

   size_t length = strlen(result);

   if (length >= 2 && result[0] == '"' && result[length - 1] == '"') {
      memmove(result, result + 1, length - 2);
      result[length - 2] = 0;
   }

   return result;
}
static bool session_id(const char *text, unsigned *id) {
   if (!text || !*text) {
      return false;
   }

   if (*text == '#') {
      text++;
   }
   unsigned result = 0;

   for (const unsigned char *p = (const unsigned char *)text ; *p ; p++) {
      if (*p < '0' || *p > '9' || result > (UINT_MAX - (*p - '0')) / 10) {
         return false;
      }
      result = result * 10 + *p - '0';
   }

   *id = result;

   return result != 0;
}
static bool matching_session(unsigned id, irc_dcc_type_t type, bool pending) {
   irc_dcc_session_t session;

   for (unsigned i = 0 ; irc_dcc_session_get(i, &session) ; i++) {
      if (session.control == ws_conn && session.id == id && session.type == type && (!pending || session.state == IRC_DCC_PENDING)) {
         return true;
      }
   }

   return false;
}
bool cmd_dcc(int argc, char **args) {
   if (argc < 1 || !args) {
      return true;
   }

   if (argc == 1) {
      list_sessions();

      return false;
   }

   if (!strcasecmp(args[1], "audio")) {
      return cmd_audio(argc, args);
   }
   bool chat = !strcasecmp(args[1], "chat"), file = !strcasecmp(args[1], "send");

   if (!chat && !file) {
      goto usage;
   }

   if (!ws_conn || ws_conn->is_ws || !ws_conn->authenticated) {
      ui_print(ui_active_window_name(), "DCC requires a connected IRC server");

      return true;
   }

   if (argc < 3) {
      goto usage;
   }
   bool accepting = !strcasecmp(args[2], "accept"), closing = !strcasecmp(args[2], "close"), message = chat && !strcasecmp(args[2], "msg");
   irc_dcc_type_t type = chat ? IRC_DCC_CHAT : IRC_DCC_SEND;

   if (accepting || closing || message) {
      unsigned id;

      if (argc < 4 || !session_id(args[3], &id) || !matching_session(id, type, accepting)) {
         ui_print(ui_active_window_name(), "No matching %s session on this IRC connection", chat ? "CHAT" : "SEND");

         return true;
      }
      bool result = false;

      if (closing && argc == 4) {
         result = irc_dcc_close(ws_conn, id);
      } else if (message && argc >= 5) {
         char *text = command_rest(argc, args, 4);

         if (text) {
            result = irc_dcc_chat_send(ws_conn, id, text);

            if (result) {
               ui_print(ui_active_window_name(), "DCC #%u <you> %s", id, text);
            }
            free(text);
         }
      } else if (accepting && chat && argc == 4) {
         result = irc_dcc_accept(ws_conn, id, -1);
      } else if (accepting && file && argc >= 5) {
         char *input = command_rest(argc, args, 4), *path = input ? expand_path(input) : NULL;
         free(input);

         if (!path) {
            return true;
         }
#ifdef HOST_POSIX
         int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);

         if (fd < 0) {
            ui_print(ui_active_window_name(), "Cannot create %s; choose a new destination file", path);
         } else {
            result = irc_dcc_accept(ws_conn, id, fd);
            close(fd);

            if (!result) {
               unlink(path);
            }
         }
#endif
         free(path);
      } else {
         goto usage;
      }

      if (result && accepting) {
         (void)dcc_window(ws_conn, id, true);
      }

      if (!result) {
         ui_print(ui_active_window_name(), "DCC operation failed; the session may be inactive or its output queue busy");
      }

      return !result;
   }
   irc_dcc_options_t options = {
      .bind = dict_get(cfg, "dcc.bind", "0.0.0.0"), .host = dict_get(cfg, "dcc.host", ""),
      .port_min = cfg_get_int("dcc.port-min", 10000), .port_max = cfg_get_int("dcc.port-max", 10099)
   };
   unsigned id = 0;

   if (chat && argc == 3) {
      id = irc_dcc_offer_chat(ws_conn, args[2], &options);
   } else if (file && argc >= 4) {
      char *input = command_rest(argc, args, 3), *path = input ? expand_path(input) : NULL;
      free(input);

      if (!path) {
         return true;
      }
#ifdef HOST_POSIX
      int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);

      if (fd >= 0) {
         const char *name = strrchr(path, '/');
         id = irc_dcc_offer_file(ws_conn, args[2], name ? name + 1 : path, fd, &options);
         close(fd);
      }
#endif
      free(path);
   } else {
      goto usage;
   }

   if (!id) {
      ui_print(ui_active_window_name(), "Unable to offer DCC; check the file, advertised IP and TCP port range");
   }

   return id == 0;
usage:
   ui_print(ui_active_window_name(),
      "Usage: /dcc | /dcc audio [accept|close] NICK [tx|rx] | /dcc chat NICK | /dcc chat accept|close ID | /dcc chat msg ID TEXT | /dcc send NICK PATH | /dcc send accept ID PATH | /dcc send close ID");

   return true;
}
static void dcc_notice(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;
   dict *d = data ? json2dict(data) : NULL;

   if (!d) {
      return;
   }
   const char *status = dict_get(d, "dcc.status", ""), *type = dict_get(d, "dcc.type", "CHAT"), *nick = dict_get(d, "dcc.nick", "");
   unsigned id = dict_get_uint(d, "dcc.id", 0);
   const char *profile = rrclient_connection_name(client);
   const char *window = dcc_window(client, id, false);

   if (!strcmp(status, "offer")) {
      ui_print(window, "DCC #%u %s offer from %s on %s: %s (%llu bytes); /dcc %s accept %u%s", id, type, nick, profile ? profile : "IRC", dict_get(d,
         "dcc.filename", ""), dict_get_ullong(d, "dcc.size", 0), !strcmp(type, "SEND") ? "send" : "chat", id, !strcmp(type, "SEND") ? " DESTINATION" : "");
   } else {
      ui_print(window, "DCC #%u %s %s: %s %s (%llu bytes)", id, type, nick, status, dict_get(d, "dcc.text", ""), dict_get_ullong(d, "dcc.transferred", 0));
   }

   if (!strcmp(status, "closed") || !strcmp(status, "complete") || !strcmp(status, "error")) {
      for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
         if (dcc_windows[i].client == client && dcc_windows[i].id == id) {
            memset(&dcc_windows[i], 0, sizeof(dcc_windows[i]));
         }
      }
   }
   dict_free(d);
}
static void dcc_chat(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)client;
   (void)user;
   dict *d = data ? json2dict(data) : NULL;

   if (!d) {
      return;
   }
   const char *text = dict_get(d, "dcc.text", "");
   const char *window = dcc_window(client, dict_get_uint(d, "dcc.id", 0), false);

   if (!strncmp(text, "\001ACTION ", 8)) {
      char *action = strdup(text + 8);

      if (action) {
         size_t length = strlen(action);

         if (length && action[length - 1] == '\001') {
            action[length - 1] = 0;
         }
         ui_print(window, "DCC #%u * %s %s", dict_get_uint(d, "dcc.id", 0), dict_get(d, "dcc.nick", ""), action);
         free(action);
      }
   } else {
      ui_print(window, "DCC #%u <%s> %s", dict_get_uint(d, "dcc.id", 0), dict_get(d, "dcc.nick", ""), text);
   }
   dict_free(d);
}

struct dcc_output {
   const char *event, *data;
   rrconn_t *client;
   event_cb_t callback;
};
static void dcc_output_deliver(void *data) {
   struct dcc_output *output = data;
   output->callback(output->event, output->data, output->client, NULL);
}
static void dcc_output_event(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)user;
   struct dcc_output output = {
      .event = event, .data = data, .client = client,
      .callback = !strcmp(event, IRC_AUDIO_OFFER_EVENT) ? offered : !strcmp(event, IRC_DCC_CHAT_EVENT) ? dcc_chat : dcc_notice
   };
   rrclient_connection_dispatch(client, dcc_output_deliver, &output);
}
void rrclient_dcc_register_events(void) {
   irc_audio_sessions_init();
   irc_dcc_init();
   event_on(IRC_DCC_EVENT, dcc_output_event, NULL);
   event_on(IRC_DCC_CHAT_EVENT, dcc_output_event, NULL);
   event_on(IRC_AUDIO_OFFER_EVENT, dcc_output_event, NULL);
}
void rrclient_dcc_poll(void) {
   irc_audio_sessions_poll();
   irc_dcc_poll();
}
void rrclient_dcc_capture(const void *pcm, size_t length) {
   if (!cfg_get_bool("dcc.audio.mute", false)) {
      irc_audio_send_pcm(NULL, pcm, length);
   }
}
