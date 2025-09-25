
// rrclient/ws.chat.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
//#include "mod.ui.gtk3/gtk.core.h"
//#include <rrclient/connman.h>
//#include <rrclient/userlist.h>
#include <librrprotocol/rrprotocol.h>

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

#if	0
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
         client_set_flag(&tmp, FLAG_ADMIN);
      } else if (has_privs(&tmp, "owner")) {
         client_set_flag(&tmp, FLAG_OWNER);
      }

      if (has_privs(&tmp, "muted")) {
         client_set_flag(&tmp, FLAG_MUTED);
         tmp.is_muted = true;
      }

      if (has_privs(&tmp, "ptt")) {
         client_set_flag(&tmp, FLAG_PTT);
      }

      if (has_privs(&tmp, "subscriber")) {
         client_set_flag(&tmp, FLAG_SUBSCRIBER);
      }

      if (has_privs(&tmp, "elmer")) {
         client_set_flag(&tmp, FLAG_ELMER);
      } else if (has_privs(&tmp, "noob")) {
         client_set_flag(&tmp, FLAG_NOOB);
      }

      if (has_privs(&tmp, "bot")) {
         client_set_flag(&tmp, FLAG_SERVERBOT);
      }

      if (has_privs(&tmp, "listener")) {
         client_set_flag(&tmp, FLAG_LISTENER);
      }

      if (has_privs(&tmp, "syslog")) {
         client_set_flag(&tmp, FLAG_SYSLOG);
      }

      if (has_privs(&tmp, "tx")) {
         client_set_flag(&tmp, FLAG_CAN_TX);
      }

      if (!userlist_add_or_update(&tmp)) {
         Log(LOG_CRIT, "ws", "OOM in ws_handle_talk_msg");
      } else {
         userlist_redraw_gtk();
      }
#endif
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

      // Support public messages and action (/me)
      if (msg_type && strcasecmp(msg_type, "pub") == 0) {
//         ui_print("[%s] <%s> %s", get_chat_ts(ts), from, data);
      } else if (msg_type && strcasecmp(msg_type, "action") == 0) {
//         ui_print("[%s] * %s %s", get_chat_ts(ts), from, data);
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
      
      struct rr_user *cptr = malloc(sizeof(struct rr_user));
      if (!cptr) {
         fprintf(stderr, "oom in ws_handle_chat_msg?!\n");
         goto cleanup;
      }
      memset(cptr, 0, sizeof(struct rr_user));
      snprintf(cptr->name, sizeof(cptr->name), "%s", user);
      Log(LOG_DEBUG, "ws.join", "New user %s has cptr:<%p>", user, cptr);
//      userlist_add_or_update(cptr);
//      ui_print("[%s] >>> %s connected to the radio <<<", get_chat_ts(ts), user);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      char *reason = dict_get(d, "talk.reason", NULL);
      if (!user || !reason) {
         goto cleanup;
      }

      time_t ts = dict_get_time_t(d, "talk.ts", now);
//      ui_print("[%s] >>> %s disconnected from the radio: %s (%.0f clones left)<<<", get_chat_ts(ts), user, reason ? reason : "No reason given", --clones);
#if	0
      struct rr_user *cptr = userlist_find(user);
      if (!cptr) {
         goto cleanup;
      }

      cptr->clones = clones;
      if (cptr->clones <= 0 ) {
         userlist_remove_by_name(cptr->name);
      }
#endif
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = dict_get(d, "talk.data", NULL);
//      ui_print("[%s] >>> WHOIS %s", user);
//      ui_print("[%s]   %s", whois_msg);
   }

cleanup:
   return false;
}
