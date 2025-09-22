//
// ws.chat.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include <librustyaxe/core.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "../ext/libmongoose/mongoose.h"
#include <rrserver/database.h>
#include <rrserver/ptt.h>
#include <librrprotocol/rrprotocol.h>

// minimum reason length for kick/ban/etc
#define	CHAT_MIN_REASON_LEN	10
extern time_t now;
extern bool dying, restarting;
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
   prepare_msg(msgbuf, sizeof(msgbuf), "You do not have enough privileges to use '%s' command", now, action);
   const char *jp = dict2json_mkstr(
      VAL_STR, "error.msg", msgbuf,
      VAL_LONG, "error.ts", now);

   mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);

   return false;
}

bool ws_chat_error_need_reason(http_client_t *cptr, const char *command) {
   if (!cptr || !command) {
      return true;
   }
   char msgbuf[HTTP_WS_MAX_MSG+1];
   prepare_msg(msgbuf, sizeof(msgbuf), "You MUST provide a reason for using'%s' command", now, command);

   const char *jp = dict2json_mkstr(
      VAL_STR, "error.msg", msgbuf,
      VAL_LONG, "error.ts", now);

   mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free((char *)jp);
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
         prepare_msg(msgbuf, sizeof(msgbuf), "KICK '%s' command matched no connected users", now, target);

         const char *jp = dict2json_mkstr(
            VAL_STR, "error.msg", msgbuf,
            VAL_LONG, "error.ts", now);

         mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
         free((char *)jp);
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

      // turn off PTT if this user holds it
      if (acptr->is_ptt) {
//         rr_ptt_set_all_off();
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

      new_state = parse_bool(state);
      if (new_state) {
         client_set_flag(cptr, FLAG_SYSLOG);
      } else {
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
bool ws_handle_chat_msg(struct mg_connection *c, dict *d) {
   if (!c || !d) {
      return true;
   }

   http_client_t *cptr = http_find_client_by_c(c);

   if (!cptr) {
      Log(LOG_DEBUG, "chat", "talk parse, cptr is NULL, c: <%x>", c);
      return true;
   }

   if (!cptr->user) {
      Log(LOG_WARN, "chat", "talk parse, cptr:<%x> ->user NULL", cptr);
      return true;
   }

   // XXX: remove this asap
   char *json_data = dict2json(d);
   Log(LOG_CRAZY, "chat", "handle chat msg: RX from cptr:<%x> (%s) => json: %.*s", cptr, cptr->chatname, json_data);
   free(json_data);

   cptr->last_heard = now;

   char *token = dict_get(d, "talk.token", NULL);
   char *cmd = dict_get(d, "talk.cmd", NULL);
   char *data = dict_get(d, "talk.data", NULL);
   char *target = dict_get(d, "talk.target", NULL);
   char *reason = dict_get(d, "talk.args.reason", NULL);
   char *msg_type = dict_get(d, "talk.msg_type", NULL);
   char *user = cptr->chatname;

   // set a default of &localrig, but use target if passed
   const char *channel = "&localrig";
   if (target) {
      channel = target;
   }

   if (cmd) {
      if (strcasecmp(cmd, "msg") == 0) {
         if (!data) {
            Log(LOG_DEBUG, "chat", "got msg for cptr <%x> with no data: chatname: %s", cptr, user);
            return true;
         }

         // If the message is empty, just return success
         if (strlen(data) == 0) {
            Log(LOG_CRAZY, "chat", "talk msg has no data");
            return false;
         }

         if (!has_priv(cptr->user->uid, "admin|owner|chat")) {
            Log(LOG_CRAZY, "chat", "user %s doesn't have chat privileges but tried to send a message", user);
            // XXX: Alert the user that their message was NOT deliverred because they aren't allowed to send it.
            ws_send_error(cptr, "You do not have CHAT privilege.");
            return true;
         }

         struct mg_str mp;
         char msgbuf[HTTP_WS_MAX_MSG+1];

         // sanity check
         if (!user) {
            Log(LOG_CRAZY, "chat", "talk parse, msg has no user field");
            return true;
         }

         // handle a file chunk
         if (msg_type) {
            if (strcasecmp(msg_type, "file_chunk") == 0) {
               char *filetype = dict_get(d, "talk.filetype", NULL);
               char *filename = dict_get(d, "talk.filename", NULL);
               long chunk_index = dict_get_long(d, "talk.chunk_index", 0);
               long total_chunks = dict_get_long(d, "talk.total_chunks", 0);

               const char *jp = dict2json_mkstr(
                  VAL_DOUBLE, "talk.chunk_index", chunk_index,
                  VAL_STR, "talk.cmd", "msg",
                  VAL_STR, "talk.data", data,
                  VAL_STR, "talk.from", cptr->chatname,
                  VAL_STR, "talk.msg_type", msg_type,
                  VAL_DOUBLE, "talk.total_chunks", total_chunks,
                  VAL_STR, "talk.filename", filename,
                  VAL_STR, "talk.filetype", filetype,
                  VAL_LONG, "talk.ts", now);

               mp = mg_str(jp);
               // Send to everyone, including the sender, which will then display it as SelfMsg
               ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
               free((void *)jp);
               return false;
            } else if (strcasecmp(msg_type, "pub") == 0 ||
               strcasecmp(msg_type, "action") == 0) {

               if (strcasecmp(msg_type, "action") == 0) {
                  Log(LOG_CRAZY, "ws.chat", "%s * %s%s", channel, cptr->chatname, data);
               } else {
                  Log(LOG_CRAZY, "ws.chat", "%s <%s> %s", channel, cptr->chatname, data);
               }
               // Check for commands
               if (data[0] == '!') {
#if	0
                  char *input = data;
                  char cmd[16], arg[32];
                  size_t cmd_len = sizeof(cmd), arg_len = sizeof(arg);

                  if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
                     /// XXX: we should send an error alert
                     return true;;
                  }

                  while (*input) {
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

                     if (strcasecmp(cmd, "help") == 0) {
                        // XXX: These should move to help/ and get served via that mechanism
                        ws_send_notice(cptr->conn, "<span>***SERVER***"
                           "<br/>*** !help for VFO commands ***<br>"
                           "&nbsp;&nbsp;&nbsp;!freq <freq> - Set frequency to <freq> - can be 7200 7.2m 7200000 etc form<br/>"
                           "&nbsp;&nbsp;&nbsp;!mode <mode> - Set mode to CW|AM|LSB|USB|FM|DL|DU<br/>"
                           "&nbsp;&nbsp;&nbsp;!power <power> - Set power (NYI)<br/>"
                           "&nbsp;&nbsp;&nbsp;!vfo <vfo> - Switch VFOs (A|B|C)<br/>"
                           "&nbsp;&nbsp;&nbsp;!width <width> - Set passband width (narrow|normal|wide)<br/></span>");
                        return false;
                     } else if (strcasecmp(cmd, "freq") == 0) {
                        long real_freq = parse_freq(arg);
                        Log(LOG_DEBUG, "ws.chat", "Got !freq %lu (%s) from %s", real_freq, arg, cptr->chatname);
                        rr_freq_set(active_vfo, real_freq);
                     } else if (strcasecmp(cmd, "mode") == 0) {
                        Log(LOG_DEBUG, "ws.chat", "Got !mode %s from %s", arg, cptr->chatname);
                        rr_mode_t new_mode = vfo_parse_mode(arg);
                        if (new_mode != MODE_NONE) {
                           rr_set_mode(active_vfo, new_mode);
                        }
                     } else if (strcasecmp(cmd, "power") == 0) {
                        Log(LOG_DEBUG, "ws.chat", "Got !power %s from %s", arg, cptr->chatname);
                     } else if (strcasecmp(cmd, "width") == 0) {
                        Log(LOG_DEBUG, "ws.chat", "Got !width %s from %s", arg, cptr->chatname);
                        rr_set_width(active_vfo, arg);
                     } else if (strcasecmp(cmd, "vfo") == 0) {
                        Log(LOG_DEBUG, "ws.chat", "Got !vfo %s from %s", arg, cptr->chatname);
                     } else {
                        Log(LOG_WARN, "ws.chat", "Unknown command: %s", cmd);
                        return false;
                     }
                  }

                  // these events shouldn't get relayed because the CAT events generated *will* be relayed
                  return false;
#endif
               } else { // Chat message
                  // Check if this is to a local channel. If not, relay it
                  if (channel[0] != '&') {
                     // Send the message to all connected servers
                  }

                  // Log to database, if configured
                  if (cfg_get_bool("chat.log", false)) {
// XXX: readd
//                     bool db_res = db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, data);
//                     if (!db_res) {
//                        fprintf(stderr, "db_add_chat_msg failed\n");
//                     }
                  }

                  const char *jp = dict2json_mkstr(
                     VAL_STR, "talk.cmd", "msg",
                     VAL_STR, "talk.data", data,
                     VAL_STR, "talk.from", cptr->chatname,
                     VAL_STR, "talk.target", channel,
                     VAL_STR, "talk.msg_type", msg_type,
                     VAL_LONG, "talk.ts", now);

                  mp = mg_str(jp);

                  // Send to everyone, including the sender, which will then display it as SelfMsg
                  ws_broadcast(NULL, &mp, WEBSOCKET_OP_TEXT);
                  free((void *)jp);
                  return false;
               }
            } else {
               Log(LOG_DEBUG, "ws.chat", "unknown message type: %s", msg_type);
            }
         }
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (!target) {
            // XXX: Send a warning to the user informing that they must specify a target username
            Log(LOG_DEBUG, "chat", "whois with no target");
            return true;
         }

         char msgbuf[HTTP_WS_MAX_MSG + 1];
         http_client_t *acptr = http_client_list;

         if (!acptr) {
            Log(LOG_DEBUG, "chat", "whois no users online?!?");
            return true;
         }
// XXX: These don't belong here?
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
      }
   }
   return true;
}
