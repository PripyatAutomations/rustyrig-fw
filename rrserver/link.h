//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RRSERVER_LINK_H
#define RRSERVER_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <librustyaxe/core.h>
struct mg_mgr;
typedef void (*rr_link_health_fn)(const char *subject, const char *site, bool online,
                                  unsigned flaps, unsigned auth_errors, long long rtt_ms, void *context);
void rrserver_links_health_foreach(rr_link_health_fn emit, void *context);
/* Read-only persisted link diagnostics, empty when unavailable. */
void rrserver_links_recent_events(const char *subject, char *out, size_t capacity);
bool rrserver_peer_config_init(void);
bool rrserver_links_init(struct mg_mgr *manager);
void rrserver_links_poll(void);
void rrserver_links_fini(void);
const char *rrserver_links_node(void);
const char *rrserver_links_route(const char *node);
/* Human-facing label: site [uuid] when known, UUID otherwise. Never use this string in protocol fields, keys or identity comparisons. */
const char *rrserver_links_format_node(const char *node, char *buffer, size_t capacity);
bool rrserver_links_send(const char *node, dict *message);
void rrserver_links_broadcast(dict *message);
#endif
