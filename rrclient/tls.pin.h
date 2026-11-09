// RustyRig client TLS trust-on-first-use confirmation events.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRCLIENT_TLS_PIN_H
#define RRCLIENT_TLS_PIN_H
#include <stdbool.h>
#define RRCLIENT_TLS_PIN_PROMPT_EVENT "client.ui.tls-pin.prompt"
#define RRCLIENT_TLS_PIN_REPLY_EVENT "client.tls-pin.reply"
typedef struct rrclient_tls_pin_request {
   char profile[128];
   char old_pin[65];
   char new_pin[65];
   bool approved;
} rrclient_tls_pin_request_t;
#endif
