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
#include "websocket.h"
extern struct GlobalState rig;	// Global state

bool ws_handle_chat_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   struct mg_str msg_data = msg->data;
   http_client_t *cptr = http_find_client_by_c(c);

   if (cptr == NULL) {
      Log(LOG_DEBUG, "chat", "talk parse, cptr == NULL, c: <%x>", c);
      return true;
   }
   cptr->last_heard = now;

   char *token = mg_json_get_str(msg_data, "$.talk.token");
   char *cmd = mg_json_get_str(msg_data, "$.talk.cmd");
   char *data = mg_json_get_str(msg_data, "$.talk.data");

   if (cmd && data) {
      if (strcmp(cmd, "msg") == 0) {
         char msgbuf[HTTP_WS_MAX_MSG+1];
         char *escaped_msg = escape_html(data);
         if (escaped_msg == NULL) {
            Log(LOG_CRIT, "oom", "OOM in ws_handle_chat_msg!");
            return true;
         }

         struct mg_str mp;
         memset(msgbuf, 0, sizeof(msgbuf));
         snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\", \"ts\": %lu } }", cptr->user->name, escaped_msg, now);
         mp = mg_str(msgbuf);
         free(escaped_msg);

         // Update the sender's last_heard time
         ws_broadcast(c, &mp);
      } else if (strcmp(cmd, "kick") == 0) {
         Log(LOG_INFO, "chat", "Kick command received, processing...");
      } else if (strcmp(cmd, "mute") == 0) {
         Log(LOG_INFO, "chat", "Mute command received, processing...");
      } else {
         Log(LOG_DEBUG, "chat", "Got unknown talk msg: |%.*s|", msg_data.len, msg_data.buf);
      }
   }
   free(token);
   free(cmd);
   free(data);
   return false;
}
