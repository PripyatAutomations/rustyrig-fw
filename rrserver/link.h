#ifndef RRSERVER_LINK_H
#define RRSERVER_LINK_H
#include <stdbool.h>
#include <librustyaxe/core.h>
struct mg_mgr;
bool rrserver_peer_config_init(void);
bool rrserver_links_init(struct mg_mgr *manager);
void rrserver_links_poll(void);
void rrserver_links_fini(void);
const char *rrserver_links_node(void);
const char *rrserver_links_route(const char *node);
bool rrserver_links_send(const char *node, dict *message);
void rrserver_links_broadcast(dict *message);
#endif
