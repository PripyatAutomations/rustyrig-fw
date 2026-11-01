#ifndef RRCLIENT_UI_NAVIGATION_H
#define RRCLIENT_UI_NAVIGATION_H
#include <stdbool.h>

/* Fixed-size, pointer-free command across GTK -> core event domain. */
#define RRCLIENT_UI_CHAT_LINK_EVENT "client.ui.chat.link-join"
typedef struct {
   char server[1024]; /* empty means keep selected server */
   char room[128];
} rrclient_ui_chat_link_t;

#define RRCLIENT_UI_CHAT_INCOMING_EVENT "client.ui.chat.incoming"
typedef struct {
   char room[128];
   char nickname[128];
   char unread[4096];
   bool query;
   bool has_unread;
} rrclient_ui_chat_incoming_t;

#endif
