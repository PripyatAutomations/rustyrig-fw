//
//
//

typedef struct ws_proto_handler {
   char *cmd;				// Command tag
   int   min_arg,
         max_arg;
   bool (*func)(dict *msg);
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
bool cli_ws_alert(dict *msg) {
   char *a_msg = dict_get(msg, "alert.msg", NULL);
   char *a_from = dict_get(msg, "alert.from", NULL);
   char *a_ts = dict_get(msg, "alert.ts", NULL);

   // Validate the message
   ///////////////////////
   if (!a_msg || !a_from || !a_ts) {
      // We're missing parameters
      return true;
   }

   return false;
}

bool cli_ws_privmsg(dict *msg) {
   return false;
}

bool cli_ws_syslog(dict *msg) {
   char *ts = dict_get(msg, "syslog.ts", NULL);
   char *prio = dict_get(msg, "syslog.prio", NULL);
   char *subsys = dict_get(msg, "syslog.subsys", NULL);
   char *data = dict_et(msg, "syslog.data", NULL);

   // Validate the message
   ///////////////////////
   if (!ts || !prio || !subsys || !data) {
      // We're missing parameters
      return true;
   }

   event_emit("syslog", msg);
   return false;
}

ws_proto_handler_t ws_proto_handlers_cli[] = {
  { .cmd = "alert",   .func = cli_ws_alert },
  { .cmd = "privmsg", .func = cli_ws_privmsg },
  { .cmd = "syslog",  .func = cli_ws_syslog },
  { .cmd = NULL }
};

///////////////
bool srv_ws_cat(dict *msg) {
   return false;
}

ws_proto_handler_t ws_proto_handlers_srv[] = {
  { .cmd = "cat",      .func = srv_ws_cat },
  { .cmd = NULL }
};
