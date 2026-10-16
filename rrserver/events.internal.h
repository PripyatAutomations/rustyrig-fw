//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#ifndef RRSERVER_EVENTS_INTERNAL_H
#define RRSERVER_EVENTS_INTERNAL_H
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/auth.h>

/* Server-private event domains; called once by rrserver_register_events(). */
void rrserver_events_rooms_register(void);
void rrserver_events_rig_register(void);
void rrserver_events_quota_register(void);
void rrserver_events_users_register(void);
void rrserver_events_admin_register(void);

void rrserver_handle_room_delete(const char *, const char *, rrconn_t *, void *);

#ifdef USE_SQLITE
/* Shared account checks and replies; these do not perform admission checks. */
bool rrserver_admin_send(rrconn_t *, const char *, const char *, dict *);
bool rrserver_user_privilege_list_valid(const char *, bool);
bool rrserver_user_target_allowed(const http_user_t *, const http_user_t *, bool);
bool rrserver_user_reload_database(rrconn_t *);
bool rrserver_user_privilege_has_elevated(const char *);
#endif
#endif
