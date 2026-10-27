//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Native IRC event adapter, shared by GTK and TUI.
#include <rrclient/irc.stats.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/rooms.h>
#include <rrclient/userlist.h>
#include <rrclient/irc.whois.h>
#include <glib.h>

extern const char *login_user;
extern void rrclient_update_connection_ui(int connected);
extern void tui_refresh_sb_online(void);

bool irc_send_privmsg(rrconn_t *cptr, const char *window, int argc, char **args) {
   if (!cptr || !window || argc < 1 || !args) {
      return true;
   }
   char message[IRC_MSGLEN] = "";

   for (int i = 0 ; i < argc ; i++) {
      if ((i && strlcat(message, " ", sizeof(message)) >= sizeof(message)) || strlcat(message, args[i], sizeof(message)) >= sizeof(message)) {
         return true;
      }
   }

   return !irc_send(cptr, "PRIVMSG %s :%s", window, message);
}

static void rrclient_irc_connection(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)data;
   (void)user;

   if (!strcmp(event, "irc.disconnected") && cptr) {
      rrclient_irc_whois_forget(cptr);
   }

   if (!cptr || cptr != ws_conn) {
      return;
   }

   if (!strcmp(event, "irc.disconnected")) {
      ws_connected = 0;
      event_emit("disconnected", cptr, NULL);

      return;
   }
   char *name = strdup(cptr->nick);

   if (name) {
      free((void *)login_user);
      login_user = name;
   }
   ws_connected = 1;
   rrclient_update_connection_ui(1);
   tui_refresh_sb_online();

   if (frontend_ops()) {
      /* A conventional IRC server has no RustyRig PTT/media controls. */
      frontend_ops()->ptt_set_online(false);
   }
   ui_print(NULL, "%sConnected to IRC as %s", get_chat_ts(now), cptr->nick);
   const char *autojoin = cptr->server ? cptr->server->autojoin : "";
   char *copy = strdup(autojoin);

   if (copy) {
      char *save = NULL;

      for (char *room = strtok_r(copy, ", \t", &save) ; room ; room = strtok_r(NULL, ", \t", &save)) {
         char *key = strchr(room, ':');

         if (key) {
            *key++ = '\0';
            irc_send(cptr, "JOIN %s %s", room, key);
         } else {
            irc_send(cptr, "JOIN %s", room);
         }
      }

      free(copy);
   }
   /* Rejoin tabs retained by the common reconnect model. */
   rrclient_rooms_rejoin_available();
}

static void rrclient_irc_send_event(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)user;

   if (!data || !cptr) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   if (!strcmp(event, "irc.sent")) {
      /* IRC delivers messages addressed to our own nick back to us. Render that server copy once, rather than also adding a local echo. */
      if (!strcasecmp(dict_get(d, "talk.target", ""), cptr->nick)) {
         dict_free(d);

         return;
      }
      dict_add(d, "talk.from", cptr->nick);
      dict_add_long(d, "msg.ts", now);
      event_emit_dict("talk.msg", cptr, d);
   } else if (!strcmp(dict_get(d, "talk.cmd", ""), "msg")) {
      ui_print(ui_active_window_name(), "\00308Cannot send IRC message: invalid destination '%s'\017", dict_get(d, "talk.target", ""));
   } else {
      ui_print(ui_active_window_name(), "\00308This IRC server does not support the requested %s command (%s)\017", dict_get(d, "msg.type", ""), dict_get(d,
         "talk.cmd", ""));
   }
   dict_free(d);
}

/* A channel numeric belongs to an existing channel tab, not the server status log. Never create a channel window just because JOIN failed. */
static bool irc_is_channel(const char *name) {
   return name && *name && strchr("#&+!", name[0]) != NULL;
}

static const char *irc_numeric_channel_window(dict *message, int argc, rrconn_t *connection) {
   /* Most channel replies put the channel at arg2; NAMES (353), WHO (352), and a few extensions use later arguments. Never interpret free-form trailing text as
    * a channel. */
   for (int i = 2 ; i < argc && i <= 5 ; i++) {
      char key[20];
      snprintf(key, sizeof(key), "msg.arg%d", i);
      const char *value = dict_get(message, key, "");

      if (!irc_is_channel(value)) {
         continue;
      }
      const char *window = rrclient_window_find_for_connection(connection, value);

      if (window) {
         return window;
      }
   }

   return NULL;
}

