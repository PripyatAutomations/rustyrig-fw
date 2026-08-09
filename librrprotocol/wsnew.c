//
// Vadlidate incoming messages from websocket and dispatch them as events
//
// Enable 
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

#define	DEBUG_PROTO_UNKNOWN
typedef struct ws_proto_handler {
   char *cmd;				// Command tag
   int   min_arg,
         max_arg;
   bool (*func)(rrconn_t *cptr, dict *msg);
} ws_proto_handler_t;

typedef struct ws_arg {
   enum {
      ARG_NONE = 0,
      ARG_STR,
      ARG_INT,
      ARG_FLOAT
   } argtype;
   bool required;
} ws_arg_t;

////////////////////////////////////////
#if	defined(DEBUG_PROTO_UNKNOWN)
bool	ws_proto_debug_unknowns = true;
#else
bool	ws_proto_debug_unknowns = false;
#endif	// defined(DEBUG_PROTO_UNKNOWN)

////////////////
// Validators //
////////////////
bool ws_msg_alert(rrconn_t *cptr, dict *msg) {
   char *a_msg = dict_get(msg, "alert.msg", NULL);
   char *a_from = dict_get(msg, "alert.from", NULL);
   char *a_ts = dict_get(msg, "alert.ts", NULL);

   //////////////////////////
   // Validate the message //
   //////////////////////////
   if (!a_msg || !a_from || !a_ts) {
      // We're missing parameters
      return true;
   }

   event_emit("alert", NULL, msg);

   return false;
}

bool ws_msg_cat(rrconn_t *cptr, dict *msg) {
   char *ts = dict_get(msg, "cat.ts", NULL);
   char *from = dict_get(msg, "cat.from", NULL);
   char *data = dict_get(msg, "cat.data", NULL);

   //////////////////////////
   // Validate the message //
   //////////////////////////
   if (!ts || !from || !data) {
      // We're missing critical parameters, discard it for now
      return true;
   }

   event_emit("cat", NULL, msg);
   return false;
}

bool ws_msg_privmsg(rrconn_t *cptr, dict *msg) {
   char *ts = dict_get(msg, "privmsg.ts", NULL);
   char *from = dict_get(msg, "privmsg.from", NULL);
   char *data = dict_get(msg, "privmsg.data", NULL);

   //////////////////////////
   // Validate the message //
   //////////////////////////
   if (!ts || !from || !data) {
      // We're missing critical parameters, discard it for now
      return true;
   }

   event_emit("privmsg", NULL, msg);
   return false;
}

bool ws_msg_syslog(rrconn_t *cptr, dict *msg) {
   char *ts = dict_get(msg, "syslog.ts", NULL);
   char *prio = dict_get(msg, "syslog.prio", NULL);
   char *subsys = dict_get(msg, "syslog.subsys", NULL);
   char *data = dict_get(msg, "syslog.data", NULL);

   //////////////////////////
   // Validate the message //
   //////////////////////////
   if (!ts || !prio || !subsys || !data) {
      // We're missing parameters, just discard it for now
      return true;
   }

   event_emit("syslog", cptr, msg);
   return false;
}

ws_proto_handler_t ws_proto_handlers[] = {
  { .cmd = "alert",   .func = ws_msg_alert },
//  { .cmd = "hello",   .func = ws_msg_hello },
//  { .cmd = "ping",    .func = ws_msg_ping },
  { .cmd = "cat",     .func = ws_msg_cat },
  { .cmd = "privmsg", .func = ws_msg_privmsg },
//  { .cmd = "quit",    .func = ws_msg_quit },
  { .cmd = "syslog",  .func = ws_msg_syslog },
  { .cmd = NULL }
};

// Dispatch an incoming websocket message to it's appropriate
// validator who will send it on or throw an error
bool ws_proto_dispatch(rrconn_t *cptr, dict *msg) {

   int h = (sizeof(ws_proto_handlers) / sizeof(ws_proto_handler_t));

   for (int i = 0; i < h; i++) {
      ws_proto_handler_t *wp = &ws_proto_handlers[i];

      if (!wp) {
         return true;
      }

      // This is gross
      if (wp->cmd && wp->cmd[0]) {
         char *tp = dict_get(msg, wp->cmd, NULL);
         if (strcasecmp(wp->cmd, tp) == 0) {
         }
      }
   }
   return false;
}
