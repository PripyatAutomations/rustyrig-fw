//
// ws.chat.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../../ext/libmongoose/mongoose.h"
#include "rrserver/auth.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/eeprom.h"
#include "common/logger.h"
#include "common/cat.h"
#include "common/posix.h"
#include "common/json.h"
#include "rrserver/http.h"
#include "rrserver/ws.h"
#include "rrserver/ptt.h"
#include "common/client-flags.h"
#define	CHAT_MIN_REASON_LEN	1

extern struct GlobalState rig;	// Global state

// Send an error message to the user, informing them they lack the appropriate privileges in chat
bool ws_chat_err_noprivs(http_client_t *cptr, const char *action) {
   if (!action || !cptr) {
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   Log(LOG_CRAZY, "core", "Unprivileged user %s (uid: %d with privs %s) requested to do %s and was denied", cptr->chatname, cptr->user->uid, cptr->user->privs, action);
   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"error\": { \"ts\": %lu, \"msg\": \"You do not have enough privileges to use '%s' command\" } }",
         now, action);
   mg_ws_send(cptr->conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   return false;
}

bool ws_chat_error_need_reason(http_client_t *cptr, const char *command) {
   if (!cptr || !command) {
      return true;
   }
   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf),
      "{ \"error\": { \"ts\": %lu, \"msg\": \"You MUST provide a reason for using'%s' command\" } }",
         now, command);
   mg_ws_send(cptr->conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
   return false;
}

///////////////////////////////
// DIE: Makes the server die //
///////////////////////////////
static bool ws_chat_cmd_die(http_client_t *cptr, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "die");
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF)) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG+1];
      prepare_msg(msgbuf, sizeof(msgbuf),
         "Shutting down due to /die \"%s\" from %s (uid: %d with privs %s)",
         (reason ? reason : "No reason given"),
         cptr->chatname, cptr->user->uid, cptr->user->privs);
      Log(LOG_AUDIT, "core", msgbuf);
      send_global_alert("***SERVER***", msgbuf);
      dying = 1;
   } else {
      ws_chat_err_noprivs(cptr, "DIE");
      return true;
   }
   return false;
}

//////////////////////////////////////
// RESTART: Make the server restart //
//////////////////////////////////////
static bool ws_chat_cmd_restart(http_client_t *cptr, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "RESTART");
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF)) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG+1];
      prepare_msg(msgbuf, sizeof(msgbuf),
         "Shutting down due to /restart from %s (uid: %d with privs %s): %s",
         cptr->chatname, cptr->user->uid, cptr->user->privs, reason);
      send_global_alert("***SERVER***", msgbuf);
      Log(LOG_AUDIT, "core", msgbuf);
      dying = 1;		// flag that this should be the last iteration
      restarting = 1;		// flag that we should restart after processing the alert
   } else {
      ws_chat_err_noprivs(cptr, "RESTART");
      return true;
   }
   return false;
}

///////////////////////
// KICK: Kick a user //
///////////////////////
static bool ws_chat_cmd_kick(http_client_t *cptr, const char *target, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!target) {
      // XXX: send an error response 'No target given'
      ws_send_error(cptr, "No target given for KICK");
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "kick");
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF)) {
      http_client_t *acptr;
      int kicked = 0;

      for (acptr = http_client_list; acptr; acptr = acptr->next) {
         // skip this one as it's not a valid chat client
         if (!acptr->active || !acptr->is_ws || acptr->chatname[0] == '\0') {
            continue;
         }

         if (strcmp(acptr->chatname, target) == 0) {
            // Build and send message
            char msgbuf[HTTP_WS_MAX_MSG+1];
            prepare_msg(msgbuf, sizeof(msgbuf),
               "kicked by %s (Reason: %s)",
               cptr->chatname,
               (reason ? reason : "No reason given"));
            Log(LOG_AUDIT, "admin.kick", "%s %s", acptr->chatname, msgbuf);
            struct mg_str ms = mg_str(msgbuf);
            ws_broadcast_with_flags(FLAG_STAFF, NULL, &ms, WEBSOCKET_OP_TEXT);
            ws_kick_client(acptr, msgbuf);
            kicked++;
         }
      }

      if (!kicked) {
         char msgbuf[HTTP_WS_MAX_MSG+1];
         prepare_msg(msgbuf, sizeof(msgbuf),
             "{ \"talk\": { \"error\": { \"ts\": %lu, \"msg\": \"KICK '%s' command matched no connected users\" } } }",
             now, target);
         mg_ws_send(cptr->conn, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      }
   } else {
      ws_chat_err_noprivs(cptr, "KICK");
      return true;
   }
   return false;
}

