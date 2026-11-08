// librrprotocol/ws.chat.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern time_t now;              // main.c

bool ws_handle_talk_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws.chat", "handle_talk_msg: cptr:<%p> d:<%p>", cptr, d);

      return false;
   }
   const char *cmd = dict_get(d, "talk.cmd", NULL);
   const char *user = dict_get(d, "talk.user", NULL);
   const char *privs = dict_get(d, "talk.privs", NULL);
   const char *muted = dict_get(d, "talk.muted", NULL);
   const char *ts = dict_get(d, "msg.ts", NULL);
   int sessions = dict_get_int(d, "talk.sessions", 1);
   bool rv = true;
   bool tx = dict_get_bool(d, "talk.state.tx", false);

   if (!cmd) {
      rv = true;

      return false;
   }

   if (cmd && strcasecmp(cmd, "userinfo") == 0) {
      if (!user) {
         rv = true;

         return false;
      }
      Log(LOG_DEBUG, "ws.talk", "UserInfo: %s has privs '%s' (TX: %s, Muted: %s, sessions: %d)", user, privs, (tx ? "true" : "false"), (muted ? "true" :
         "false"), sessions);
      event_emit_dict("userinfo", cptr, d);
   } else if (!strcasecmp(cmd, "supervise")) {
      event_emit_dict("supervise.result", cptr, d);
   } else if (!strcasecmp(cmd, "admin")) {
      event_emit_dict("admin.result", cptr, d);
   } else if (cmd && strcasecmp(cmd, "msg") == 0) {
      const char *from = dict_get(d, "talk.from", NULL);
      const char *data = dict_get(d, "talk.data", NULL);
      const char *msg_type = dict_get(d, "talk.msg_type", NULL);
      const char *target = dict_get(d, "talk.target", NULL);
      time_t ts = dict_get_time_t(d, "msg.ts", now);

      if (from) {
         event_emit_dict("talk.msg", cptr, d);
      }
   } else if (cmd && strcasecmp(cmd, "typing") == 0) {
      /* Cluster presence is a semantic event, not a chat transcript. */
      if (dict_get(d, "talk.from", NULL) && dict_get(d, "talk.room", NULL)) {
         event_emit_dict("talk.typing", cptr, d);
      }
   } else if (cmd && strcasecmp(cmd, "join") == 0) {
      if (!user) {
         return false;
      }
      event_emit_dict("join", cptr, d);
   } else if (cmd && strcasecmp(cmd, "part") == 0) {
      event_emit_dict("part", cptr, d);
   } else if (cmd && strcasecmp(cmd, "room-state") == 0) {
      event_emit_dict("room.state", cptr, d);
   } else if (cmd && strcasecmp(cmd, "room-list") == 0) {
      event_emit_dict("room.list", cptr, d);
   } else if (cmd && strcasecmp(cmd, "room-removed") == 0) {
      event_emit_dict("room.deleted", cptr, d);
   } else if (cmd && strcasecmp(cmd, "room-vfo-list") == 0) {
      event_emit_dict("room.vfo-list", cptr, d);
   } else if (cmd && strcasecmp(cmd, "room-vfo") == 0) {
      event_emit_dict("room.vfo", cptr, d);
   } else if (cmd && strcasecmp(cmd, "topic") == 0) {
      event_emit_dict("room.topic", cptr, d);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      if (!user) {
         return false;
      }
      char *quit_user = strdup(user);

      if (!quit_user) {
         return false;
      }
      Log(LOG_INFO, "ws.chat", "talk: sending quit for %s", quit_user);
      event_emit_dict("quit", cptr, d);
      free(quit_user);
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = dict_get(d, "talk.data", NULL);
      event_emit_dict("whois", cptr, d);
   } else if (cmd && (strcasecmp(cmd, "replay-start") == 0 || strcasecmp(cmd, "replay-complete") == 0)) {
      event_emit_dict("chat.replay", cptr, d);
   }

   return true;
}
