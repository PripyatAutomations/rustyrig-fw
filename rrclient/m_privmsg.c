// Native IRC event adapter, shared by GTK and TUI.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/frontend.h>
#include <rrclient/rooms.h>
#include <rrclient/userlist.h>

extern const char *login_user;
extern void rrclient_update_connection_ui(int connected);
extern void tui_refresh_sb_online(void);

bool irc_send_privmsg(rrconn_t *cptr, const char *window, int argc, char **args) {
   if (!cptr || !window || argc < 1 || !args) {
      return true;
   }
   char message[IRC_MSGLEN] = "";

   for (int i = 0 ; i < argc ; i++) {
      if ((i && strlcat(message, " ", sizeof(message)) >= sizeof(message)) ||
         strlcat(message, args[i], sizeof(message)) >= sizeof(message)) {
         return true;
      }
   }

   return !irc_send(cptr, "PRIVMSG %s :%s", window, message);
}

static void rrclient_irc_connection(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)data;
   (void)user;

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
   ui_print(NULL, "%s Connected to IRC as %s", get_chat_ts(now), cptr->nick);
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

   if (!data || cptr != ws_conn) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }

   if (!strcmp(event, "irc.sent")) {
      dict_add(d, "talk.from", cptr->nick);
      dict_add_long(d, "msg.ts", now);
      event_emit_dict("talk.msg", cptr, d);
   } else {
      ui_print(ui_active_window_name(), "\00308This IRC server does not support the requested %s command (%s)\017", dict_get(d, "msg.type", ""), dict_get(d,
         "talk.cmd", ""));
   }
   dict_free(d);
}

static void rrclient_irc_message(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!data || !cptr || cptr != ws_conn) {
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
   dict_add(d, "talk.privs", "");

   if ((!strcasecmp(cmd, "PRIVMSG") || !strcasecmp(cmd, "NOTICE")) && argc >= 3) {
      bool channel = arg1[0] == '#' || arg1[0] == '&';
      dict_add(d, "talk.target", channel ? arg1 : from);
      const char *kind = !strcasecmp(cmd, "NOTICE") ? "notice" : channel ? "pub" : "priv";
      const char *text = arg2;
      char action[IRC_MSGLEN];
      size_t length = strlen(text);

      if (*text == '\001') {
         if (length < 9 || strncmp(text, "\001ACTION ", 8) || text[length - 1] != '\001') {
            goto done;
         }
         memcpy(action, text + 8, length - 9);
         action[length - 9] = '\0';
         text = action;
         kind = "action";
      }
      dict_add(d, "talk.msg_type", kind);
      dict_add(d, "talk.data", text);
      event_emit_dict("talk.msg", cptr, d);
   } else if (!strcasecmp(cmd, "JOIN") && argc >= 2 && *from) {
      dict_add(d, "talk.room", arg1);
      event_emit_dict("join", cptr, d);
      dict_add_int(d, "talk.sessions", 1);
      event_emit_dict("userinfo", cptr, d);
   } else if (!strcasecmp(cmd, "PART") && argc >= 2 && *from) {
      dict_add(d, "talk.room", arg1);
      event_emit_dict("part", cptr, d);
   } else if (!strcasecmp(cmd, "KICK") && argc >= 3) {
      dict_add(d, "talk.room", arg1);
      dict_add(d, "talk.user", arg2);
      event_emit_dict("part", cptr, d);
      ui_print(arg1, "%s * %s kicked %s: %s", get_chat_ts(now), from, arg2, dict_get(message, "msg.arg3", ""));
   } else if (!strcasecmp(cmd, "QUIT") && *from) {
      while (userlist_remove_by_name_room(from, NULL)) {
         /* QUIT removes this nick from every joined channel. */
      }
      ui_print(NULL, "%s * %s quit: %s", get_chat_ts(now), from, arg1);
   } else if (!strcasecmp(cmd, "NICK") && argc >= 2 && *from) {
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
            userlist_remove_by_name_room(from, room);
            dict_add(d, "talk.room", room);
            dict_add(d, "talk.user", arg1);
            event_emit_dict("userinfo", cptr, d);
         }
      }

      ui_print(NULL, "%s * %s is now %s", get_chat_ts(now), from, arg1);
   } else if ((!strcasecmp(cmd, "TOPIC") && argc >= 3) || (!strcmp(cmd, "332") && argc >= 4)) {
      dict_add(d, "talk.room", !strcmp(cmd, "332") ? arg2 : arg1);
      dict_add(d, "talk.topic", !strcmp(cmd, "332") ? dict_get(message, "msg.arg3", "") : arg2);
      event_emit_dict("room.topic", cptr, d);
   } else if (!strcmp(cmd, "353") && argc >= 5) {
      dict_add(d, "talk.room", dict_get(message, "msg.arg3", ""));
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
   } else if (!strcasecmp(cmd, "MODE") && argc >= 3 && (arg1[0] == '#' || arg1[0] == '&')) {
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
      } else if (strcmp(cmd, "001")) {
         ui_print(NULL, "%s [%s] %s", get_chat_ts(now), cmd, text);
      }
   }
done:
   dict_free(d);
   dict_free(message);
}

static void rrclient_irc_error(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)user;

   if (!data || cptr != ws_conn) {
      return;
   }
   dict *d = json2dict(data);

   if (d) {
      ui_print(NULL, "\00304IRC error: %s\017", dict_get(d, "error.msg", dict_get(d, "msg.arg1", "unknown error")));
      dict_free(d);
   }
}

static void rrclient_irc_capabilities(const char *event, const char *data, rrconn_t *cptr, void *user) {
   (void)event;
   (void)data;
   (void)user;

   if (cptr == ws_conn && rrclient_present_context() && frontend_ops()) {
      frontend_ops()->userlist_redraw();
   }
}

void rrclient_irc_register_events(void) {
   event_on("irc.capabilities", rrclient_irc_capabilities, NULL);
   event_on("irc.message", rrclient_irc_message, NULL);
   event_on("irc.connected", rrclient_irc_connection, NULL);
   event_on("irc.disconnected", rrclient_irc_connection, NULL);
   event_on("irc.error", rrclient_irc_error, NULL);
   event_on("irc.sent", rrclient_irc_send_event, NULL);
   event_on("irc.command.unsupported", rrclient_irc_send_event, NULL);
}