///////////////////////
// MUTE: Mute a user //
///////////////////////
static bool ws_chat_cmd_mute(http_client_t *cptr, const char *target, const char *reason) {
   if (!cptr || !cptr->user) {
      return true;
   }

   if (!target) {
      // XXX: send an error response 'No target given'
      ws_send_error(cptr, "No target given for MUTE");
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF)) {
      http_client_t *acptr = http_find_client_by_name(target);
      if (!acptr) {
         return true;
      }
      acptr->user->is_muted = true;

      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG+1];
      prepare_msg(msgbuf, sizeof(msgbuf), "%s MUTEd by %s: Reason: %s",
         target, cptr->chatname,
         (reason ? reason : "No reason given"));
      send_global_alert("***SERVER***", msgbuf);

      // broadcast the userinfo so cul updates
      ws_send_userinfo(acptr, NULL);
      Log(LOG_AUDIT, "admin.mute", "%s", msgbuf);

      // turn off PTT if this user holds it
      if (acptr->is_ptt) {
         rr_ptt_set_all_off();
         acptr->is_ptt = false;
      }
   } else {
      ws_chat_err_noprivs(cptr, "MUTE");
      return true;
   }
   return false;
}

///////////////////////////
// UNMUTE: Unmute a user //
///////////////////////////
static bool ws_chat_cmd_unmute(http_client_t *cptr, const char *target) {
   if (!cptr) {
      return true;
   }

   if (!target) {
      // XXX: send an error response 'No target given'
      ws_send_error(cptr, "No target given for UNMUTE");
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF)) {
      http_client_t *acptr = http_find_client_by_name(target);
      if (!acptr) {
         return true;
      }
      acptr->user->is_muted = false;

      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG+1];
      prepare_msg(msgbuf, sizeof(msgbuf), "%s UNMUTEd by %s",
         target, cptr->chatname);
      send_global_alert("***SERVER***", msgbuf);
      Log(LOG_AUDIT, "admin.unmute", msgbuf);
      // broadcast the userinfo so cul updates
      ws_send_userinfo(acptr, NULL);
   } else {
      ws_chat_err_noprivs(cptr, "UNMUTE");
      return true;
   }
   return false;
}

// Toggle syslog
static bool ws_chat_cmd_syslog(http_client_t *cptr, const char *state) {
   if (!cptr || !state) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) || client_has_flag(cptr, FLAG_SYSLOG)) {
      bool new_state = false;

      // Parse the state
      if ((strcasecmp(state, "on") == 0) ||
          (strcasecmp(state, "true") == 0) ||
          (strcasecmp(state, "yes") == 0)) {
         new_state = true;
         client_set_flag(cptr, FLAG_SYSLOG);
      } else if ((strcasecmp(state, "off") == 0) ||
                 (strcasecmp(state, "false") == 0) ||
                 (strcasecmp(state, "no") == 0)) {
         new_state = false;
         client_clear_flag(cptr, FLAG_SYSLOG);
      }
   } else {
      ws_chat_err_noprivs(cptr, "SYSLOG");
      return true;
   }
   return false;
}

// Send the updated userinfo for a single user; see ws_send_users below for everyone
bool ws_send_userinfo(http_client_t *cptr, http_client_t *acptr) {
   if (!cptr || !cptr->authenticated || !cptr->user) {
      return true;
   }

   const char *jp = dict2json_mkstr(
      VAL_INT, "talk.clones", cptr->user->clones,
      VAL_STR, "talk.cmd", "userinfo",
      VAL_BOOL, "talk.muted", cptr->user->is_muted,
      VAL_STR, "talk.privs", cptr->user->privs,
      VAL_STR, "talk.user", cptr->chatname,
      VAL_LONG, "talk.ts", now,
      VAL_BOOL, "talk.tx", cptr->is_ptt);

   struct mg_str mp = mg_str(jp);

   if (acptr) {
      ws_send_to_cptr(NULL, acptr, &mp, WEBSOCKET_OP_TEXT);
   } else {
      ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
   }
   free((char *)jp);
   return false;
}

