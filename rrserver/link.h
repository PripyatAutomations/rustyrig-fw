#ifndef RRSERVER_LINK_H
#define RRSERVER_LINK_H
#include <stdbool.h>
struct mg_mgr;
bool rrserver_peer_config_init(void);
bool rrserver_links_init(struct mg_mgr *manager);
void rrserver_links_poll(void);
void rrserver_links_fini(void);
#endif
