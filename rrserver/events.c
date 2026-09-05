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
#include <rrserver/backend.h>


static void rrserver_handle_hello(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   if (!d) {
      return;
   }
   dict_dump(d, NULL);
   dict_free(d);
}


static void rrserver_handle_nomatch(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }
   Log(LOG_WARN, "ws.nomatch", "NOMATCH: %s", data);
}

static void rrserver_handle_rigctlmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data) {
      return;
   }

   dict *d = json2dict(data);
   if (!d) {
      return;
   }

   dict_dump(d, NULL);

    const char *rc_cmd = dict_get(d, "rigctl.cmd", NULL);
   const char *rc_vfo = dict_get(d, "rigctl.vfo", NULL);
   const char *rc_from = dict_get(d, "rigctl.from", NULL);
   int rc_freq = dict_get_int(d, "rigctl.freq", 0);
   const char *rc_mode = dict_get(d, "rigctl.mode", NULL);

   Log(LOG_CRIT, "ws.rigctl", "cmd: %s, vfo: %s, from: %s, freq: %d, mode: %s",
      rc_cmd, rc_vfo, rc_from, rc_freq, rc_mode ? rc_mode : "(none)");

   if (!rc_cmd || !rc_vfo || !rc_from) {
      dict_free(d);
      return;
   }

   rr_vfo_t vfo = vfo_lookup(rc_vfo[0]);

   if (strcasecmp(rc_cmd, "mode") == 0) {
      // Set the rig mode (from !mode chat command or ws cat.cmd mode)
      if (!rc_mode) {
         Log(LOG_WARN, "ws.rigctl", "MODE set without a mode");
         dict_free(d);
         return;
      }

      rr_mode_t new_mode = vfo_parse_mode(rc_mode);
      if (new_mode == MODE_NONE) {
         Log(LOG_WARN, "ws.rigctl", "Couldn't parse mode %s", rc_mode);
         dict_free(d);
         return;
      }

      // Audit trail: who changed which VFO to what mode
      Log(LOG_AUDIT, "ws.rigctl", "User %s set VFO %s MODE to %s", rc_from, rc_vfo, rc_mode);
      rr_set_mode(vfo, new_mode);
      dict_free(d);
      return;
   }

   fprintf(stderr, "setting vfo %s freq to %d\n", rc_vfo, rc_freq);

   // Audit trail: who changed which VFO to what frequency
   Log(LOG_AUDIT, "ws.rigctl", "User %s set VFO %s FREQ to %d hz", rc_from, rc_vfo, rc_freq);

   rr_freq_set(vfo, rc_freq);
   dict_free(d);
}


static void rrserver_handle_send_chat_replay(const char *event, const char *data, rrconn_t *cptr, void *user) {
   if (!data || !cptr) {
      return;
   }

   dict *d = json2dict(data);
   if (!d) {
      return;
   }

   dict_dump(d, NULL);

   const char *channel = dict_get(d, "talk.target", NULL);

#ifdef USE_SQLITE
   if (channel) {
      db_send_chat_replay(cptr, channel);
   }
#endif
   dict_free(d);
}


static void rrserver_handle_talkmsg(const char *event, const char *data, rrconn_t *cptr, void *user) {
   Log(LOG_CRAZY, "events.ws", "ENTER talk.msg: event=<%s> data=<%p> cptr=<%p>",
      event ? event : "(null)", data, cptr);

   if (!data || !cptr) {
      Log(LOG_CRIT, "ws.chat", "handle_talkmsg with data:<%p> and cptr:<%p>",
         data, cptr);
      return;
   }

   dict *d = json2dict(data);
   Log(LOG_CRAZY, "events.ws", "talk.msg json2dict returned d=<%p>", d);

   if (!d) {
      Log(LOG_WARN, "ws.chat", "failed to parse talk.msg event");
      return;
   }

   const char *channel = dict_get(d, "talk.target", NULL);

   const char *msg_type = dict_get(d, "talk.msg_type", NULL);

   if (!msg_type) {
      Log(LOG_WARN, "ws.chat", "talk.msg event has no message type");
      dict_free(d);
      return;
   }

   /*
    * File chunks aren't normal chat messages and should not be
    * written to chat_log. They still need to be broadcast.
    */
   if (strcasecmp(msg_type, "file_chunk") == 0) {
      Log(LOG_DEBUG, "ws.chat", "broadcasting file chunk from %s", cptr->chatname);
      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
      return;
   }

   /*
    * Normal public/action messages.
    */
   if (strcasecmp(msg_type, "pub") == 0 ||
       strcasecmp(msg_type, "action") == 0) {

      if (strcasecmp(msg_type, "action") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s * %s%s",
            channel ? channel : "&localrig",
            cptr->chatname,
            dict_get(d, "talk.data", ""));
      } else if (strcasecmp(msg_type, "pub") == 0) {
         Log(LOG_INFO, "ws.chat", "** %s <%s> %s",
            channel ? channel : "&localrig",
            cptr->chatname, dict_get(d, "talk.data", ""));
      }

      const char *talk_from = dict_get(d, "talk.from", NULL);
      const char *talk_target = dict_get(d, "talk.target", NULL);
      const char *talk_msg_type = dict_get(d, "talk.msg_type", NULL);
      const char *talk_msg = dict_get(d, "talk.data", NULL);

#ifdef USE_SQLITE
      if (channel) {
         if (!db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, talk_msg)) {
            Log(LOG_WARN, "db", "failed to save chat message");
         }
      }
#endif

      Log(LOG_CRAZY, "ws.chat", "talk.msg broadcasting: from=<%s> target=<%s> type=<%s>",
         talk_from, talk_target, talk_msg_type);

      ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
      Log(LOG_CRAZY, "ws.chat", "talk.msg broadcast returned");
      dict_free(d);
      return;
   }

   Log(LOG_DEBUG, "ws.chat", "unknown talk.msg type: %s", msg_type);
   dict_free(d);
}


static void rrserver_handle_recording_start(const char *event,
                                            const char *data,
                                            rrconn_t *cptr,
                                            void *user) {
   // Deal with this
}


static void rrserver_handle_recording_stop(const char *event,
                                           const char *data,
                                           rrconn_t *cptr,
                                           void *user) {
   // Deal with this
}


void rrserver_register_events(void) {
   Log(LOG_CRAZY, "events", "Registering rrserver events");
   event_on("NOMATCH", rrserver_handle_nomatch, NULL);
   event_on("recording-start", rrserver_handle_recording_start, NULL);
   event_on("recording-stop",rrserver_handle_recording_stop, NULL);
   event_on("rigctl", rrserver_handle_rigctlmsg, NULL);
   event_on("send-chat-replay", rrserver_handle_send_chat_replay, NULL);
   event_on("talk.msg", rrserver_handle_talkmsg, NULL);
   event_on("hello", rrserver_handle_hello, NULL);
   Log(LOG_CRAZY, "events", "Finished registering rrserver events");
}