/* Presentation belongs to rrclient, not the IRC transport. Keep this table independent of GLib/GTK so a future librustyirc stays reusable. */
typedef struct {
   const char *code;
   const char *label;
   bool subject;
} irc_numeric_label_t;

static const irc_numeric_label_t irc_numeric_labels[] = {
   {
      "002", "Your host", false
   }, {
      "003", "Server created", false
   },
   {
      "004", "Server software", false
   }, {
      "005", "Server capabilities", false
   },
   {
      "010", "Redirect", false
   }, {
      "020", "Server notice", false
   },
   {
      "200", "Connection trace", false
   }, {
      "201", "Connection trace", false
   },
   {
      "202", "Connection trace", false
   }, {
      "203", "Connection trace", false
   },
   {
      "204", "Connection trace", false
   }, {
      "205", "Connection trace", false
   },
   {
      "206", "Connection trace", false
   }, {
      "208", "Connection trace", false
   },
   {
      "211", "Connection statistics", false
   }, {
      "212", "Command statistics", false
   },
   {
      "213", "Configuration", false
   }, {
      "214", "Configuration", false
   },
   {
      "215", "Configuration", false
   }, {
      "216", "Configuration", false
   },
   {
      "217", "Configuration", false
   }, {
      "218", "Configuration", false
   },
   {
      "219", "End of statistics", false
   }, {
      "221", "Your user modes", false
   },
   {
      "234", "Service listing", false
   }, {
      "235", "End of services", false
   },
   {
      "241", "Server configuration", false
   }, {
      "242", "Server uptime", false
   },
   {
      "243", "Server configuration", false
   }, {
      "244", "Server configuration", false
   },
   {
      "249", "Server statistics", false
   }, {
      "250", "Connection statistics", false
   },
   {
      "251", "Users online", false
   }, {
      "252", "Operators online", false
   },
   {
      "253", "Unknown connections", false
   }, {
      "254", "Channels formed", false
   },
   {
      "255", "Network population", false
   }, {
      "256", "Administrative information", false
   },
   {
      "257", "Administrator", false
   }, {
      "258", "Administrator", false
   },
   {
      "259", "Administrator", false
   }, {
      "261", "Server log", false
   },
   {
      "262", "End of trace", false
   }, {
      "263", "Server busy", false
   },
   {
      "265", "Local users", false
   }, {
      "266", "Global users", false
   },
   {
      "281", "Services", false
   }, {
      "282", "End of services", false
   },
   {
      "300", "Away information", true
   }, {
      "302", "Userhost", false
   },
   {
      "303", "Online users", false
   }, {
      "305", "You are no longer away", false
   },
   {
      "306", "You are now away", false
   }, {
      "315", "End of WHO", true
   },
   {
      "321", "Channel list", false
   }, {
      "322", "Channel", true
   },
   {
      "323", "End of channel list", false
   }, {
      "324", "Channel modes", true
   },
   {
      "325", "Channel modes", true
   }, {
      "326", "Channel information", true
   },
   {
      "329", "Channel created", true
   }, {
      "331", "No topic set", true
   },
   {
      "332", "Topic", true
   }, {
      "333", "Topic set by", true
   },
   {
      "341", "Invitation sent", true
   }, {
      "346", "Invite exemption", true
   },
   {
      "347", "End of invite exemptions", true
   }, {
      "348", "Ban exemption", true
   },
   {
      "349", "End of ban exemptions", true
   }, {
      "351", "Server version", false
   },
   {
      "352", "WHO", true
   }, {
      "353", "Names", true
   },
   {
      "364", "Network links", false
   }, {
      "365", "End of links", false
   },
   {
      "366", "End of names", true
   }, {
      "367", "Channel ban", true
   },
   {
      "368", "End of channel bans", true
   }, {
      "369", "End of WHOWAS", true
   },
   {
      "371", "Information", false
   }, {
      "372", "MOTD", false
   },
   {
      "374", "End of information", false
   }, {
      "375", "MOTD begins", false
   },
   {
      "376", "End of MOTD", false
   }, {
      "381", "IRC operator privileges granted", false
   },
   {
      "382", "Rehash completed", false
   }, {
      "383", "Service information", false
   },
   {
      "391", "Server time", false
   }, {
      "392", "Users", false
   },
   {
      "393", "Users", false
   }, {
      "394", "End of users", false
   },
   {
      "395", "No users", false
   },
   {
      "401", "No such nick or channel", true
   }, {
      "402", "No such server", true
   },
   {
      "404", "Cannot send to channel", true
   }, {
      "406", "No such WHOWAS nick", true
   },
   {
      "407", "Too many targets", true
   }, {
      "408", "No such service", true
   },
   {
      "409", "No origin specified", false
   }, {
      "411", "No recipient", false
   },
   {
      "412", "No text to send", false
   }, {
      "413", "No top-level domain", false
   },
   {
      "414", "Wildcard in top-level domain", false
   }, {
      "421", "Unknown command", true
   },
   {
      "422", "MOTD unavailable", false
   }, {
      "423", "No administrative info", false
   },
   {
      "424", "File error", false
   }, {
      "431", "No nickname given", false
   },
   {
      "432", "Invalid nickname", true
   }, {
      "433", "Nickname in use", true
   },
   {
      "436", "Nickname collision", true
   }, {
      "437", "Nickname unavailable", true
   },
   {
      "441", "User not in channel", true
   }, {
      "442", "Not on channel", true
   },
   {
      "443", "User already on channel", true
   }, {
      "444", "User not logged in", true
   },
   {
      "445", "SUMMON disabled", false
   }, {
      "446", "USERS disabled", false
   },
   {
      "451", "Not registered", false
   }, {
      "461", "Missing parameters", true
   },
   {
      "462", "Already registered", false
   }, {
      "463", "Host not permitted", false
   },
   {
      "465", "Banned from server", false
   }, {
      "466", "Server ban", false
   },
   {
      "467", "Channel key already set", true
   }, {
      "472", "Unknown mode", true
   },
   {
      "478", "Channel ban list full", true
   }, {
      "481", "Permission denied", false
   },
   {
      "482", "Not channel operator", true
   }, {
      "483", "Server operator required", false
   },
   {
      "484", "Restricted connection", false
   }, {
      "485", "Cannot kill server", false
   },
   {
      "491", "No operator privileges", false
   }, {
      "501", "Unknown user mode", false
   },
   {
      "502", "Cannot change another user's modes", false
   },
   {
      "524", "Help unavailable", false
   }, {
      "525", "Invalid key", true
   },
   {
      "696", "Invalid mode parameter", true
   }, {
      "704", "Help", false
   },
   {
      "705", "Help", false
   }, {
      "706", "End of help", false
   }
};

