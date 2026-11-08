// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* RustyRig GTK chat delivery identity. MIT / GPL when built with Mongoose. */
#include "gtk.chat-context.h"

extern const char *server_name, *login_user;
extern char session_token[];
/* Only GTK accesses the presentation scope. Capture does not read it. */
static const GtkChatContext *presentation;

void gtk_chat_context_capture(GtkChatContext *snapshot, const char *room) {
   snapshot->profile = g_strdup(server_name);
   snapshot->room = g_strdup(room);
   snapshot->window = g_strdup(rrclient_window_name(room));
   snapshot->nickname = g_strdup(login_user);
   snapshot->session = g_strdup(session_token);
   snapshot->irc = ws_conn && !ws_conn->is_ws;
   snapshot->selected = rrclient_present_context();
}

void gtk_chat_context_clear(GtkChatContext *snapshot) {
   g_free(snapshot->profile);
   g_free(snapshot->room);
   g_free(snapshot->window);
   g_free(snapshot->nickname);
   g_free(snapshot->session);
}

const GtkChatContext *gtk_chat_context_push(const GtkChatContext *snapshot) {
   const GtkChatContext *previous = presentation;
   presentation = snapshot;

   return previous;
}

void gtk_chat_context_pop(const GtkChatContext *previous) {
   presentation = previous;
}

const GtkChatContext *gtk_chat_context_current(void) {
   return presentation;
}

const char *gtk_chat_window_name(const char *room) {
   if (presentation && room && presentation->room && !g_ascii_strcasecmp(room, presentation->room)) {
      return presentation->window;
   }

   return rrclient_window_name(room);
}

const char *gtk_chat_profile(void) {
   return presentation ? presentation->profile : server_name;
}

bool gtk_chat_context_selected(void) {
   if (presentation) {
      return presentation->selected && !g_strcmp0(presentation->profile, rrclient_selected_server());
   }

   return rrclient_present_context();
}

bool gtk_chat_context_irc(void) {
   return presentation ? presentation->irc : ws_conn && !ws_conn->is_ws;
}
