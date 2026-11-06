//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Server-owned external SDR bridge subprocess lifecycle.
#ifndef RRSERVER_BRIDGE_MANAGER_H
#define RRSERVER_BRIDGE_MANAGER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rrserver/backend.h>
typedef struct {
   char name[80], driver[32];
   int pid;
   uint64_t ipc_in, ipc_out, pcm_bytes, pcm_frames, errors;
   uint64_t wire_rx, wire_tx, last_report_us;
} rr_bridge_worker_stats_t;
/* Core-thread snapshot: no pointers to live bridge objects escape. */
size_t rrserver_bridges_stats_snapshot(rr_bridge_worker_stats_t *out, size_t capacity);
const rr_backend_type_t *rrserver_bridge_backend_type(void);
bool rrserver_bridges_prepare(void);
bool rrserver_bridges_start(const char *config_file);
void rrserver_bridges_poll(void);
bool rrserver_bridge_temporary(const struct rr_server_rig *radio);
struct rr_server_rig *rrserver_bridge_for_sdr_room(const char *room);
void rrserver_websdr_register_events(void);
void rrserver_bridges_stop(void);
#endif
