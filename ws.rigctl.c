#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "ws.h"
#include "ptt.h"

extern struct GlobalState rig;	// Global state

rr_vfo_t vfo_lookup(const char *vfo) {
   rr_vfo_t c_vfo;

   switch(vfo[0]) {
      case 'A':
         c_vfo = VFO_A;
         break;
      case 'B':
         c_vfo = VFO_B;
         break;
      case 'C':
         c_vfo = VFO_C;
         break;
      case 'D':
         c_vfo = VFO_D;
         break;
      case 'E':
         c_vfo = VFO_E;
         break;
      default:
         c_vfo = VFO_NONE;
         break;
   }

   return c_vfo;
}

bool ws_handle_rigctl_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   http_client_t *cptr = http_find_client_by_c(c);

   if (cptr == NULL) {
      Log(LOG_DEBUG, "rigctl", "rig parse, cptr == NULL, c: <%x>", c);
      return true;
   }
   cptr->last_heard = now;

   char *token = mg_json_get_str(msg_data, "$.rig.token");
   char *cmd = mg_json_get_str(msg_data, "$.rig.cmd");
   char *data = mg_json_get_str(msg_data, "$.rig.data");

   if (cmd && data) {
      if (strcasecmp(cmd, "ptt") == 0) {
         char *vfo = mg_json_get_str(msg_data, "$.rig.data.vfo");
         char *state = mg_json_get_str(msg_data, "$.rig.data.state");

         if (vfo == NULL || state == NULL) {
            Log(LOG_DEBUG, "rigctl", "PTT set without vfo or state");
            free(token);
            free(cmd);
            free(data);
            free(vfo);
            free(state);
            return true;
         }
         rr_vfo_t c_vfo;
         bool c_state;
         char msgbuf[HTTP_WS_MAX_MSG+1];
         struct mg_str mp;

         if (strcasecmp(state, "true") == 0) {
            c_state = true;
         } else {
            c_state = false;
         }

         if (vfo == NULL) {
            c_vfo = VFO_A;
         } else {
         }

         memset(msgbuf, 0, sizeof(msgbuf));
         snprintf(msgbuf, sizeof(msgbuf), "{ \"rigctl\": { \"user\": \"%s\", \"cmd\": \"ptt\", \"state\": \"%s\", \"vfo\": \"%s\", \"ts\": %lu } }", cptr->user->name, state, vfo, now);
         mp = mg_str(msgbuf);
         cptr->last_heard = now;
         ws_broadcast(c, &mp);
         rr_ptt_set(c_vfo, c_state);
      } else {
         Log(LOG_DEBUG, "rigctl", "Got unknown rig msg: |%.*s|", msg_data.len, msg_data.buf);
      }
   }
   free(token);
   free(cmd);
   free(data);
   return false;
}
