// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
/* RustyRig GTK chat delivery identity. MIT / GPL when built with Mongoose. Snapshots are captured in core and consumed only on the GTK owner thread. They
 * contain no borrowed connection or component-state pointers. */
#ifndef RR_GTK_CHAT_CONTEXT_H
#define RR_GTK_CHAT_CONTEXT_H
#include <glib.h>
#include <rrclient/connman.h>

typedef struct {
   char *profile, *room, *window, *nickname, *session;
   bool irc, selected;
} GtkChatContext;

void gtk_chat_context_capture(GtkChatContext *snapshot, const char *room);
void gtk_chat_context_clear(GtkChatContext *snapshot);
/* Scoped presentation identity; never switches the core connection context. */
const GtkChatContext *gtk_chat_context_push(const GtkChatContext *snapshot);
void gtk_chat_context_pop(const GtkChatContext *previous);
const GtkChatContext *gtk_chat_context_current(void);
const char *gtk_chat_window_name(const char *room);
const char *gtk_chat_profile(void);
bool gtk_chat_context_selected(void);
bool gtk_chat_context_irc(void);
/* Register core capture listeners; context must outlive module subscriptions. */
void gtk_chat_protocol_register(GMainContext *context);
void gtk_chat_note_activity(const char *room, bool query);
#endif
