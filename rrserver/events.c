//
// rrserver/events.c: event listeners for shared protocol events from
// librrprotocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>
#include <rrclient/ui.h>

static void rrserver_handle_talkmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[talk.msg]\n");
   dict_dump(d, stderr);
#if	0
   //sqlite3 *masterdb = NULL;
   /*
    *  bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src, const char
    * *msg_dest, const char *msg_type, const char *msg_data) {
    *  (void)db;
    *  (void)msg_ts;
    *  (void)msg_src;
    *  (void)msg_dest;
    *  (void)msg_type;
    *  (void)msg_data;
    *  return false;
    *  }
    */
    // Log to database, if configured if (cfg_get_bool("chat.log", false) ) {
    bool db_res = db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, data);
  
       if (!db_res) {
          fprintf(stderr, "db_add_chat_msg failed\n");
       }
    }
    const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data",
       data, VAL_STR, "talk.from", cptr->chatname, VAL_STR, "talk.target", channel, VAL_STR,
       "talk.msg_type", msg_type, VAL_LONG, "talk.ts", now);
   free( (void *)jp );
#endif

   dict_free(d);
}

static void rrserver_handle_hello(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[ws.msg.hello]\n");
   dict_dump(d, stderr);
   dict_free(d);
}

static void rrserver_handle_nomatch(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[NOMATCH]\n");
   dict_dump(d, stderr);
   dict_free(d);
}

void rrserver_register_events(void) {
   event_on("NOMATCH", rrserver_handle_nomatch, NULL);
   event_on("ws.msg.hello", rrserver_handle_hello, NULL);
   event_on("talk.msg", rrserver_handle_talkmsg, NULL);
}
