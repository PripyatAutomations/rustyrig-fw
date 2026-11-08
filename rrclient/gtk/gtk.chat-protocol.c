// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* RustyRig GTK protocol presentation. MIT / GPL when built with Mongoose. Capture connection identity in core; render copied data on the GTK owner. */
#include "gtk.chat-context.h"
#include "gtk.core.h"
#include "gtk.chat.h"
#include <rrclient/userlist.h>
#include <rrclient/irc.stats.h>
#include <rrclient/irc.whois.h>

extern bool dying;
extern bool rr_module_token_add(rr_event_token_t token);

/* Render owned protocol snapshots in their originating presentation identity. */
extern char session_token[HTTP_TOKEN_LEN + 1];
extern const char *login_user;
static void frontend_gtk_room_protocol_event(const char *event, const char *data, rrconn_t *connection, void *unused) {
   (void)connection;
   (void)unused;

   if (dying || !data) {
      return;
   }

   if (!strcmp(event, "userinfo")) {
      userlist_redraw_gtk();

      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const GtkChatContext *context = gtk_chat_context_current();
   const char *nickname = context ? context->nickname : login_user;
   const char *token = context ? context->session : session_token;
   const char *room = dict_get(d, "talk.room", NULL);

   if (!room) {
      room = dict_get(d, "talk.target", NULL);
   }

   if (!room) {
      dict_free(d);

      return;
   }

   if (!strcmp(event, "join")) {
      const char *who = dict_get(d, "talk.user", NULL);

      if (who && nickname && !strcasecmp(who, nickname)) {
         gtk_chat_room_add(room);
      }
      userlist_redraw_gtk();
   } else if (!strcmp(event, "part")) {
      const char *session = dict_get(d, "talk.session", NULL);
      bool self = session && *session && token && token[0] && !strcmp(session, token);

      if (self && !dict_get_bool(d, "talk.kicked", false)) {
         gtk_chat_room_remove(room);
      }
      userlist_redraw_gtk();
   } else if (!strcmp(event, "room.deleted")) {
      gtk_chat_room_remove(room);
      userlist_redraw_gtk();
   } else if (!strcmp(event, "room.topic")) {
      gtk_chat_room_set_topic(room, dict_get(d, "talk.topic", ""));
   } else if (!strcmp(event, "room.vfo") || !strcmp(event, "room.vfo-list")) {
      gtk_chat_room_vfos_changed(room);
   } else if (!strcmp(event, "room.state")) {
      userlist_room_state_changed(room);
      userlist_redraw_gtk();
      ptt_button_refresh();
   }
   dict_free(d);
}

static void frontend_gtk_talk_msg_event(const char *event, const char *data, rrconn_t *connection, void *user) {
   (void)event;
   (void)connection;
   (void)user;

   if (dying || !data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const GtkChatContext *context = gtk_chat_context_current();
   const char *nickname = context ? context->nickname : login_user;
   const char *room = dict_get(d, "talk.target", NULL);
   const char *from = dict_get(d, "talk.from", "?");
   const char *kind = dict_get(d, "talk.msg_type", NULL);
   const char *text = dict_get(d, "talk.data", NULL);
   bool query = dict_get_bool(d, "talk.query", false) || (kind && (!strcasecmp(kind, "priv") || !strcasecmp(kind, "privmsg")));

   if (query && from && nickname && strcasecmp(from, nickname)) {
      room = from;
   }

   if (room && *room) {
      const char *unread = text && kind && strncasecmp(kind, "replay-", 7) && (!nickname || strcasecmp(from, nickname)) ? text : NULL;
      gtk_chat_incoming(room, query, unread, nickname);
   }
   dict_free(d);
}

/* Capture synchronously while the transport's core context is still active. All chat work uses idle priority, so JOIN/tab creation precedes its text. */
typedef struct {
   GtkChatContext context;
   char *event, *data;
} GtkChatProtocol;
static gboolean frontend_gtk_chat_protocol_run(gpointer opaque) {
   GtkChatProtocol *pending = opaque;
   const GtkChatContext *previous = gtk_chat_context_push(&pending->context);

   if (!strcmp(pending->event, "talk.msg")) {
      frontend_gtk_talk_msg_event(pending->event, pending->data, NULL, NULL);
   } else {
      frontend_gtk_room_protocol_event(pending->event, pending->data, NULL, NULL);
   }
   gtk_chat_context_pop(previous);

   return G_SOURCE_REMOVE;
}
static void frontend_gtk_chat_protocol_free(gpointer opaque) {
   GtkChatProtocol *pending = opaque;
   gtk_chat_context_clear(&pending->context);
   g_free(pending->event);
   g_free(pending->data);
   g_free(pending);
}
static void frontend_gtk_chat_protocol_capture(const char *event, const char *data, rrconn_t *connection, void *gtk_context) {
   (void)connection;

   if (dying || !data) {
      return;
   }
   dict *d = json2dict(data);

   if (!d) {
      return;
   }
   const char *room = dict_get(d, "talk.room", dict_get(d, "talk.target", NULL));
   const char *kind = dict_get(d, "talk.msg_type", "");
   const char *from = dict_get(d, "talk.from", "");
   bool query = dict_get_bool(d, "talk.query", false) || !strcasecmp(kind, "priv") || !strcasecmp(kind, "privmsg");

   if (!strcmp(event, "talk.msg") && query && login_user && *from && strcasecmp(from, login_user)) {
      room = from;
   }
   GtkChatProtocol *pending = g_new0(GtkChatProtocol, 1);
   gtk_chat_context_capture(&pending->context, room);
   pending->event = g_strdup(event);
   pending->data = g_strdup(data);
   dict_free(d);
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, frontend_gtk_chat_protocol_run, pending, frontend_gtk_chat_protocol_free);
   g_source_attach(source, gtk_context);
   g_source_unref(source);
}

/* Query bookkeeping and highlight counters belong to core, not the GTK scope. */
void gtk_chat_note_activity(const char *room, bool query) {
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add(d, "profile", gtk_chat_profile() ? gtk_chat_profile() : "");
   dict_add(d, "room", room ? room : "");
   dict_add_bool(d, "query", query);
   event_emit_dict("client.ui.chat.activity", NULL, d);
   dict_free(d);
}
static void gtk_chat_activity_core(const char *event, const char *data, rrconn_t *unused, void *user) {
   (void)event;
   (void)unused;
   (void)user;
   dict *d = data ? json2dict(data) : NULL;

   if (!d) {
      return;
   }
   rrconn_t *connection = rrclient_connection_find(dict_get(d, "profile", ""));
   const char *room = dict_get(d, "room", "");

   if (connection && *room) {
      if (dict_get_bool(d, "query", false)) {
         rrclient_irc_query_opened(connection, room);
      } else {
         rrclient_irc_stats_highlight(connection, room);
      }
   }
   dict_free(d);
}

void gtk_chat_protocol_register(GMainContext *context) {
   rr_module_token_add(event_on_domain_token("client.ui.chat.activity", gtk_chat_activity_core, NULL, "core"));
   static const char *events[] = {
      "join", "part", "room.deleted", "room.topic", "room.vfo",
      "room.vfo-list", "room.state", "userinfo", "talk.msg"
   };

   for (size_t i = 0 ; i < G_N_ELEMENTS(events) ; i++) {
      rr_event_token_t token = event_on_token(events[i], frontend_gtk_chat_protocol_capture, context);

      if (token) {
         rr_module_token_add(token);
      } else {
         g_log("RustyRig-GTK", G_LOG_LEVEL_WARNING, "Unable to subscribe to %s for GTK delivery", events[i]);
      }
   }
}
