#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "auth.h"
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "http.h"
#include "ws.h"
extern struct GlobalState rig;	// Global state

// Send an error message to the user, informing them they lack the appropriate privileges
bool ws_chat_err_noprivs(http_client_t *cptr, const char *action) {
   Log(LOG_CRAZY, "core", "Unprivileged user %s (uid: %d with privs %s) requested to do %s and was denied", cptr->chatname, cptr->user->uid, cptr->user->privs, action);
   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"talk\": { \"error\": { \"ts\": %lu, \"msg\": \"You do not have enough privileges to use '%s' command\" } } }",
         now, action);
   return false;
}

static bool ws_chat_cmd_die(http_client_t *cptr, const char *reason) {
   if (has_priv(cptr->user->uid, "admin|owner")) {
      // Send an ALERRT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG+1];
      prepare_msg(msgbuf, sizeof(msgbuf),
         "Shutting down due to /die \"%s\" from %s (uid: %d with privs %s)",
         (reason != NULL ? reason : "No reason given"),
         cptr->chatname, cptr->user->uid, cptr->user->privs);
      Log(LOG_AUDIT, "core", msgbuf);
      send_global_alert(cptr, "***SERVER***", msgbuf);
      dying = 1;
   } else {
      ws_chat_err_noprivs(cptr, "DIE");
      return true;
   }
   return false;
}

static bool ws_chat_cmd_restart(http_client_t *cptr, const char *reason) {
   if (has_priv(cptr->user->uid, "admin|owner")) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG+1];
      prepare_msg(msgbuf, sizeof(msgbuf),
         "Shutting down due to /restart from %s (uid: %d with privs %s)",
         cptr->chatname, cptr->user->uid, cptr->user->privs);
      send_global_alert(cptr, "***SERVER***", msgbuf);
      Log(LOG_AUDIT, "core", msgbuf);
      dying = 1;		// flag that this should be the last iteration
      restarting = 1;		// flag that we should restart after processing the alert
   } else {
      Log(LOG_AUDIT, "core", "Got /restart from %s (uid: %d with privs %s) who does not have appropriate privileges", cptr->chatname, cptr->user->uid, cptr->user->privs);
      ws_chat_err_noprivs(cptr, "RESTART");
      return true;
   }
   return false;
}

void ws_send_userinfo(http_client_t *c) {
   if (!c || !c->authenticated || !c->user)
      return;

   char buf[256];
   int len = mg_snprintf(buf, sizeof(buf),
      "{ \"talk\": { \"cmd\": \"userinfo\", \"user\": \"%s\", \"privs\": \"%s\", \"tx\": %s } }",
      c->chatname,
      c->user->privs,
      c->is_ptt ? "true" : "false");

   struct mg_str msg = mg_str_n(buf, len);
   ws_broadcast(NULL, &msg);
}

bool ws_handle_chat_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   if (msg == NULL || c == NULL) {
      return true;
   }

   bool rv = true;
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
   char *msg_type = mg_json_get_str(msg_data, "$.talk.msg_type");
   char *user = cptr->chatname;
   long chunk_index = mg_json_get_long(msg_data, "$.talk.chunk_index", 0);
   long total_chunks = mg_json_get_long(msg_data, "$.talk.total_chunks", 0);

   if (cmd) {
      if (strcasecmp(cmd, "msg") == 0) {
         if (!data) {
            Log(LOG_DEBUG, "chat", "got msg for cptr <%x> with no data: chatname: %s", cptr, user);
            rv = true;
            goto cleanup;
         }

         // If the message is empty, just return success
         if (strlen(data) == 0) {
            rv = false;
            goto cleanup;
         }

         if (!has_priv(cptr->user->uid, "admin|owner|chat")) {
            Log(LOG_INFO, "chat", "user %s doesn't have chat privileges but tried to send a message", user);
            // XXX: Alert the user that their message was NOT deliverred because they aren't allowed to send it.
            rv = true;
            goto cleanup;
         }

         struct mg_str mp;
         char msgbuf[HTTP_WS_MAX_MSG+1];

         // sanity check
         if (user == NULL) {
            rv = true;
            goto cleanup;
         }

         // handle a file chunk
         if (msg_type != NULL && strcmp(msg_type, "file_chunk") == 0) {
            prepare_msg(msgbuf, sizeof(msgbuf),
                        "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\", \"ts\": %lu, \"msg_type\": \"%s\", \"chunk_index\": %ld, \"total_chunks\": %ld } }",
                        cptr->chatname, data, now, msg_type, chunk_index, total_chunks);
         } else { // or just a chat message
            char *escaped_msg = escape_html(data);
            if (escaped_msg == NULL) {
               Log(LOG_CRIT, "oom", "OOM in ws_handle_chat_msg!");
               rv = true;
               goto cleanup;
            }
            prepare_msg(msgbuf, sizeof(msgbuf), "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\", \"ts\": %lu, \"msg_type\": \"%s\" } }",
                        cptr->chatname, escaped_msg, now, msg_type);
            free(escaped_msg);
         }
         mp = mg_str(msgbuf);

         // Send to everyone, including the sender, which will then display it as SelfMsg
         ws_broadcast(NULL, &mp);
      } else if (strcasecmp(cmd, "die") == 0) {
         ws_chat_cmd_die(cptr, data);
      } else if (strcasecmp(cmd, "kick") == 0) {
         Log(LOG_INFO, "chat", "Kick command received, processing...");
      } else if (strcasecmp(cmd, "mute") == 0) {
         Log(LOG_INFO, "chat", "Mute command received, processing...");
      } else if (strcasecmp(cmd, "restart") == 0) {
         ws_chat_cmd_restart(cptr, data);
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (target == NULL) {
            // XXX: Send an warning to the user informing that they must specify a target username
            Log(LOG_DEBUG, "chat", "whois with no target");
            rv = true;
            goto cleanup;
         }
         http_client_t *acptr = http_client_list;
         int clone_idx = 0;

         while (acptr != NULL) {
            // not a match? Carry on!
            if (strcasecmp(acptr->chatname, target) != 0) {
               acptr = acptr->next;
               continue;
            }

            char whois_data[HTTP_WS_MAX_MSG/2];
            char msgbuf[HTTP_WS_MAX_MSG+1];

            if (acptr == NULL) {
               // XXX: No such user error
               Log(LOG_DEBUG, "chat", "whois |%s| - acptr == NULL", target);
               rv = true;
               goto cleanup;
            }

            // Form the message and send it
            if (acptr->user == NULL) {
               // XXX: Send No Such User response
               Log(LOG_DEBUG, "chat", "whois |%s| - acptr->user == NULL", target);
               acptr = acptr->next;
               continue;
            }

            http_user_t *up = acptr->user;
            prepare_msg(whois_data, sizeof(whois_data), 
               "{ \"username\": \"%s\", \"clone\": %d, \"email\": \"%s\", \"privs\": \"%s\", \"connected\": %lu, \"last_heard\": %lu, \"ua\": \"%s\" }",
               acptr->chatname, clone_idx, up->email, up->privs, acptr->session_start,
               acptr->last_heard, (acptr->user_agent ? acptr->user_agent : "Unknown"));

            // prepare the response
            prepare_msg(msgbuf, sizeof(msgbuf),
               "{ \"talk\": { \"cmd\": \"whois\", \"data\": %s, \"ts\": %lu } }",
               whois_data, now);
            mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
            clone_idx++;		// keep track of which clone this is

            acptr = acptr->next;
            continue;
         }
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
   free(msg_type);
   return rv;
}
