// Client password rotation over authenticated TLS.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_PASSWORD_CHANGE_H
#define RRCLIENT_PASSWORD_CHANGE_H
#include <stdbool.h>
#define RRCLIENT_PASSWORD_PROMPT_EVENT "client.ui.password-change.prompt"
#define RRCLIENT_PASSWORD_SUBMIT_EVENT "client.password-change.submit"
#define RRCLIENT_PASSWORD_RESULT_EVENT "client.ui.password-change.result"
typedef struct rrclient_password_change {
   char profile[128];
   char username[128];
   char password[257]; /* sensitive: zero after processing; never log */
   bool success;
   char reason[256];
} rrclient_password_change_t;
#endif
