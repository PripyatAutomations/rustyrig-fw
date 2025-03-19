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
extern struct GlobalState rig;	// Global state

bool ws_handle_chat_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   bool rv = true;		// return value
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
   char *target = mg_json_get_str(msg_data, "$.talk.target");
   char *user = cptr->user->name;

   if (cmd && data) {
      if (strcasecmp(cmd, "msg") == 0) {
         char msgbuf[HTTP_WS_MAX_MSG+1];
         char *escaped_msg = escape_html(data);

         if (escaped_msg == NULL) {
            Log(LOG_CRIT, "oom", "OOM in ws_handle_chat_msg!");
            return true;
         }
         struct mg_str mp;

         // sanity check
         if (user == NULL) {
            rv = true;
            goto cleanup;
         }

         memset(msgbuf, 0, sizeof(msgbuf));
         if (strcasecmp(user, "guest") == 0) {
            snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s%04d\", \"cmd\": \"msg\", \"data\": \"%s\", \"ts\": %lu } }", user, cptr->guest_id, escaped_msg, now);
         } else {
            snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\", \"ts\": %lu } }", user, escaped_msg, now);
         }
         mp = mg_str(msgbuf);
         free(escaped_msg);

         // Send to everyone, including the sender, which will then display it as SelfMsg
         ws_broadcast(NULL, &mp);
      } else if (strcasecmp(cmd, "kick") == 0) {
         Log(LOG_INFO, "chat", "Kick command received, processing...");
      } else if (strcasecmp(cmd, "mute") == 0) {
         Log(LOG_INFO, "chat", "Mute command received, processing...");
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (target == NULL) {
            // XXX: Send an warning to the user informing that they must specify a target username
            rv = true;
            goto cleanup;
         }

         // Deal with user database supplied data
         int tgt_uid = -1;
         int guest_id = -1;
         bool guest = false;
         http_client_t *tcptr = NULL;

         if (strncasecmp(target, "guest", 5) == 0) {
            // Deal with guest usernames (they don't actually existing in user table except as GUEST
            guest = true;
            guest_id = atoi(target + 5);
            // look up the account data, but note guests are 'special'
            tgt_uid = http_user_index("GUEST");
            Log(LOG_DEBUG, "chat", "WHOIS for %s%04d (%d) by %s", target, cptr->guest_id, tgt_uid, user);
            tcptr = http_find_client_by_guest_id(tgt_uid);
         } else {
            tgt_uid = http_user_index(target);
            Log(LOG_DEBUG, "chat", "WHOIS for %s (%d) by %s", target, tgt_uid, user);
            tcptr = http_find_client_by_name(target);
         }

         if (tgt_uid < 0 || tgt_uid > HTTP_MAX_USERS) {
            Log(LOG_DEBUG, "chat", "tgt_uid %d is invalid for user %s", tgt_uid, target);
            rv = true;
            goto cleanup;
         }

         if (tcptr == NULL) {
            // XXX: No such user error
            Log(LOG_DEBUG, "chat", "whois |%s| - tcptr == NULL", target);
            rv = true;
            goto cleanup;
         }

         // Form the message and send it
         if (tcptr->user == NULL) {
            // XXX: Send No Such User response
            Log(LOG_DEBUG, "chat", "whois |%s| - tcptr->user == NULL", target);
            rv = true;
            goto cleanup;
         }

         struct mg_str mp;
         char msgbuf[HTTP_WS_MAX_MSG+1];
         memset(msgbuf, 0, sizeof(msgbuf));

         if (guest) {
//            snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s%04d\", \"cmd\": \"whois\", \"data\": \"%s\", \"ts\": %lu } }", user, cptr->guest_id, escaped_msg, now);
         } else {
//            snprintf(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"whois\", \"data\": \"%s\", \"ts\": %lu } }", user, escaped_msg, now);
         }
         mp = mg_str(msgbuf);
      } else {
         Log(LOG_DEBUG, "chat", "Got unknown talk msg: |%.*s|", msg_data.len, msg_data.buf);
      }
   }

// Cleanup our mg_json_get_str returns from above, its easier to just do it down here
cleanup:
   free(token);
   free(cmd);
   free(data);
   free(target);
   return rv;
}
