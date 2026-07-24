
// rrgtk/ws.chat.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
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

#if	defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"

extern dict *cfg;		// config.c
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

      Log(LOG_DEBUG, "ws.talk", "UserInfo: %s has privs '%s' (TX: %s, Muted: %s, clones: %.0f)", user, privs, (tx ? "true" : "false"), muted, clones);

      struct rr_user tmp = {0};

      snprintf(tmp.name, sizeof(tmp.name), "%s", user);
      snprintf(tmp.privs, sizeof(tmp.privs), "%s", privs);

      if (tx) {
         tmp.is_ptt = true;
      }

      if (muted && strcasecmp(muted, "true") == 0) {
         tmp.is_muted = true;
      }

      if (has_privs(&tmp, "admin")) {
         tmp.user_flags |= FLAG_ADMIN;
      } else if (has_privs(&tmp, "owner")) {
         tmp.user_flags |= FLAG_OWNER;
      }

      if (has_privs(&tmp, "muted")) {
         tmp.user_flags |= FLAG_MUTED;
         tmp.is_muted = true;
      }

      if (has_privs(&tmp, "ptt")) {
         tmp.user_flags |= FLAG_PTT;
      }

      if (has_privs(&tmp, "subscriber")) {
         tmp.user_flags |= FLAG_SUBSCRIBER;
      }

      if (has_privs(&tmp, "elmer")) {
         tmp.user_flags |= FLAG_ELMER;
      } else if (has_privs(&tmp, "noob")) {
         tmp.user_flags |= FLAG_NOOB;
      }

      if (has_privs(&tmp, "bot")) {
         tmp.user_flags |= FLAG_SERVERBOT;
      }

      if (has_privs(&tmp, "listener")) {
         tmp.user_flags |= FLAG_LISTENER;
      }

      if (has_privs(&tmp, "syslog")) {
         tmp.user_flags |= FLAG_SYSLOG;
      }

      if (has_privs(&tmp, "tx")) {
         tmp.user_flags |= FLAG_CAN_TX;
      }

      struct rr_user *ui_user = calloc(1, sizeof(*ui_user));
      if (!ui_user) {
         Log(LOG_CRIT, "ws", "OOM in ws_handle_talk_msg");
      } else {
         memcpy(ui_user, &tmp, sizeof(*ui_user));
         event_emit("http.userinfo", NULL, ui_user);
         free(ui_user);
      }
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
          struct talk_msg_event_data {
             char from[128];
             char data[4096];
             char target[128];
             char msg_type[32];
             time_t ts;
          } *tmed = calloc(1, sizeof(*tmed));

          if (tmed) {
             snprintf(tmed->from, sizeof(tmed->from), "%s", from);
             snprintf(tmed->data, sizeof(tmed->data), "%s", data);
             snprintf(tmed->target, sizeof(tmed->target), "%s", target ? target : "");
             snprintf(tmed->msg_type, sizeof(tmed->msg_type), "%s", msg_type ? msg_type : "pub");
             tmed->ts = ts;
             event_emit("talk.msg", NULL, tmed);
             free(tmed);
          }
       }
#if	0
      gui_window_t *win = gui_find_window(NULL, "main");
      GtkWidget *main_window = win->gtk_win;

      if (!gtk_window_is_active(GTK_WINDOW(main_window))) {
         gtk_window_set_urgency_hint(GTK_WINDOW(main_window), TRUE);
      }
#endif
   } else if (cmd && strcasecmp(cmd, "join") == 0) {
      char *ip = dict_get(d, "talk.ip", NULL);
      time_t ts = dict_get_time_t(d, "talk.ts", now);

      if (!user || !ip) {
         goto cleanup;
      }
      
      struct rr_user *cptr = calloc(1, sizeof(struct rr_user));
      if (!cptr) {
         fprintf(stderr, "oom in ws_handle_chat_msg?!\n");
         goto cleanup;
      }
      snprintf(cptr->name, sizeof(cptr->name), "%s", user);
      Log(LOG_DEBUG, "ws.join", "New user %s has cptr:<%p>", user, cptr);
      event_emit("http.userjoin", NULL, cptr);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      char *reason = dict_get(d, "talk.reason", NULL);
      if (!user || !reason) {
         goto cleanup;
      }

      time_t ts = dict_get_time_t(d, "talk.ts", now);
//      ui_print("[%s] >>> %s disconnected from the radio: %s (%.0f clones left)<<<", get_chat_ts(ts), user, reason ? reason : "No reason given", --clones);
      char *quit_user = strdup(user);
      if (!quit_user) {
         goto cleanup;
      }
      event_emit("http.userquit", NULL, quit_user);
      free(quit_user);
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = dict_get(d, "talk.data", NULL);
      if (whois_msg) {
         event_emit("http.whois", NULL, (void *)whois_msg);
      }
//      ui_print("[%s] >>> WHOIS %s", user);
//      ui_print("[%s]   %s", whois_msg);
   }

cleanup:
   return false;
}
#endif	// defined(USE_MONGOOSE)
