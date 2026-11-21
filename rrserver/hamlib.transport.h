// SPDX-License-Identifier: GPL-2.0-only
#ifndef RRSERVER_HAMLIB_TRANSPORT_H
#define RRSERVER_HAMLIB_TRANSPORT_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* One private socketpair + child per physical rig. Exactly one owner thread
 * may call rr_hamlib_transport_exchange(); heartbeats have a dedicated sender.
 * This transport never authorizes TX or interprets the rig operation payload.
 */
typedef struct rr_hamlib_transport {
   int fd;
   int watchdog_fd; /* separate child-initiated liveness challenge channel */
   pid_t pid;
   pthread_t watchdog;
   bool watchdog_started;
   pthread_t heartbeat;
   pthread_mutex_t send_mutex;
   atomic_bool running;
   atomic_bool connected;
   bool heartbeat_started;
   uint32_t sequence;
} rr_hamlib_transport_t;

bool rr_hamlib_transport_open(rr_hamlib_transport_t *t, const char *path,
   int model, const char *device, int baud, int timeout_ms);
bool rr_hamlib_transport_exchange(rr_hamlib_transport_t *t, uint16_t type,
   const void *payload, uint32_t length, uint16_t reply_type,
   void *reply, uint32_t *reply_length, uint32_t reply_capacity,
   int timeout_ms);
bool rr_hamlib_transport_ready(const rr_hamlib_transport_t *t);
void rr_hamlib_transport_close(rr_hamlib_transport_t *t);

#endif
