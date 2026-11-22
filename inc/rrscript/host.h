/* RustyScript host integration, shared by rrclient and rrserver.
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef RR_RRSCRIPT_HOST_H
#define RR_RRSCRIPT_HOST_H
#include <stdbool.h>
typedef struct rrscript_host rrscript_host_t;
/* Loads local .rrs scripts. Scripts do not run merely by being loaded.
 * The caller owns the returned host and closes it before event_shutdown(). */
rrscript_host_t *rrscript_host_open(const char *directory, bool client);
void rrscript_host_close(rrscript_host_t *host);
/* Return true if a script binding consumed the command or shortcut. */
bool rrscript_host_command(rrscript_host_t *host, const char *line);
bool rrscript_host_hotkey(rrscript_host_t *host, const char *sequence);
#endif
