//
// rrclient/ws.rigctl.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#define	__RRCLIENT	1
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
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "mod.ui.gtk3/gtk.freqentry.h"
#include "rrclient/ws.h"
#include "rrclient/userlist.h"
#include <librustyaxe/client-flags.h>

extern time_t poll_block_expire, poll_block_delay;
extern dict *cfg;		// config.c
extern bool server_ptt_state;
extern time_t now;
extern gulong freq_changed_handler_id;

// Store the previous mode
// XXX: this needs to go into the per-VFO
char old_mode[16];

bool ws_handle_rigctl_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      Log(LOG_DEBUG, "ws.rigctl", "handle_rigctl_msg invalid args: c:<%x> d:<%x>", c, d);
      return true;
   }

   time_t ts = dict_get_time_t(d, "cat.ts", now);

   if (dict_get(d, "cat.state.mode", NULL)) {
      if (poll_block_expire < now) {
         char *vfo = dict_get(d, "cat.state.vfo", NULL);
         double freq = dict_get_double(d, "cat.state.freq", 0.0);
         char *mode = dict_get(d, "cat.state.mode", NULL);
         double width = dict_get_double(d, "cat.state.width", 0.0);
         double power = dict_get_double(d, "cat.state.power", 0.0);

// XXX: PTT
         bool ptt = dict_get_bool(d, "cat.state.ptt", false);

         server_ptt_state = ptt;
//         g_signal_handlers_block_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);
//         g_signal_handlers_unblock_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);

         int ts = dict_get_int(d, "cat.ts", 0);
         char *user = dict_get(d, "cat.user", NULL);

         if (user && *user) {
//            Log(LOG_DEBUG, "ws.cat", "user:<%x> = |%s|", user, user);
            struct rr_user *cptr = NULL;
            if ((cptr = userlist_find(user))) {
               Log(LOG_DEBUG, "ws.cat", "ptt set to %s for cptr:<%x>", (cptr->is_ptt ? "true" : "false"), cptr);
               cptr->is_ptt = ptt;
            }
         }


         if (freq > 0) {
//            g_signal_handler_block(freq_entry, freq_changed_handler_id);
            GtkFreqEntry *fe = GTK_FREQ_ENTRY(freq_entry);  // cast

            if (!gtk_freq_entry_is_editing(fe)) {
               unsigned long old_freq = gtk_freq_entry_get_frequency(fe);
               gtk_freq_entry_set_frequency(fe, freq);
               float freq_f = (float)freq;
               if (freq != old_freq && freq_f > 0) {
                  Log(LOG_CRAZY, "ws", "Updating freq_entry: %.0f, old freq: %.0f", freq_f, old_freq);
               }
//               g_signal_handler_unblock(freq_entry, freq_changed_handler_id);
            }
         }

         if (mode && strlen(mode) > 0) {
            // XXX: We need to suppress sending a CAT message by disabling the changed signal on the mode combo

            if (strcasecmp(mode, "PKTUSB") == 0) {
               memset(mode, 0, 6);
               sprintf(mode, "D-U");
            } else if (strcasecmp(mode, "PKTLSB") == 0) {
               memset(mode, 0, 6);
               sprintf(mode, "D-L");
            }

            if (strcasecmp(old_mode, mode) == 0) {
               goto local_cleanup;
            }

            Log(LOG_CRAZY, "ws.rigctl", "Set MODE to %s", mode);
#if	0	// XXX: re-enable fm-mode
            if (strcasecmp(mode, "FM") == 0) {
               fm_dialog_show();
            } else {
               // Hide the FM dialog
               fm_dialog_hide();
            }
#endif
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);

            // save the old mode so we can compare next time
            memset(old_mode, 0, sizeof(old_mode));
            snprintf(old_mode, sizeof(old_mode), "%s", mode);
         }
      }
   } else {
      char *json_msg = dict2json(d);
      ui_print("[%s] ==> CAT: Unknown msg -- %s", get_chat_ts(ts), json_msg);
      Log(LOG_DEBUG, "ws.cat", "Unknown msg: %s", json_msg);
      free(json_msg);
   }

local_cleanup:
   return false;
}

bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt) {
   if (!c || !vfo) {
      return true;
   }

   const char *jp = dict2json_mkstr(
      VAL_STR, "cat.cmd", "ptt",
      VAL_STR, "cat.vfo", vfo,
      VAL_BOOL, "cat.ptt", ptt,
      VAL_ULONG, "cat.ts", now);

   Log(LOG_CRAZY, "ws.cat", "Sending: %s", jp);
   int ret = mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_ptt_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode) {
   if (!c || !vfo || !mode) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   const char *jp = dict2json_mkstr(
      VAL_STR, "cat.cmd", "mode",
      VAL_STR, "cat.vfo", vfo,
      VAL_STR, "cat.mode", mode,
      VAL_ULONG, "cat.ts", now);

   Log(LOG_CRAZY, "ws.cat", "Sending: %s", jp);
   int ret = mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_mode_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}

bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq) {
   if (!c || !vfo) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   const char *jp = dict2json_mkstr(
      VAL_STR, "cat.cmd", "freq",
      VAL_STR, "cat.vfo", vfo,
      VAL_FLOAT, "cat.freq", freq,
      VAL_ULONG, "cat.ts", now);
   Log(LOG_CRAZY, "ws.cat", "Sending: %s", jp);
   int ret = mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_mode_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}
