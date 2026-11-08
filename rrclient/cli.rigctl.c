//
// rrclient/ws.rigctl.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
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

extern time_t poll_block_expire, poll_block_delay;
extern time_t now;

// Store the previous mode
// XXX: this needs to go into the per-VFO
char old_mode[16];

bool ws_handle_rigctl_cli_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws.rigctl", "handle_rigctl_msg invalid args: cptr:<%p> d:<%p>", cptr, d);

      return false;
   }
   time_t ts = dict_get_time_t(d, "msg.ts", now);

   if (dict_get(d, "cat.state.mode", NULL)) {
// XXX: Implement this - state message throttling & dict_diff usage
/*
 *     if (poll_block_expire < now) {
 *        return true;
 *     }
 *     poll_block_expire = now + poll_block_delay;
 */
      const char *vfo = dict_get(d, "cat.state.vfo", NULL);
      const char *mode = dict_get(d, "cat.state.mode", NULL);
      long freq = dict_get_long(d, "cat.state.freq", 0);
      int width = dict_get_int(d, "cat.state.width", 0);
      int power = dict_get_int(d, "cat.state.power", 0);
      bool ptt = dict_get_bool(d, "cat.state.ptt", false);
      time_t ts = dict_get_ulong(d, "msg.ts", 0);
      const char *user = dict_get(d, "cat.user", NULL);

      if (user && *user) {
         Log(LOG_DEBUG, "ws.cat", "user:<%p> = |%s|", user, user);

         char real_mode[32];
         memset(real_mode, 0, sizeof(real_mode));
         snprintf(real_mode, sizeof(real_mode), "%s", mode);

         if (real_mode && strlen(real_mode) > 0) {
            // XXX: We need to suppress sending a CAT message by disabling the
            // changed signal on the mode combo

            if (strcasecmp(old_mode, real_mode) == 0) {
               goto local_cleanup;
            }
// XXX: need to fix FM mode dialog crash ASAP
            Log(LOG_CRAZY, "ws.rigctl", "Set MODE to %s", real_mode);

/*
 *           if (strcasecmp(real_mode, "FM") == 0) {
 *  //               fm_dialog_show();
 *           } else {
 *              // Hide the FM dialog
 *  //               fm_dialog_hide();
 *           }
 */
            // save the old mode so we can compare next time
            memset(old_mode, 0, sizeof(old_mode));
            strlcpy(old_mode, real_mode, sizeof(old_mode));
         }
      }
   } else if (dict_get(d, "cat.cmd", NULL)) {
      // This is a command broadcast (ptt/freq/mode/width) echoed by the
      // server, possibly triggered by another user. On PTT we update the
      // sender's TX flag in the userlist (drives the red PTT button label
      // with their callsign) and let UI layers know.
      const char *cmd = dict_get(d, "cat.cmd", NULL);
      const char *cmd_user = dict_get(d, "cat.user", NULL);
      const char *cmd_vfo = dict_get(d, "cat.vfo", NULL);
      Log(LOG_CRAZY, "ws.cat", "Got cat.cmd %s from %s on vfo %s", (cmd ? cmd : "?"), (cmd_user ? cmd_user : "?"), (cmd_vfo ? cmd_vfo : "?"));

      if (cmd && strcasecmp(cmd, "ptt") == 0 && cmd_user && *cmd_user) {
         bool cmd_ptt = dict_get_bool(d, "cat.ptt", false);
         Log(LOG_DEBUG, "ws.cat", "%s %s transmitting", cmd_user, (cmd_ptt ? "started" : "stopped"));

         // Let any registered listeners know (rrclient UI, chat log, etc) --
         // librrprotocol itself tracks no client-side UI state.
         dict *tx_ev = dict_new();
         dict_add(tx_ev, "tx.user", (char *)cmd_user);
         dict_add_bool(tx_ev, "tx.state", cmd_ptt);
         event_emit_dict("ptt.tx", cptr, tx_ev);
         dict_free(tx_ev);
      }
   } else {
      char *json_msg = dict2json(d);
//      ui_print("[%s] ==> CAT: Unknown msg -- %s", get_chat_ts(ts), json_msg);
      Log(LOG_DEBUG, "ws.cat", "Unknown msg: %s", json_msg);
      free(json_msg);
   }

local_cleanup:

   return true;
}

bool ws_send_ptt_cmd_in_room(rrconn_t *cptr, const char *vfo, bool ptt, const char *room) {
   if (!cptr || !vfo) {
      return false;
   }
   dict *cat_msg = dict_new();

   if (!cat_msg) {
      return false;
   }
   dict_add(cat_msg, "msg.type", "cat");

   if (room && *room) {
      dict_add(cat_msg, "cat.room", room);
   }
   dict_add(cat_msg, "cat.cmd", "ptt");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add_bool(cat_msg, "cat.ptt", ptt);
   dict_add_ulong(cat_msg, "msg.ts", now);
   bool sent = ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return sent;
}

bool ws_send_mode_cmd_in_room(rrconn_t *cptr, const char *vfo, const char *mode, const char *room) {
   if (!cptr || !vfo || !mode) {
      return false;
   }
   dict *cat_msg = dict_new();

   if (!cat_msg) {
      return false;
   }
   dict_add(cat_msg, "msg.type", "cat");

   if (room && *room) {
      dict_add(cat_msg, "cat.room", room);
   }
   dict_add(cat_msg, "cat.cmd", "mode");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add(cat_msg, "cat.mode", mode);
   dict_add_ulong(cat_msg, "msg.ts", now);
   bool sent = ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return sent;
}

bool ws_send_width_cmd_in_room(rrconn_t *cptr, const char *vfo, const char *width, const char *room) {
   if (!cptr || !vfo || !width) {
      return false;
   }
   dict *cat_msg = dict_new();

   if (!cat_msg) {
      return false;
   }
   dict_add(cat_msg, "msg.type", "cat");

   if (room && *room) {
      dict_add(cat_msg, "cat.room", room);
   }
   dict_add(cat_msg, "cat.cmd", "width");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add(cat_msg, "cat.width", width);
   dict_add_ulong(cat_msg, "msg.ts", now);
   bool sent = ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return sent;
}

bool ws_send_freq_cmd_in_room(rrconn_t *cptr, const char *vfo, long freq, const char *room) {
   if (!cptr || !vfo) {
      return false;
   }
   dict *cat_msg = dict_new();

   if (!cat_msg) {
      return false;
   }
   dict_add(cat_msg, "msg.type", "cat");

   if (room && *room) {
      dict_add(cat_msg, "cat.room", room);
   }
   dict_add(cat_msg, "cat.cmd", "freq");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add_long(cat_msg, "cat.freq", freq);
   dict_add_ulong(cat_msg, "msg.ts", now);
   bool sent = ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return sent;
}

bool ws_send_ptt_cmd(rrconn_t *cptr, const char *vfo, bool ptt) {
   return ws_send_ptt_cmd_in_room(cptr, vfo, ptt, NULL);
}

bool ws_send_mode_cmd(rrconn_t *cptr, const char *vfo, const char *mode) {
   return ws_send_mode_cmd_in_room(cptr, vfo, mode, NULL);
}

bool ws_send_width_cmd(rrconn_t *cptr, const char *vfo, const char *width) {
   return ws_send_width_cmd_in_room(cptr, vfo, width, NULL);
}

bool ws_send_freq_cmd(rrconn_t *cptr, const char *vfo, long freq) {
   return ws_send_freq_cmd_in_room(cptr, vfo, freq, NULL);
}
