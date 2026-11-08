//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_UI_NAVIGATION_H
#define RRCLIENT_UI_NAVIGATION_H

/* Fixed-size, pointer-free command across GTK -> core event domain. */
#define RRCLIENT_UI_CHAT_LINK_EVENT "client.ui.chat.link-join"
typedef struct {
   char server[1024];           /* empty means keep selected server */
   char room[128];
} rrclient_ui_chat_link_t;

#endif