// Send info on all online users to the user
bool ws_send_users(http_client_t *cptr) {
   if (!cptr) {
      return true;
   }
   http_client_t *current = http_client_list;
   
   // iterate over all the users
   while (current) {
      // should this be sent to a single user?
      if (cptr) {
         ws_send_userinfo(current, cptr);
      } else {           // nope, broadcast it
         ws_send_userinfo(current, NULL);
      }

      if (!current->next) {
         return false;
      }

      current = current->next;
   }
   return false;
}

// XXX: Once json2dict is implemented, we need to use it here
bool ws_handle_chat_msg(struct mg_ws_message *msg, struct mg_connection *c) {
   if (!msg || !c) {
      return true;
   }

   bool rv = true;
   struct mg_str msg_data = msg->data;
   http_client_t *cptr = http_find_client_by_c(c);

   if (!cptr) {
      Log(LOG_DEBUG, "chat", "talk parse, cptr == NULL, c: <%x>", c);
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   cptr->last_heard = now;

   char *token = mg_json_get_str(msg_data, "$.talk.token");
   char *cmd = mg_json_get_str(msg_data, "$.talk.cmd");
   char *data = mg_json_get_str(msg_data, "$.talk.data");
   char *target = mg_json_get_str(msg_data, "$.talk.args.target");
   char *reason = mg_json_get_str(msg_data, "$.talk.args.reason");
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
            Log(LOG_CRAZY, "chat", "user %s doesn't have chat privileges but tried to send a message", user);
            // XXX: Alert the user that their message was NOT deliverred because they aren't allowed to send it.
            rv = true;
            goto cleanup;
         }

         struct mg_str mp;
         char msgbuf[HTTP_WS_MAX_MSG+1];

         // sanity check
         if (!user) {
            rv = true;
            goto cleanup;
         }

         // handle a file chunk
         if (msg_type && strcmp(msg_type, "file_chunk") == 0) {
            char *filetype = mg_json_get_str(msg_data, "$.talk.filetype");
            char *filename = mg_json_get_str(msg_data, "$.talk.filename");
            prepare_msg(msgbuf, sizeof(msgbuf),
                        "{ \"talk\": { \"from\": \"%s\", \"cmd\": \"msg\", \"data\": \"%s\", "
                        "\"ts\": %lu, \"msg_type\": \"%s\", \"chunk_index\": %ld, "
                        "\"total_chunks\": %ld, \"filename\": \"%s\", \"filetype\": \"%s\" } }",
                        cptr->chatname, data, now, msg_type, chunk_index, total_chunks, filename, filetype);
            free(filetype);
            free(filename);
            mp = mg_str(msgbuf);
            // Send to everyone, including the sender, which will then display it as SelfMsg
            ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
         } else { // or just a chat message
            if (data[0] == '!') {
               char *input = data + 1;  // skip initial '!'
               char cmd[16], arg[32];
               size_t cmd_len = sizeof(cmd), arg_len = sizeof(arg);

               if (cptr->user->is_muted || !has_priv(cptr->user->uid, "admin|owner|tx|noob")) {
                  /// XXX: we should send an error alert
                  rv = true;
                  ws_chat_err_noprivs(cptr, "TALK");
                  goto cleanup;
               }

               while (*input) {
                  // skip spaces and !
                  while (isspace(*input) || (*input == '!')) {
                     input++;
                  }

                  // extract command
                  size_t i = 0;
                  while (*input && !isspace(*input) && i < cmd_len - 1) {
                     cmd[i++] = *input++;
                  }
                  cmd[i] = '\0';

                  while (isspace(*input)) {
                    input++;
                  }

                  // extract argument
                  i = 0;
                  while (*input && !isspace(*input) && i < arg_len - 1) {
                     arg[i++] = *input++;
                  }
                  arg[i] = '\0';

                  if (*cmd == '\0' || *arg == '\0') {
                     break;
                  }

                  bool freq_changed = false,
                       mode_changed = false,
                       power_changed = false,
                       width_changed = false;
                  rr_mode_t new_mode;
                  long new_freq;
                  float new_power;
                  char *new_width;

                  if (strcasecmp(cmd, "help") == 0) {
                     ws_send_notice(cptr->conn, "<span>***SERVER***"
                        "<br/>*** !help for VFO commands ***<br>"
                        "&nbsp;&nbsp;&nbsp;!freq <freq> - Set frequency to <freq> - can be 7200 7.2m 7200000 etc form<br/>"
                        "&nbsp;&nbsp;&nbsp;!mode <mode> - Set mode to CW|AM|LSB|USB|FM|DL|DU<br/>"
                        "&nbsp;&nbsp;&nbsp;!power <power> - Set power (NYI)<br/>"
                        "&nbsp;&nbsp;&nbsp;!vfo <vfo> - Switch VFOs (A|B|C)<br/>"
                        "&nbsp;&nbsp;&nbsp;!width <width> - Set passband width (narrow|normal|wide)<br/></span>");
                     goto cleanup;
                  } else if (strcasecmp(cmd, "freq") == 0) {
                     new_freq = parse_freq(arg);
                     Log(LOG_DEBUG, "ws.chat", "Got !freq %lu (%s) from %s", new_freq, arg, cptr->chatname);

                     if (new_freq >= 0) {
                        rr_freq_set(active_vfo, new_freq);
                        freq_changed = true;
                     } else {
                        ws_send_error(cptr, "Invalid freq %s provided for !freq", arg);
                     }
                     continue;
                  } else if (strcasecmp(cmd, "mode") == 0) {
                     new_mode = vfo_parse_mode(arg);

                     if (new_mode != MODE_NONE) {
                        mode_changed = true;
                        Log(LOG_DEBUG, "ws.chat", "Got !mode %s from %s", arg, cptr->chatname);
                        rr_set_mode(active_vfo, new_mode);
                     } else {
                        // Alert the client that the mode wasn't succesfully applied
                        ws_send_error(cptr, "Invalid mode %s provided for !mode", arg);
                     }
                     continue;
                  } else if (strcasecmp(cmd, "power") == 0) {
                     power_changed = true;
                     new_power = atof(arg);
                     Log(LOG_DEBUG, "ws.chat", "Got !power |%s| %.3f from %s", arg, new_power, cptr->chatname);
                     continue;
                  } else if (strcasecmp(cmd, "width") == 0) {
                     width_changed = true;
                     new_width = arg;
                     Log(LOG_DEBUG, "ws.chat", "Got !width %s from %s", arg, cptr->chatname);
                     rr_set_width(active_vfo, arg);
                     continue;
                  } else if (strcasecmp(cmd, "vfo") == 0) {
                     Log(LOG_DEBUG, "ws.chat", "Got !vfo %s from %s", arg, cptr->chatname);
                     continue;
                  } else {
                     Log(LOG_WARN, "ws.chat", "Unknown command: %s", cmd);
                     ws_send_error(cptr, "Ignoring unknown ! command: %s, try !help", cmd);
                     goto cleanup;
                  }

                  // handle sending an alert
                  if (freq_changed || mode_changed || power_changed || width_changed) {
                     char msgbuf[HTTP_WS_MAX_MSG+1];
                     prepare_msg(msgbuf, sizeof(msgbuf),
                        "{ \"notice\": { \"ts\": %lu, \"msg\": \"Set rig0\", \"from\": \"%s\" } }",
                           now, cptr->chatname);
                  } 

                  //
                  const char *jp = dict2json_mkstr(
                     VAL_FLOATP, "cat.state.freq", new_freq, 3,
                     VAL_STR, "cat.state.mode", new_mode,
                     VAL_FLOATP, "cat.state.power", new_power, 3,
                     VAL_STR, "cat.state.ptt", rig.backend->api->ptt_get(active_vfo) ? "true" : "false",
                     VAL_STR, "cat.user", cptr->chatname,
                     VAL_STR, "cat.state.vfo", active_vfo,
                     VAL_INT, "cat.state.width", new_width,
                     VAL_LONG, "cat.ts", now);

                  struct mg_str mp = mg_str(jp);
                  ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
                  free((char *)jp);

                  goto cleanup;
               }
            } else {			// just a message
               char *escaped_msg = escape_html(data);

               if (!escaped_msg) {
                  Log(LOG_CRIT, "oom", "OOM in ws_handle_chat_msg!");
                  rv = true;
                  goto cleanup;
               }

               const char *jp = dict2json_mkstr(
                  VAL_STR, "talk.cmd", "msg",
                  VAL_STR, "talk.data", escaped_msg,
                  VAL_STR, "talk.from", cptr->chatname,
                  VAL_STR, "talk.msg_type", msg_type,
                  VAL_LONG, "talk.ts", now);

               mp = mg_str(jp);

               // Send to everyone, including the sender, which will then display it as SelfMsg
               ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
               free((char *)jp);
               free(escaped_msg);
            }
         }
#if	0	// XXX: Not yet....
// XXX: These need moved into a struct array we can walk...
typedef struct talk_cmd {
   const char *cmd, bool (*cb)();
} talk_cmd_t;
talk_cmd_t talk_commands[] = {
   { "die",	ws_chat_cmd_die }
};
      } else {
#else
      } else if (strcasecmp(cmd, "die") == 0) {
         ws_chat_cmd_die(cptr, reason);
      } else if (strcasecmp(cmd, "kick") == 0) {
         ws_chat_cmd_kick(cptr, target, reason);
      } else if (strcasecmp(cmd, "mute") == 0) {
         ws_chat_cmd_mute(cptr, target, reason);
      } else if (strcasecmp(cmd, "names") == 0) {
         ws_send_users(cptr);
      } else if (strcasecmp(cmd, "restart") == 0) {
         ws_chat_cmd_restart(cptr, reason);
      } else if (strcasecmp(cmd, "syslog") == 0) {
         ws_chat_cmd_syslog(cptr, target);
      } else if (strcasecmp(cmd, "unmute") == 0) {
         ws_chat_cmd_unmute(cptr, target);
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (!target) {
            // XXX: Send a warning to the user informing that they must specify a target username
            Log(LOG_DEBUG, "chat", "whois with no target");
            rv = true;
            goto cleanup;
         }
         char msgbuf[HTTP_WS_MAX_MSG + 1];
         http_client_t *acptr = http_client_list;

         if (!acptr) {
            Log(LOG_DEBUG, "chat", "whois no users online?!?");
            rv = true;
            goto cleanup;
         }

         // create the full message
         memset(msgbuf, 0, sizeof(msgbuf));
         int clone_idx = 0;
         char whois_data[HTTP_WS_MAX_MSG / 2];
         char *wp = whois_data;
         size_t remaining = sizeof(whois_data);
         int written;

         written = snprintf(wp, remaining, "[");

         if (written < 0 || (size_t)written >= remaining) {
            goto trunc;
         }
         wp += written;
         remaining -= written;

         while (acptr) {
            if (strcasecmp(acptr->chatname, target) != 0) {
               acptr = acptr->next;
               continue;
            }

            if (!acptr->user) {
               acptr = acptr->next;
               continue;
            }

            http_user_t *up = acptr->user;

            // Add comma if not the first
            if (clone_idx > 0) {
               written = snprintf(wp, remaining, ",");
               if (written < 0 || (size_t)written >= remaining)
                  goto trunc;
               wp += written;
               remaining -= written;
            }

            written = snprintf(wp, remaining,
               "{ \"username\": \"%s\", \"clone\": %d, \"email\": \"%s\", \"privs\": \"%s\", \"connected\": %lu, \"last_heard\": %lu, \"ua\": \"%s\", \"muted\": \"%s\"  }",
               acptr->chatname, clone_idx++, up->email, up->privs,
               acptr->session_start, acptr->last_heard,
               acptr->user_agent ? acptr->user_agent : "Unknown",
               acptr->user->is_muted ? "true" : "false");

            if (written < 0 || (size_t)written >= remaining) {
               goto trunc;
            }
            wp += written;
            remaining -= written;

            acptr = acptr->next;
         }

         written = snprintf(wp, remaining, "]");
         if (written < 0 || (size_t)written >= remaining) {
            goto trunc;
         }
         wp += written;
         remaining -= written;

         // Send the full message
         snprintf(msgbuf, sizeof(msgbuf),
            "{ \"talk\": { \"cmd\": \"whois\", \"data\": %s, \"ts\": %lu } }",
            whois_data, now);
         Log(LOG_CRAZY, "ws.chat", "ws message whois: %s", msgbuf);
         mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
         goto cleanup;

trunc:
         Log(LOG_WARN, "chat", "whois_data truncated");
         ws_send_alert(cptr, "WHOIS data truncated to %i bytes", written);
         goto cleanup;
      }
#endif
   }

// Cleanup our mg_json_get_str returns from above, its easier to just do it down here
cleanup:
   free(token);
   free(cmd);
   free(data);
   free(target);
   free(reason);
   free(msg_type);
   return rv;
}
