//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Per-instance hardware I/O workers; all completion callbacks run on the server thread.
#ifndef RRSERVER_BACKEND_WORKER_H
#define RRSERVER_BACKEND_WORKER_H
#include <rrserver/backend.h>
#include <rrserver/rig.properties.h>

typedef enum {
   RR_BACKEND_POLL, RR_BACKEND_FREQ, RR_BACKEND_MODE, RR_BACKEND_WIDTH,
   RR_BACKEND_POWER, RR_BACKEND_PTT
} rr_backend_operation_t;
typedef void (*rr_backend_completion_fn)(rr_backend_t *, rr_server_vfo_t *, bool failed, void *);
typedef void (*rr_backend_observation_fn)(rr_backend_t *, rr_server_vfo_t *, rr_vfo_data_t *);

bool rr_backend_worker_start(rr_backend_t *, rr_backend_observation_fn);
void rr_backend_worker_stop(rr_backend_t *);
void rr_backend_worker_drain(rr_backend_t *);
bool rr_backend_worker_busy(rr_backend_t *);
bool rr_backend_worker_yield(rr_backend_t *);
bool rr_backend_worker_submit(rr_backend_t *, rr_server_vfo_t *, rr_backend_operation_t, dict_value_t, rr_backend_completion_fn, void *);
rr_property_update_t rr_backend_property_observe(rr_backend_t *, rr_server_vfo_t *, const char *, val_type_t, const dict_value_t *);
rr_property_update_t rr_backend_property_unavailable(rr_backend_t *, rr_server_vfo_t *, const char *);
bool rr_backend_worker_shutdown(rr_backend_t *, int code);
/* Backend associated with the current worker thread, NULL elsewhere. */
rr_backend_t *rr_backend_worker_current(void);
void rr_backend_log(rr_backend_t *, logpriority_t, const char *, const char *, ...) __attribute__((format(printf, 4, 5)));
#endif
