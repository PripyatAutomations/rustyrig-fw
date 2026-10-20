//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// rrserver/hardware.c: attached-device registry; no protocol or UI ownership
// SPDX-License-Identifier: MIT
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <rrserver/hardware.h>

struct rr_hardware_device {
   char *id, *kind, *driver, *path, *parent, *owner, *error;
   const rr_hardware_driver_t *api;
   void *data;
};
struct rr_hardware {
   GPtrArray *devices;
};

rr_hardware_t *rr_hardware_new(void) {
   rr_hardware_t *r = g_new0(rr_hardware_t, 1);
   r->devices = g_ptr_array_new();

   return r;
}

void rr_hardware_free(rr_hardware_t *r) {
   if (!r) {
      return;
   }

   for (size_t i = r->devices->len ; i ; i--) {
      rr_hardware_device_t *d = g_ptr_array_index(r->devices, i - 1);

      if (d->owner && d->api && d->api->release) {
         d->api->release(d->data);
      }

      if (d->api && d->api->destroy) {
         d->api->destroy(d->data);
      }
      g_free(d->id);
      g_free(d->kind);
      g_free(d->driver);
      g_free(d->path);
      g_free(d->parent);
      g_free(d->owner);
      g_free(d->error);
      g_free(d);
   }

   g_ptr_array_free(r->devices, true);
   g_free(r);
}

rr_hardware_device_t *rr_hardware_find(const rr_hardware_t *r, const char *id) {
   if (!r || !id) {
      return NULL;
   }

   for (size_t i = 0 ; i < r->devices->len ; i++) {
      rr_hardware_device_t *d = g_ptr_array_index(r->devices, i);

      if (!strcmp(id, d->id)) {
         return d;
      }
   }

   return NULL;
}

rr_hardware_device_t *rr_hardware_add(rr_hardware_t *r, const rr_hardware_spec_t *s) {
   if (!r || !s || !s->id || !*s->id || !s->kind || !*s->kind ||
      !s->driver || !*s->driver || rr_hardware_find(r, s->id) || (s->parent && *s->parent && !rr_hardware_find(r, s->parent))) {
      return NULL;
   }
   rr_hardware_device_t *d = g_new0(rr_hardware_device_t, 1);
   d->id = g_strdup(s->id);
   d->kind = g_strdup(s->kind);
   d->driver = g_strdup(s->driver);
   d->path = g_strdup(s->path ? s->path : "");
   d->parent = g_strdup(s->parent ? s->parent : "");
   d->api = s->api;
   d->data = s->data;
   g_ptr_array_add(r->devices, d);

   return d;
}

size_t rr_hardware_count(const rr_hardware_t *r) {
   return r ? r->devices->len : 0;
}
const rr_hardware_device_t *rr_hardware_at(const rr_hardware_t *r, size_t i) {
   return r && i < r->devices->len ? g_ptr_array_index(r->devices, i) : NULL;
}

#define ACCESSOR(name, member) \
        const char *rr_hardware_ ## name(const rr_hardware_device_t * d) { \
           return d && d->member ? d->member : ""; \
        }
ACCESSOR(id, id)
ACCESSOR(kind, kind)
ACCESSOR(driver, driver)
ACCESSOR(path, path)
ACCESSOR(parent, parent)
ACCESSOR(owner, owner)
ACCESSOR(error, error)
void *rr_hardware_data(const rr_hardware_device_t * d) {
   return d ? d->data : NULL;
}

static bool failed(rr_hardware_device_t *d, int code) {
   if (d) {
      g_free(d->error);
      d->error = g_strdup(strerror(code));
   }
   errno = code;

   return false;
}

bool rr_hardware_claim(rr_hardware_device_t *d, const char *owner) {
   if (!d || !owner || !*owner) {
      return failed(d, EINVAL);
   }

   if (d->owner) {
      return failed(d, EBUSY);
   }

   if (!d->api || !d->api->claim) {
      return failed(d, ENOTSUP);
   }
   errno = 0;

   if (!d->api->claim(d->data, owner)) {
      return failed(d, errno ? errno : EIO);
   }
   d->owner = g_strdup(owner);
   g_clear_pointer(&d->error, g_free);

   return true;
}

bool rr_hardware_release(rr_hardware_device_t *d, const char *owner) {
   if (!d || !owner || !d->owner || strcmp(owner, d->owner)) {
      return failed(d, EPERM);
   }
   errno = 0;

   if (!d->api || !d->api->release || !d->api->release(d->data)) {
      return failed(d, errno ? errno : EIO);
   }
   g_clear_pointer(&d->owner, g_free);
   g_clear_pointer(&d->error, g_free);

   return true;
}
