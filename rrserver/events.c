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
//
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/database.h>

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

static void rrserver_handle_send_chat_replay(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[send-chat-replay]\n");
   dict_dump(d, stderr);
   const char *channel = dict_get(d, "talk.target", NULL);

#ifdef  USE_SQLITE
   if (channel) {
      db_send_chat_replay(cptr, channel);
   }
#endif

   dict_free(d);
}

static void rrserver_handle_talkmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   fprintf(stderr, "[talk.msg]\n");
   dict_dump(d, stderr);
   const char *channel = dict_get(d, "talk.target", NULL);
   const char *msg_type = dict_get(d, "talk.msg_type", NULL);

   if (!channel || !msg_type) {
      dict_free(d);
      return;
   }

#ifdef  USE_SQLITE
   // Log to database, if configured if (cfg_get_bool("chat.log", false) ) {
   bool db_res = db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, data);

   if (!db_res) {
      fprintf(stderr, "db_add_chat_msg failed\n");
   }
#endif

   dict_free(d);
}

static void rrserver_handle_recording_start(const char *event, const char *data, rrconn_t *cptr, void *user) {
   // Deal with this
}

static void rrserver_handle_recording_stop(const char *event, const char *data, rrconn_t *cptr, void *user) {
   // Deal with this
}

void rrserver_register_events(void) {
   event_on("NOMATCH", rrserver_handle_nomatch, NULL);
   event_on("recording-start", rrserver_handle_recording_start, NULL);
   event_on("recording-stop", rrserver_handle_recording_start, NULL);
   event_on("send-chat-replay", rrserver_handle_send_chat_replay, NULL);
   event_on("talk.msg", rrserver_handle_talkmsg, NULL);
   event_on("ws.msg.hello", rrserver_handle_hello, NULL);
}
