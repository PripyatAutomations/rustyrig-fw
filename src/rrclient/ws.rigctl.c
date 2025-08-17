//
// gtk-client/ws.rigctl.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include "common/config.h"
#define	__RRCLIENT	1
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
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/gtk.freqentry.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern time_t poll_block_expire, poll_block_delay;
extern void ui_show_whois_dialog(GtkWindow *parent, const char *json_array);
extern void fm_dialog_show(void);
extern void fm_dialog_hide(void);
extern dict *cfg;		// config.c
extern struct mg_mgr mgr;
extern bool ws_connected;
extern bool ws_tx_connected;
extern struct mg_connection *ws_conn, *ws_tx_conn;
extern bool server_ptt_state;
extern const char *tls_ca_path;
extern struct mg_str tls_ca_path_str;
extern bool cfg_show_pings;
extern time_t now;
extern char session_token[HTTP_TOKEN_LEN+1];
extern const char *get_chat_ts(void);
extern GtkWidget *main_window;
extern dict *servers;
extern gulong freq_changed_handler_id;

// Store the previous mode
// XXX: this needs to go into the per-VFO
char old_mode[16];

bool ws_handle_rigctl_msg(struct mg_connection *c, struct mg_ws_message *msg) {
   struct mg_str msg_data = msg->data;

   if (mg_json_get(msg_data, "$.cat", NULL) > 0) {
      if (poll_block_expire < now) {
         char *vfo = mg_json_get_str(msg_data, "$.cat.vfo");
         double freq;
         mg_json_get_num(msg_data, "$.cat.state.freq", &freq);
         char *mode = mg_json_get_str(msg_data, "$.cat.state.mode");
         double width;
         mg_json_get_num(msg_data, "$.cat.state.width", &width);
         double power;
         mg_json_get_num(msg_data, "$.cat.state.power", &power);

// XXX: PTT
         bool ptt = false;
         char *ptt_s = mg_json_get_str(msg_data, "$.cat.state.ptt");

         if (ptt_s && ptt_s[0] != '\0' && strcasecmp(ptt_s, "true") == 0) {
            ptt = true;
         }  else {
            ptt = false;
         }

         server_ptt_state = ptt;
//         g_signal_handlers_block_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);
         update_ptt_button_ui(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ptt_button), server_ptt_state);
//         g_signal_handlers_unblock_by_func(ptt_button, cast_func_to_gpointer(on_ptt_toggled), NULL);

         double ts;
         mg_json_get_num(msg_data, "$.cat.ts", &ts);
         char *user = mg_json_get_str(msg_data, "$.cat.user");

         if (user) {
//            Log(LOG_DEBUG, "ws.cat", "user:<%x> = |%s|", user, user);
            struct rr_user *cptr = NULL;
            if ((cptr = userlist_find(user))) {
               Log(LOG_DEBUG, "ws.cat", "ptt set to %s for cptr:<%x>", (cptr->is_ptt ? "true" : "false"), cptr);
               cptr->is_ptt = ptt;
            }
         }


         if (freq > 0) {
//            g_signal_handler_block(freq_entry, freq_changed_handler_id);
//            gtk_freq_entry_set_frequency(GTK_FREQ_ENTRY(freq_entry), freq);
            GtkFreqEntry *fe = GTK_FREQ_ENTRY(freq_entry);  // cast
            if (gtk_freq_entry_is_editing(fe)) {
//            if (!fe->editing) {
               unsigned long old_freq = gtk_freq_entry_get_frequency(fe);
               gtk_freq_entry_set_frequency(fe, freq);
//               g_signal_handler_unblock(freq_entry, freq_changed_handler_id);
               if (freq != old_freq) {
                  Log(LOG_CRAZY, "ws", "Updating freq_entry: %.0f, old freq: %.0f", freq, old_freq);
               }
            }
         }

         if (mode && strlen(mode) > 0) {
            // XXX: We need to suppress sending a CAT message by disabling the changed signal on the mode combo
            set_combo_box_text_active_by_string(GTK_COMBO_BOX_TEXT(mode_combo), mode);
            if (strcasecmp(mode, old_mode) != 0) {
               if (strcasecmp(mode, "FM") == 0) {
                  // Show the FM dialog
                  fm_dialog_show();
               } else {
                  // Hide the FM dialog
                  fm_dialog_hide();
               }
            }
            // save the old mode so we can compare next time
            memset(old_mode, 0, sizeof(old_mode));
            snprintf(old_mode, sizeof(old_mode), "%s", mode);
         }

         // free memory
         free(vfo);
         free(mode);
         free(user);
//      } else {
//         Log(LOG_CRAZY, "ws.cat", "Polling blocked for %d seconds", (poll_block_expire - now));
      }
   } else {
      ui_print("[%s] ==> CAT: Unknown msg -- %s", get_chat_ts(), msg_data);
      Log(LOG_DEBUG, "ws.cat", "Unknown msg: %s", msg->data);
   }
   return false;
}

bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt) {
   if (!c || !vfo) {
      return true;
   }

   char msgbuf[512];
   memset(msgbuf, 0, 512);
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"ptt\", \"vfo\": \"%s\", \"ptt\": \"%s\" } }",
                 vfo, (ptt ? "true" : "false"));

   Log(LOG_CRAZY, "ws.cat", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

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
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"mode\", \"vfo\": \"%s\", \"mode\": \"%s\" } }",
                 vfo, mode);
   Log(LOG_CRAZY, "ws.cat", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

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
   snprintf(msgbuf, 512, "{ \"cat\": { \"cmd\": \"freq\", \"vfo\": \"%s\", \"freq\": %.0f } }", vfo, freq);
   Log(LOG_CRAZY, "ws.cat", "Sending: %s", msgbuf);
   int ret = mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);

   if (ret < 0) {
      Log(LOG_DEBUG, "cat", "ws_send_mode_cmd: mg_ws_send error: %d", ret);
      return true;
   }
   return false;
}