static const irc_numeric_label_t *irc_numeric_label(const char *code) {
   for (size_t i = 0 ; i < sizeof(irc_numeric_labels) / sizeof(irc_numeric_labels[0]) ; i++) {
      if (!strcmp(irc_numeric_labels[i].code, code)) {
         return &irc_numeric_labels[i];
      }
   }

   return NULL;
}

/* Keep complete numeric parameters for daemon-specific WHOIS extensions and malformed/older replies, whose layouts cannot safely be assumed. */
static void rrclient_irc_numeric(dict *message, const char *cmd, int argc, rrconn_t *connection) {
   const char *nick = dict_get(message, "msg.arg2", "");
   const char *output = rrclient_irc_whois_window(connection, nick);
   const char *channel_window = irc_numeric_channel_window(message, argc, connection);

   if (channel_window) {
      output = channel_window;
   }
   const char *arg3 = dict_get(message, "msg.arg3", "");
   const char *arg4 = dict_get(message, "msg.arg4", "");

   if ((!strcmp(cmd, "405") || !strcmp(cmd, "403") || !strcmp(cmd, "471") ||
      !strcmp(cmd, "473") || !strcmp(cmd, "474") || !strcmp(cmd, "475") ||
      !strcmp(cmd, "476") || !strcmp(cmd, "477") || !strcmp(cmd, "489")) && argc >= 4 && irc_is_channel(nick)) {
      /* Rejected JOINs are reported in their existing tab, or status if no tab exists. Do not manufacture a dead channel tab. */
      ui_print(output, "%sCannot join %s: %s [%s]", get_chat_ts(now), nick, arg3, cmd);
   } else if (!strcmp(cmd, "332") && argc >= 4) {
      ui_print(output, "%sTopic for %s: %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "331") && argc >= 4) {
      ui_print(output, "%sNo topic is set for %s", get_chat_ts(now), nick);
   } else if (!strcmp(cmd, "333") && argc >= 5) {
      ui_print(output, "%sTopic for %s set by %s at %s", get_chat_ts(now), nick, arg3, arg4);
   } else if (!strcmp(cmd, "324") && argc >= 4) {
      ui_print(output, "%sModes for %s: %s %s", get_chat_ts(now), nick, arg3, arg4);
   } else if (!strcmp(cmd, "329") && argc >= 4) {
      ui_print(output, "%sChannel %s created at %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "353") && argc >= 5) {
      ui_print(output, "%sUsers in %s: %s", get_chat_ts(now), arg3, arg4);
   } else if (!strcmp(cmd, "366") && argc >= 4) {
      ui_print(output, "%sEnd of names for %s", get_chat_ts(now), nick);
   } else if (!strcmp(cmd, "367") && argc >= 4) {
      ui_print(output, "%sBan on %s: %s (set by %s)", get_chat_ts(now), nick, arg3, arg4);
   } else if (!strcmp(cmd, "368") && argc >= 4) {
      ui_print(output, "%sEnd of ban list for %s", get_chat_ts(now), nick);
   } else if (!strcmp(cmd, "305") && argc >= 3) {
      ui_print(output, "%sYou are no longer marked away", get_chat_ts(now));
   } else if (!strcmp(cmd, "306") && argc >= 3) {
      ui_print(output, "%sYou are now marked away", get_chat_ts(now));
   } else if (!strcmp(cmd, "433") && argc >= 4) {
      ui_print(output, "%sNickname %s is already in use: %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "313") && argc >= 4) {
      userlist_mark_oper(nick, true);
      ui_print(output, "%sWHOIS %s: %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "311") && argc >= 7) {
      userlist_mark_oper(nick, false);
      ui_print(output, "%sWHOIS %s!%s@%s (%s)", get_chat_ts(now), nick, arg3, arg4, dict_get(message, "msg.arg6", ""));
   } else if (!strcmp(cmd, "312") && argc >= 5) {
      ui_print(output, "%sWHOIS %s: server %s (%s)", get_chat_ts(now), nick, arg3, arg4);
   } else if (!strcmp(cmd, "319") && argc >= 4) {
      ui_print(output, "%sWHOIS %s: channels %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "317") && argc >= 4 && *arg3 && strspn(arg3, "0123456789") == strlen(arg3)) {
      /* Some older daemons omit signon time. Do not interpret their trailing explanatory text as an epoch timestamp. */
      if (argc >= 5 && *arg4 && strspn(arg4, "0123456789") == strlen(arg4)) {
         char signon[64];
         format_timestamp(dict_get_time_t(message, "msg.arg4", 0), signon, sizeof(signon));
         ui_print(output, "%sWHOIS %s: idle %s seconds; signed on %s (%s)", get_chat_ts(now), nick, arg3, signon, arg4);
      } else {
         ui_print(output, "%sWHOIS %s: idle %s seconds", get_chat_ts(now), nick, arg3);
      }
   } else if (!strcmp(cmd, "301") && argc >= 4) {
      ui_print(output, "%sWHOIS %s is away: %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "307") && argc >= 4) {
      ui_print(output, "%sWHOIS %s: registered nickname (%s)", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "330") && argc >= 5) {
      ui_print(output, "%sWHOIS %s: logged in as %s", get_chat_ts(now), nick, arg3);
   } else if ((!strcmp(cmd, "671") || !strcmp(cmd, "670") || !strcmp(cmd, "275")) && argc >= 4) {
      ui_print(output, "%sWHOIS %s: secure connection (%s)", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "276") && argc >= 4) {
      ui_print(output, "%sWHOIS %s: TLS certificate %s", get_chat_ts(now), nick, arg3);
   } else if ((!strcmp(cmd, "378") || !strcmp(cmd, "379") || !strcmp(cmd, "320") || !strcmp(cmd, "310") || !strcmp(cmd, "335")) && argc >= 4) {
      ui_print(output, "%sWHOIS %s: %s", get_chat_ts(now), nick, arg3);
   } else if (!strcmp(cmd, "318") && argc >= 3) {
      ui_print(output, "%sWHOIS %s: %s", get_chat_ts(now), nick, dict_get(message, "msg.arg3", "End of WHOIS"));
   } else {
      char parameters[IRC_MSGLEN] = "";

      /* Skip only the numeric and recipient. This retains the queried nick, account, address, TLS/cert info and every extension's other fields. */
      for (int i = 2 ; i < argc ; i++) {
         char key[32];
         snprintf(key, sizeof(key), "msg.arg%d", i);

         if (i > 2) {
            strlcat(parameters, " ", sizeof(parameters));
         }
         strlcat(parameters, dict_get(message, key, ""), sizeof(parameters));
      }

      const irc_numeric_label_t *label = irc_numeric_label(cmd);

      if (label) {
         /* Preserve every server-supplied parameter, even when the numeric has a daemon-specific layout. */
         ui_print(output, "%s%s [%s]: %s", get_chat_ts(now), label->label, cmd, parameters);
      } else {
         ui_print(output, "%s[%s] %s", get_chat_ts(now), cmd, parameters);
      }
   }
}

/* CTCP policy belongs to the client; the protocol library only emits queries. */
static void rrclient_irc_ctcp(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!data || !cptr || cptr->is_ws || cptr != ws_conn) {
      return;
   }
   dict *message = json2dict(data);

   if (!message) {
      return;
   }
   const char *prefix = dict_get(message, "msg.prefix", "");
   const char *text = dict_get(message, "msg.arg2", "");
   size_t length = strlen(text);
   char nick[NICKLEN + 1], payload[IRC_MSGLEN];
   size_t nicklen = strcspn(prefix, "!");

   if (strcasecmp(dict_get(message, "msg.cmd", ""), "PRIVMSG") ||
      !nicklen || nicklen > NICKLEN || length < 2 || length >= sizeof(payload) || text[0] != '\001' || text[length - 1] != '\001') {
      goto done;
   }
   memcpy(nick, prefix, nicklen);
   nick[nicklen] = '\0';

   if (strpbrk(nick, " ,:\r\n")) {
      goto done;
   }
   memcpy(payload, text + 1, length - 2);
   payload[length - 2] = '\0';

   if (strpbrk(payload, "\001\r\n")) {
      goto done;
   }
   char *params = strchr(payload, ' ');

   if (params) {
      *params++ = '\0';
   }
   const char *reply = NULL;
   char info[128];

   if (!strcasecmp(payload, "PING")) {
      /* Preserve the complete query, including its original parameters. */
      irc_send(cptr, "NOTICE %s :%s", nick, text);
   } else {
      if (!strcasecmp(payload, "VERSION") || !strcasecmp(payload, "FINGER")) {
         reply = "rustyrig rrclient " VERSION;
      } else if (!strcasecmp(payload, "CLIENTINFO")) {
         reply = "ACTION CLIENTINFO FINGER PING SOURCE TIME USERINFO VERSION";
      } else if (!strcasecmp(payload, "SOURCE")) {
         reply = "https://github.com/PripyatAutomations/rustyrig-fw";
      } else if (!strcasecmp(payload, "TIME")) {
         format_timestamp(now, info, sizeof(info));
         reply = info;
      } else if (!strcasecmp(payload, "USERINFO")) {
         snprintf(info, sizeof(info), "%s (rustyrig rrclient)", cptr->nick);
         reply = info;
      }

      if (reply) {
         irc_send(cptr, "NOTICE %s :\001%s %s\001", nick, payload, reply);
      }
   }
done:
   dict_free(message);
}

static void rrclient_irc_message(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!data || !cptr) {
      return;
   }
   dict *message = json2dict(data);
   dict *d = dict_new();

   if (!message || !d) {
      dict_free(message);
      dict_free(d);

      return;
   }
   const char *cmd = dict_get(message, "msg.cmd", "");
   const char *prefix = dict_get(message, "msg.prefix", "");
   char from[NICKLEN + 1];
   snprintf(from, sizeof(from), "%.*s", (int)strcspn(prefix, "!"), prefix);
   const char *arg1 = dict_get(message, "msg.arg1", "");
   const char *arg2 = dict_get(message, "msg.arg2", "");
   int argc = dict_get_int(message, "msg.argc", 0);
   dict_add_long(d, "msg.ts", now);
   dict_add(d, "talk.user", from);
   dict_add(d, "talk.from", from);
   /* IRC prefixes are nick!user@host. Preserve the full user@host for membership notices. */
   const char *bang = strchr(prefix, '!');
   const char *at = bang ? strchr(bang + 1, '@') : NULL;
   if (at && at > bang + 1 && at[1]) {
      char userhost[IRC_MSGLEN];
      snprintf(userhost, sizeof(userhost), "%s", bang + 1);
      dict_add(d, "talk.userhost", userhost);
   }
   dict_add(d, "talk.privs", "");

   if ((!strcasecmp(cmd, "PRIVMSG") || !strcasecmp(cmd, "NOTICE")) && argc >= 3) {
      bool channel = arg1[0] == '#' || arg1[0] == '&';

      dict_add(d, "talk.target", channel ? arg1 : from);
      dict_add_bool(d, "talk.query", !channel);
      const char *kind = !strcasecmp(cmd, "NOTICE") ? "notice" : channel ? "pub" : "priv";
      const char *text = arg2;
      char action[IRC_MSGLEN];
      size_t length = strlen(text);

      if (*text == '\001') {
         if (!strcasecmp(cmd, "NOTICE") && length >= 2 && length < IRC_MSGLEN && text[length - 1] == '\001') {
            ui_print(from, "%sCTCP reply from %s: %.*s", get_chat_ts(now), from, (int)(length - 2), text + 1);
            goto done;
         }

         if (length < 9 || strncmp(text, "\001ACTION ", 8) || text[length - 1] != '\001') {
            goto done;
         }
         memcpy(action, text + 8, length - 9);
         action[length - 9] = '\0';
         text = action;
         kind = "action";
      }

      if (!cptr->authenticated) {
         if (!strcmp(kind, "notice")) {
            ui_print(NULL, "%s-%s- %s", get_chat_ts(now), from, text);
         } else if (!strcmp(kind, "action")) {
            ui_print(NULL, "%s* %s %s", get_chat_ts(now), from, text);
         } else {
            ui_print(NULL, "%s<%s> %s", get_chat_ts(now), from, text);
         }
         goto done;
      }

      if (!channel && !strcasecmp(cmd, "NOTICE") && *from && !strchr(prefix, '!')) {
         rrclient_server_status_window(from);
      }
      dict_add(d, "talk.msg_type", kind);
      dict_add(d, "talk.data", text);
      event_emit_dict("talk.msg", cptr, d);
   } else if (!strcasecmp(cmd, "JOIN") && argc >= 2 && *from) {
      /* Ordinary IRC members have no prefix; retain their transport identity. */
      dict_add(d, "talk.privs", "irc:");
      dict_add(d, "talk.room", arg1);
      event_emit_dict("join", cptr, d);
      dict_add_int(d, "talk.sessions", 1);
      event_emit_dict("userinfo", cptr, d);
   } else if (!strcasecmp(cmd, "PART") && argc >= 2 && *from) {
      dict_add(d, "talk.room", arg1);
      const char *userhost = dict_get(d, "talk.userhost", "");
      ui_print(arg1, "%s\00304*\017 %s%s%s%s left %s%s%s", get_chat_ts(now), from,
         *userhost ? " (" : "", userhost, *userhost ? ")" : "", arg1,
         argc >= 3 && *arg2 ? ": " : "", argc >= 3 ? arg2 : "");
      event_emit_dict("part", cptr, d);
   } else if (!strcasecmp(cmd, "KICK") && argc >= 3) {
      dict_add(d, "talk.room", arg1);
      dict_add(d, "talk.user", arg2);
      const char *userhost = dict_get(d, "talk.userhost", "");
      ui_print(arg1, "%s* %s%s%s%s kicked %s: %s", get_chat_ts(now), from,
         *userhost ? " (" : "", userhost, *userhost ? ")" : "", arg2,
         dict_get(message, "msg.arg3", ""));
      event_emit_dict("part", cptr, d);
   } else if (!strcasecmp(cmd, "QUIT") && *from) {
      userlist_mark_oper(from, false);

      if (cptr != ws_conn) {
         goto done;
      }

      /* Room and roster views belong to the connection dispatching this event. */
      for (unsigned i = 0 ; rrclient_room_iter(i) ; i++) {
         const char *room = rrclient_room_iter(i);

         if (userlist_find_in_room(from, room)) {
            ui_print(room, "%s* %s quit: %s", get_chat_ts(now), from, arg1);
         }
      }

      while (userlist_remove_by_name_room(from, NULL)) {
         /* QUIT removes this nick from every joined channel. */
      }
   } else if (!strcasecmp(cmd, "NICK") && argc >= 2 && *from) {
      bool oper = userlist_is_oper(from);

      if (!strcasecmp(from, cptr->nick)) {
         snprintf(cptr->nick, sizeof(cptr->nick), "%s", arg1);
         char *name = strdup(arg1);

         if (name) {
            free((void *)login_user);
            login_user = name;
         }
      }

      for (unsigned i = 0 ; rrclient_room_iter(i) ; i++) {
         const char *room = rrclient_room_iter(i);
         struct rr_user *member = userlist_find_in_room(from, room);

         if (member) {
            dict_add(d, "talk.privs", member->privs);
            dict_add_bool(d, "talk.irc-oper", member->irc_oper);
            userlist_remove_by_name_room(from, room);
            dict_add(d, "talk.room", room);
            dict_add(d, "talk.user", arg1);
            event_emit_dict("userinfo", cptr, d);
         }
      }

      userlist_mark_oper(from, false);
      userlist_mark_oper(arg1, oper);
      ui_print(NULL, "%s* %s is now %s", get_chat_ts(now), from, arg1);
   } else if ((!strcasecmp(cmd, "TOPIC") && argc >= 3) || (!strcmp(cmd, "332") && argc >= 4)) {
      dict_add(d, "talk.room", !strcmp(cmd, "332") ? arg2 : arg1);
      dict_add(d, "talk.topic", !strcmp(cmd, "332") ? dict_get(message, "msg.arg3", "") : arg2);
      dict_add_bool(d, "talk.query", !strcmp(cmd, "332"));
      event_emit_dict("room.topic", cptr, d);
   } else if (!strcmp(cmd, "333") && argc >= 5) {
      const char *setter = dict_get(message, "msg.arg3", "unknown");
      ui_print(arg2, "%sTopic for %s set by %s", get_chat_ts(now), arg2, setter);
   } else if (!strcmp(cmd, "353") && argc >= 5) {
      dict_add(d, "talk.room", dict_get(message, "msg.arg3", ""));
      userlist_names_begin(dict_get(message, "msg.arg3", ""));
      char *names = strdup(dict_get(message, "msg.arg4", ""));

      if (names) {
         char *save = NULL;

         for (char *name = strtok_r(names, " ", &save) ; name ; name = strtok_r(NULL, " ", &save)) {
            char modes[40] = "irc:";
            size_t length = 4;
            char mode;
            while ((mode = irc_prefix_mode(cptr, *name))) {
               if (length + 1 < sizeof(modes)) {
                  modes[length++] = mode;
               }
               name++;
            }
            modes[length] = '\0';
            dict_add(d, "talk.privs", modes);
            dict_add(d, "talk.user", name);
            dict_add_int(d, "talk.sessions", 1);
            event_emit_dict("userinfo", cptr, d);
         }

         free(names);
      }
   } else if (!strcmp(cmd, "352") && argc >= 8) {
      userlist_mark_oper(dict_get(message, "msg.arg6", ""), strchr(dict_get(message, "msg.arg7", ""), '*') != NULL);
      rrclient_irc_numeric(message, cmd, argc, cptr);
   } else if (!strcasecmp(cmd, "MODE") && argc >= 3 && arg1[0] != '#' && arg1[0] != '&') {
      bool adding = true;

      for (const char *mode = arg2 ; *mode ; mode++) {
         if (*mode == '+') {
            adding = true;
         } else if (*mode == '-') {
            adding = false;
         } else if (*mode == 'o') {
            userlist_mark_oper(arg1, adding);
         }
      }
   } else if (!strcmp(cmd, "366") && argc >= 3) {
      userlist_names_end(arg2);
   } else if (!strcasecmp(cmd, "MODE") && argc >= 3 && (arg1[0] == '#' || arg1[0] == '&')) {
      GString *details = g_string_new(arg2);
      for (int i = 3; i < argc; ++i) {
         char key[32];
         snprintf(key, sizeof(key), "msg.arg%d", i);
         const char *value = dict_get(message, key, "");
         if (*value) g_string_append_printf(details, " %s", value);
      }
      ui_print(arg1, "%s\00310*\017 %s sets mode %s", get_chat_ts(now), from, details->str);
      g_string_free(details, TRUE);
      bool adding = true;
      int parameter = 3;

      for (const char *p = arg2 ; *p ; p++) {
         if (*p == '+' || *p == '-') {
            adding = *p == '+';
            continue;
         }

         if (!irc_mode_has_argument(cptr, *p, adding)) {
            continue;
         }

         if (parameter >= argc) {
            break;
         }
         char key[32];
         snprintf(key, sizeof(key), "msg.arg%d", parameter++);
         const char *nick = dict_get(message, key, "");
         char single[] = {
            *p, '\0'
         };
         struct rr_user *member = *irc_modes_symbol(cptr, single) ? userlist_find_in_room(nick, arg1) : NULL;

         if (!member) {
            continue;
         }
         char modes[200] = "irc:";
         size_t length = 4;

         for (const char *m = !strncmp(member->privs, "irc:", 4) ? member->privs + 4 : "" ; *m ; m++) {
            if (*m != *p && length + 2 < sizeof(modes)) {
               modes[length++] = *m;
            }
         }

         if (adding) {
            modes[length++] = *p;
         }
         modes[length] = '\0';
         dict_add(d, "talk.room", arg1);
         dict_add(d, "talk.user", nick);
         dict_add(d, "talk.privs", modes);
         dict_add_int(d, "talk.sessions", 1);
         event_emit_dict("userinfo", cptr, d);
      }
   } else if (strlen(cmd) == 3 && cmd[0] >= '0' && cmd[0] <= '9') {
      char key[32];
      snprintf(key, sizeof(key), "msg.arg%d", argc > 1 ? argc - 1 : 0);
      const char *text = dict_get(message, key, "");

      if (!strcmp(cmd, "464")) {
         dict_add(d, "auth.error", text);
         event_emit_dict("auth.error", cptr, d);
      } else if (strcmp(cmd, "001") && strcmp(cmd, "470")) {
         rrclient_irc_numeric(message, cmd, argc, cptr);

         if (!strcmp(cmd, "318") || !strcmp(cmd, "401") || !strcmp(cmd, "406")) {
            rrclient_irc_whois_complete(cptr, arg2);
         }
      }
   }
done:
   dict_free(d);
   dict_free(message);
}

static void rrclient_irc_error(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!data || !cptr || !rrclient_present_context()) {
      return;
   }
   dict *d = json2dict(data);

   if (d) {
      ui_print(NULL, "\00304IRC error: %s\017", dict_get(d, "error.msg", dict_get(d, "msg.arg1", "unknown error")));
      dict_free(d);
   }
}

static void rrclient_irc_redirect(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!data || !cptr || cptr != ws_conn || cptr->is_ws) {
      return;
   }
   dict *message = json2dict(data);

   if (!message) {
      return;
   }
   const char *old_room = dict_get(message, "msg.arg2", "");
   const char *new_room = dict_get(message, "msg.arg3", "");

   /* The numeric handler validates the channel pair. Forget the old reconnect intent without sending PART/JOIN; the server will confirm the new JOIN. */
   userlist_remove_room(old_room);
   rrclient_room_part(old_room);
   ui_close_window(old_room);

   if (frontend_ops() && frontend_ops()->chat_room_add) {
      frontend_ops()->chat_room_add(new_room);
   }
   ui_print(new_room, "%s* %s redirects %s to %s: %s", get_chat_ts(now), dict_get(message, "msg.prefix", "server"), old_room, new_room, dict_get(message,
      "msg.arg4", "Forwarding to another channel"));
   dict_free(message);
}

static void rrclient_irc_capabilities(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)user;

   if (cptr && rrclient_present_context() && frontend_ops()) {
      frontend_ops()->userlist_redraw();
   }
}

static void rrclient_irc_sasl(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)user;

   if (client == ws_conn && data && *data) {
      ui_print(NULL, "%s%s", get_chat_ts(now), data);
   }
}

void rrclient_irc_register_events(void) {
   rrclient_irc_stats_register();
   event_on("irc.sasl", rrclient_irc_sasl, NULL);
   event_on("irc.channel.redirect", rrclient_irc_redirect, NULL);
   event_on("irc.capabilities", rrclient_irc_capabilities, NULL);
   event_on("irc.ctcp", rrclient_irc_ctcp, NULL);
   event_on("irc.message", rrclient_irc_message, NULL);
   event_on("irc.connected", rrclient_irc_connection, NULL);
   event_on("irc.disconnected", rrclient_irc_connection, NULL);
   event_on("irc.error", rrclient_irc_error, NULL);
   event_on("irc.sent", rrclient_irc_send_event, NULL);
   event_on("irc.command.unsupported", rrclient_irc_send_event, NULL);
}
