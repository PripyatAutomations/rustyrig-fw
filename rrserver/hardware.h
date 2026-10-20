//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// rrserver/hardware.h: server-owned attached hardware and exclusive leases
// SPDX-License-Identifier: MIT
#ifndef RRSERVER_HARDWARE_H
#define RRSERVER_HARDWARE_H
#include <stdbool.h>
#include <stddef.h>

typedef struct rr_hardware rr_hardware_t;
typedef struct rr_hardware_device rr_hardware_device_t;
/* Driver callbacks return true on success. No events or client access here. Registry mutation/claim/release belongs to the server thread. After claim, only the
 * owning worker may access the driver until that worker is stopped. */
typedef struct {
   bool (*claim)(void *data, const char *owner);
   bool (*release)(void *data);
   void (*destroy)(void *data);
} rr_hardware_driver_t;
typedef struct {
   const char *id, *kind, *driver, *path, *parent;
   const rr_hardware_driver_t *api;
   void *data;
} rr_hardware_spec_t;

rr_hardware_t *rr_hardware_new(void);
/* Stop all owning workers before freeing. Releases devices in reverse order. */
void rr_hardware_free(rr_hardware_t *registry);
/* Copies metadata; takes data ownership only on success. IDs must be unique. */
rr_hardware_device_t *rr_hardware_add(rr_hardware_t *, const rr_hardware_spec_t *);
rr_hardware_device_t *rr_hardware_find(const rr_hardware_t *, const char *id);
size_t rr_hardware_count(const rr_hardware_t *);
const rr_hardware_device_t *rr_hardware_at(const rr_hardware_t *, size_t index);
const char *rr_hardware_id(const rr_hardware_device_t *);
const char *rr_hardware_kind(const rr_hardware_device_t *);
const char *rr_hardware_driver(const rr_hardware_device_t *);
const char *rr_hardware_path(const rr_hardware_device_t *);
const char *rr_hardware_parent(const rr_hardware_device_t *);
const char *rr_hardware_owner(const rr_hardware_device_t *);
const char *rr_hardware_error(const rr_hardware_device_t *);
void *rr_hardware_data(const rr_hardware_device_t *);
/* A second claim fails, even from the same owner. Failed releases retain the lease, preventing reassignment of a device whose safe state is uncertain. */
bool rr_hardware_claim(rr_hardware_device_t *, const char *owner);
bool rr_hardware_release(rr_hardware_device_t *, const char *owner);
#endif
