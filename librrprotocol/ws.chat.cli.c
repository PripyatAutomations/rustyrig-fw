
// rrclient/ws.chat.c
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

#if     defined(USE_MONGOOSE)
extern dict *cfg;                // config.c
extern time_t now;

bool ws_handle_talk_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      Log(LOG_DEBUG, "ws.chat", "handle_talk_msg: c:<%p> d:<%p>", c, d);

      return true;
   }
   char *cmd = dict_get(d, "talk.cmd", NULL);
   char *user = dict_get(d, "talk.user", NULL);
   char *privs = dict_get(d, "talk.privs", NULL);
   char *muted = dict_get(d, "talk.muted", NULL);
   char *ts = dict_get(d, "talk.ts", NULL);
   char *clones_s = dict_get(d, "talk.clones", NULL);
   double clones = 0;

   if (clones_s) {
      clones = atof(clones_s);
   }
   bool rv = false;
   bool tx = dict_get_bool(d, "talk.state.tx", false);

   if (!cmd) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "userinfo") == 0) {
      if (!user) {
         rv = true;
         goto cleanup;
      }
      Log(LOG_DEBUG, "ws.talk", "UserInfo: %s has privs '%s' (TX: %s, Muted: %s, clones: %.0f)", user, privs,
         (tx ? "true" : "false"), muted, clones);
      event_emit_dict("userinfo", NULL, d);
   } else if (cmd && strcasecmp(cmd, "msg") == 0) {
      char *from = dict_get(d, "talk.from", NULL);
      char *data = dict_get(d, "talk.data", NULL);
      char *msg_type = dict_get(d, "talk.msg_type", NULL);
      char *target = dict_get(d, "talk.target", NULL);
      time_t ts = dict_get_time_t(d, "talk.ts", now);

      if (strcasecmp(msg_type, "action") == 0) {
         Log(LOG_CRAZY, "ws.chat", "chat: %s * %s %s", target, from, data);
      } else {
         Log(LOG_CRAZY, "ws.chat", "chat: %s <%s> %s", target, from, data);
      }

      if (from && data) {
         event_emit_dict("talk.msg", NULL, d);
      }
   } else if (cmd && strcasecmp(cmd, "join") == 0) {
      char *ip = dict_get(d, "talk.ip", NULL);
      time_t ts = dict_get_time_t(d, "talk.ts", now);

      if (!user || !ip) {
         goto cleanup;
      }
      event_emit_dict("join", NULL, d);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      char *reason = dict_get(d, "talk.reason", NULL);

      if (!user || !reason) {
         goto cleanup;
      }
      time_t ts = dict_get_time_t(d, "talk.ts", now);
//      ui_print("[%s] >>> %s disconnected from the radio: %s (%.0f clones
// left)<<<", get_chat_ts(ts), user, reason ? reason : "No reason given",
// --clones);
      char *quit_user = strdup(user);

      if (!quit_user) {
         goto cleanup;
      }
      event_emit("quit", NULL, quit_user);
      free(quit_user);
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = dict_get(d, "talk.data", NULL);

      if (whois_msg) {
         event_emit("whois", NULL, (void *)whois_msg);
      }
//      ui_print("[%s] >>> WHOIS %s", user);
//      ui_print("[%s]   %s", whois_msg);
   }
cleanup:

   return false;
}
#endif // defined(USE_MONGOOSE)
