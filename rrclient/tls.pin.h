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
   char cert_sha256[65];
   char cert_cn[256];
   char cert_sans[512];
   char node_name[254];
   char address[256];
   /* 0 = Deny, 1 = Accept Once, 2 = Accept Always */
   unsigned decision;
} rrclient_tls_pin_request_t;
#endif
