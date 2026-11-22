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
/* Names are .rrs filenames at the script root, or autoload/name.rrs.
 * No absolute paths or traversal. These functions must run in the host's
 * script execution context, not concurrently with callback delivery. */
bool rrscript_host_load(rrscript_host_t *host, const char *name, char **error);
bool rrscript_host_unload(rrscript_host_t *host, const char *name, char **error);
bool rrscript_host_reload(rrscript_host_t *host, const char *name, char **error);
/* Lists available scripts in the root and autoload directories, including
 * whether each is currently loaded. */
void rrscript_host_list(rrscript_host_t *host,
                        void (*visit)(const char *name, bool loaded, void *user),
                        void *user);
/* Return true if a script binding consumed the command or shortcut. */
/* Lookup only, with no execution; allows local script commands while offline. */
bool rrscript_host_has_command(rrscript_host_t *host, const char *line);
bool rrscript_host_command(rrscript_host_t *host, const char *line);
bool rrscript_host_hotkey(rrscript_host_t *host, const char *sequence);
#endif
