//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Local bridge supervision and mapping onto the existing radio/property model.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <glib.h>
#include <librrprotocol/bridge.ipc.h>
#include <librrprotocol/bridge.media.h>
#include <librrprotocol/rrprotocol.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <rrserver/media.h>
#include <rrserver/bridge.access.h>
#include <rrserver/rig.rooms.h>
#include <rrserver/database.h>
#include <rrserver/globalstate.h>
#include <rrserver/rig.registry.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/bridge.config.h>
#include <librustyaxe/core.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/bridge.hints.h>
#include <rrserver/cfg.rig.h>
#include <rrserver/rig.properties.h>

struct bridge_child {
   char *name, *driver, *runtime_config;
   bool temporary;
   uint64_t idle_since;
   /* Event-loop-owned hardware demand, never an authorization decision. */
   uint64_t demand_epoch, demand_check_at;
   bool rx_requested;
   rr_backend_t *backend;
   subproc_t process;
   int fd, media_fd;
   unsigned sample_rate;
   char resampler[64];
   bool hello, ready, online, desired, idling;
   uint64_t next_retry, started, last_seen, pending, deadline, next_request, kill_deadline;
   uint64_t desired_hz, requested_hz;
   char desired_mode[16], requested_mode[16];
   unsigned failures;
   int hint_source;
   void (*complete)(rr_control_result_t, void *);
   void *complete_user;
   rrp_bridge_ipc_message_t caps;
   struct bridge_child *next;
};
extern struct GlobalState rig;
static struct bridge_child *children;
static char *config_path;
static bool stopping;
static uint64_t temporary_idle_us = 60000000;
static uint64_t demand_epoch = 1;
#define BRIDGE_DEMAND_RECHECK_US UINT64_C(250000)

/* Coalesce lifecycle changes; active I/O and timeout polling stay unthrottled. */
static void bridge_demand_changed(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)client;
   (void)user;
   demand_epoch++;
}

static void bridge_demand_catalog(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   (void)data;
   (void)length;
   bridge_demand_changed(event, NULL, client, user);
}

/* Explicit unsubscribe currently sends a reply without a media.unsubscribed
 * event. Invalidate on its existing routing hook before the next bridge poll. */
static void bridge_demand_request(const char *event, const void *data, size_t length, rrconn_t *client, void *user) {
   if (length != sizeof(rr_server_io_t)) return;
   const rr_server_io_t *io = data;
   if (!strcmp(dict_get(io->message, "msg.type", ""), "media") &&
       !strcmp(dict_get(io->message, "media.cmd", ""), "unsubscribe")) {
      bridge_demand_changed(event, NULL, client, user);
   }
}

/* Reconcile time-based eligibility as well as explicit changes. The cache holds
 * no client pointers, and media delivery still checks current access per frame. */
static bool bridge_rx_requested(struct bridge_child *bridge, uint64_t current) {
   if (bridge->demand_epoch != demand_epoch || current >= bridge->demand_check_at) {
      bridge->demand_epoch = demand_epoch;
      bridge->rx_requested = rrserver_media_rx_requested(bridge->backend->owner);
      bridge->demand_check_at = current + BRIDGE_DEMAND_RECHECK_US;
   }
   return bridge->rx_requested;
}

static const char *bridge_public_name(const struct bridge_child *b) {
   const char *name = rr_rig_registry_public_alias(rig.rigs, b->backend->owner);

   return name ? name : b->name;
}

static bool capabilities(struct bridge_child *b, const rrp_bridge_ipc_message_t *m);
static void websdr_remove(rr_server_rig_t *radio);
static void capability_source(struct bridge_child *b, const char *source) {
   rr_property_descriptor_t d = {
      .name = "capabilities.source", .type = VAL_STR, .readable = true
   };
   rr_property_descriptor_t existing;

   if (!rr_rig_property_describe(b->backend->owner, d.name, &existing)) {
      rr_rig_property_define(b->backend->owner, &d);
   }
   dict_value_t value = {
      .s = (char *)source
   };
   rr_rig_property_observe(b->backend->owner, d.name, VAL_STR, &value);
}

static const char *metadata_fields[] = {
   "url", "driver", "name", "location", "contact", "antenna", "hardware", "grid", "low_hz", "high_hz", "ext_api", "connected_at", "initiated_by", "requested_at", "directory_name", "directory_location", "directory_country", "directory_contact", "directory_antenna", "directory_hardware", "directory_grid", "directory_users", "directory_users_max", "directory_low_hz", "directory_high_hz", "directory_last_seen", "attached_room"
};
static bool publish_metadata(struct bridge_child *b, const char *key, const char *text, bool save) {
   bool known = false;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(metadata_fields) ; i++) {
      known |= !strcmp(key, metadata_fields[i]);
   }

   if (!known || !text || !*text || strlen(text) > 511) {
      return false;
   }
   char *valid = g_utf8_make_valid(text, -1);

   for (char *p = valid ; *p ; p++) {
      if ((unsigned char)*p < 32 || *p == 127) {
         *p = ' ';
      }
   }

   char name[64];
   snprintf(name, sizeof(name), "receiver.%s", key);
   rr_property_descriptor_t d = {
      .name = name, .type = VAL_STR, .readable = true
   }, existing;

   if (!rr_rig_property_describe(b->backend->owner, name, &existing)) {
      rr_rig_property_define(b->backend->owner, &d);
   }
   dict_value_t value = {
      .s = valid
   };
   rr_rig_property_observe(b->backend->owner, name, VAL_STR, &value);

   if (save && !b->temporary && !rrserver_bridge_metadata_set(b->name, b->driver, key, valid)) {
      Log(LOG_WARN, "bridge", "[%s] unable to cache receiver metadata", bridge_public_name(b));
   }
   g_free(valid);

   return true;
}
static rr_server_vfo_t *receiver(struct bridge_child *b) {
   return rr_server_vfo_find_alias(b->backend->owner, "A");
}
static void message(struct bridge_child *b, rrp_bridge_ipc_message_t *m, uint32_t type) {
   rrp_bridge_ipc_init(m, type);
   snprintf(m->name, sizeof(m->name), "%s", b->name);
   snprintf(m->driver, sizeof(m->driver), "%s", b->driver);
}
static void boolean(rr_server_rig_t *rig, const char *name, bool value) {
   rr_property_descriptor_t d = {
      .name = name, .type = VAL_BOOL, .readable = true
   };
   rr_property_descriptor_t existing;

   if (!rr_rig_property_describe(rig, name, &existing)) {
      rr_rig_property_define(rig, &d);
   }
   dict_value_t v = {
      .i = value
   };
   rr_rig_property_observe(rig, name, VAL_BOOL, &v);
}
static void finish(struct bridge_child *b, rr_control_result_t result) {
   void (*callback)(rr_control_result_t, void *) = b->complete;
   void *user = b->complete_user;
   b->pending = 0;
   b->complete = NULL;
   b->complete_user = NULL;

   if (callback) {
      callback(result, user);
   }
}
static void offline(struct bridge_child *b) {
   if (b->online) {
      capability_source(b, "cached");
   }
   b->online = false;

   if (b->sample_rate) {
      fwdsp_processor_stop(b->resampler);
   }
   b->sample_rate = 0;
   boolean(b->backend->owner, "online", false);
   boolean(b->backend->owner, "rx.available", false);
   rr_server_vfo_t *vfo = receiver(b);

   if (vfo) {
      rr_vfo_property_unavailable(vfo, RR_PROP_VFO_FREQUENCY);
      rr_vfo_property_unavailable(vfo, RR_PROP_VFO_MODE);
   }

   if (b->pending) {
      finish(b, RR_CONTROL_BACKEND_FAILED);
   }
}
static void disconnect(struct bridge_child *b) {
   offline(b);

   if (b->fd >= 0) {
      close(b->fd);
   }
   b->fd = -1;

   if (b->media_fd >= 0) {
      close(b->media_fd);
   }
   b->media_fd = -1;
   b->hello = b->ready = false;

   if (b->process.pid > 0) {
      kill(b->process.pid, SIGTERM);
      b->kill_deadline = mono_us() + 2000000;
   }
}
static bool send_setting(struct bridge_child *b, uint64_t hz, const char *mode, void (*callback) (rr_control_result_t, void *), void *user) {
   if (!b->ready || !b->online || b->pending || hz < b->caps.rx_min_hz || hz > b->caps.rx_max_hz || !rrp_bridge_mode_valid(b->caps.rx_modes, mode)) {
      return false;
   }
   rrp_bridge_ipc_message_t m;
   message(b, &m, RRP_BRIDGE_IPC_SET);
   m.request = ++b->next_request;

   if (!m.request) {
      m.request = ++b->next_request;
   }
   m.frequency_hz = hz;
   snprintf(m.mode, sizeof(m.mode), "%s", mode);

   if (!rrp_bridge_ipc_send(b->fd, &m)) {
      disconnect(b);

      return false;
   }
   b->pending = m.request;
   b->deadline = mono_us() + 5000000;
   b->requested_hz = hz;
   snprintf(b->requested_mode, sizeof(b->requested_mode), "%s", mode);
   b->complete = callback;
   b->complete_user = user;

   return true;
}
static rr_control_result_t control(const rr_control_request_t *r, void *user) {
   struct bridge_child *b = user;

   if (r->rig != b->backend->owner || r->vfo != receiver(b)) {
      return RR_CONTROL_INVALID;
   }

   if (!b->desired || !b->online) {
      return RR_CONTROL_BACKEND_FAILED;
   }
   uint64_t hz = b->desired_hz;
   const char *mode = b->desired_mode;

   if (!strcmp(r->property, RR_PROP_VFO_FREQUENCY) && r->value_type == VAL_LONG) {
      hz = r->value.l;
   } else if (!strcmp(r->property, RR_PROP_VFO_MODE) && r->value_type == VAL_STR) {
      mode = r->value.s;
   } else {
      return RR_CONTROL_UNSUPPORTED;
   }

   return send_setting(b, hz, mode, r->complete, r->complete_user) ? RR_CONTROL_PENDING : RR_CONTROL_BACKEND_FAILED;
}
static bool create(rr_backend_t *backend) {
   const char *name = rr_backend_config_get(backend, "bridge");
   const char *driver = rrp_bridge_config_get(name, "driver");

   if (!name || !driver) {
      return true;
   }
   struct bridge_child *b = calloc(1, sizeof(*b));

   if (!b) {
      return true;
   }
   b->name = strdup(name);
   b->driver = strdup(driver);

   if (!b->name || !b->driver) {
      free(b->name);
      free(b->driver);
      free(b);

      return true;
   }
   b->backend = backend;
   b->process = (subproc_t) SUBPROC_INIT;
   b->fd = b->media_fd = -1;
   static unsigned identity;
   snprintf(b->resampler, sizeof(b->resampler), "proc.bridge-%u", ++identity);
   boolean(backend->owner, "online", false);
   boolean(backend->owner, "rx.available", false);
   boolean(backend->owner, "rx.supported", false);
   boolean(backend->owner, "tx.supported", false);
   // TX availability is deliberately false even for a future TX-capable driver.
   boolean(backend->owner, "tx.available", false);
   rrp_bridge_ipc_message_t hints;
   int source = rrserver_bridge_hints_load(name, driver, &hints);

   if (source < 0) {
      free(b->name);
      free(b->driver);
      free(b);

      return true;
   }
   b->hint_source = source;

   if (source) {
      b->caps = hints;
   }
   capability_source(b, source == 2 ? "configured" : source == 1 ? "cached" : "unknown");
   b->next = children;
   children = b;
   backend->data = b;
   rr_server_rig_set_control_handler(backend->owner, control, b);

   return false;
}
static void destroy(rr_backend_t *backend) {
   struct bridge_child *b = backend->data;

   if (!b) {
      return;
   }
   disconnect(b);
   subproc_stop(&b->process, SIGTERM);
   struct bridge_child **p = &children;
   while (*p && *p != b) {
      p = &(*p)->next;
   }

   if (*p) {
      *p = b->next;
   }

   if (b->runtime_config) {
      unlink(b->runtime_config);
   }
   g_free(b->runtime_config);
   free(b->name);
   free(b->driver);
   free(b);
   backend->data = NULL;
}
static bool supported(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   return rr_server_vfo_owner(vfo) == backend->owner && !strcmp(rr_server_vfo_alias(vfo), "A");
}
static bool set_freq(rr_backend_t *backend, rr_server_vfo_t *vfo, int hz) {
   rr_control_request_t r = {
      .rig = backend->owner, .vfo = vfo, .property = RR_PROP_VFO_FREQUENCY, .value_type = VAL_LONG, .value.l = hz
   };

   return rr_rig_control(&r) != RR_CONTROL_PENDING;
}
static bool set_mode(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_mode_t mode) {
   rr_control_request_t r = {
      .rig = backend->owner, .vfo = vfo, .property = RR_PROP_VFO_MODE, .value_type = VAL_STR, .value.s = vfo_mode_name(mode)
   };

   return rr_rig_control(&r) != RR_CONTROL_PENDING;
}
static float get_freq(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   rr_property_snapshot_t s;

   return supported(backend, vfo) && rr_vfo_property_read(vfo, RR_PROP_VFO_FREQUENCY, &s) && s.known && s.available ? s.value.l : 0;
}
static rr_mode_t get_mode(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   rr_property_snapshot_t s;

   return supported(backend, vfo) && rr_vfo_property_read(vfo, RR_PROP_VFO_MODE, &s) && s.known && s.available ? vfo_parse_mode(s.value.s) : MODE_NONE;
}
static const char *get_mode_string(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   return vfo_mode_name(get_mode(backend, vfo));
}
static rr_vfo_data_t *poll_state(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   if (!supported(backend, vfo)) {
      return NULL;
   }
   rr_vfo_data_t *state = calloc(1, sizeof(*state));

   if (!state) {
      return NULL;
   }
   rr_property_snapshot_t s;

   if (rr_vfo_property_read(vfo, RR_PROP_VFO_FREQUENCY, &s) && s.known && s.available) {
      state->freq = s.value.l;
   }
   state->mode = get_mode(backend, vfo);
   state->id = VFO_A;

   return state;
}

static const rr_backend_funcs_t api = {
   .create = create, .destroy = destroy, .vfo_supported = supported, .freq_set = set_freq, .mode_set = set_mode,
   .freq_get = get_freq, .mode_get = get_mode, .mode_get_str = get_mode_string, .poll_state = poll_state
};

static const rr_backend_type_t type = {
   .name = "bridge", .description = "Isolated SDR bridge", .uses_property_state = true, .event_driven = true, .api = &api
};
const rr_backend_type_t *rrserver_bridge_backend_type(void) {
   return &type;
}

// Expand the named bridge roster into ordinary configured rigs before registry
// initialization, so UUID persistence, rooms and client discovery stay generic.
bool rrserver_bridges_prepare(void) {
   const char *roster = cfg_get("bridge.instances");

   if (!roster || !*roster) {
      return true;
   }
   gchar **names = g_strsplit_set(roster, " ,\t\r\n", -1);
   GString *rigs = g_string_new(cfg_get("rig.instances") ? cfg_get("rig.instances") : "");
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   gchar **existing = g_strsplit_set(rigs->str, " ,\t\r\n", -1);

   for (int i = 0 ; existing[i] ; i++) {
      if (*existing[i]) {
         g_hash_table_add(seen, g_strdup(existing[i]));
      }
   }

   g_strfreev(existing);
   bool ok = true;

   for (int i = 0 ; names[i] && ok ; i++) {
      const char *name = names[i];

      if (!*name) {
         continue;
      }
      const char *enabled = rrp_bridge_config_get(name, "enabled");

      if (enabled && (!strcasecmp(enabled, "false") || !strcmp(enabled, "0"))) {
         continue;
      }

      if (enabled && strcasecmp(enabled, "true") && strcmp(enabled, "1")) {
         ok = false;
         break;
      }
      const char *alias = rrp_bridge_config_get(name, "rig");

      if (!alias || !*alias) {
         alias = name;
      }
      const char *driver = rrp_bridge_config_get(name, "driver");

      if (!rr_rig_config_alias_valid(name) || strlen(name) >= 64 || !rr_rig_config_alias_valid(alias) || strlen(alias) >= 64 ||
         !rr_rig_config_alias_valid(driver) || strlen(driver) >= 64 || g_hash_table_contains(seen, alias)) {
         ok = false;
         break;
      }
      // Do not silently replace a physical rig with a bridge.
      const char *configured = rr_rig_config_get(alias, "backend");

      if (configured && *configured) {
         ok = false;
         break;
      }
      g_hash_table_add(seen, g_strdup(alias));
      char key[256];
      snprintf(key, sizeof(key), "rig:%s.backend", alias);
      dict_add(cfg, key, "bridge");
      snprintf(key, sizeof(key), "rig:%s.bridge", alias);
      dict_add(cfg, key, name);
      snprintf(key, sizeof(key), "rig:%s.vfos", alias);
      dict_add(cfg, key, "A");
      g_string_append_printf(rigs, " %s", alias);
   }

   if (ok) {
      ok = dict_add(cfg, "rig.instances", rigs->str) == 0;
   } else {
      Log(LOG_CRIT, "bridge", "Invalid bridge roster or conflicting rig alias");
   }
   g_string_free(rigs, true);
   g_strfreev(names);
   g_hash_table_destroy(seen);

   return ok;
}
static void schedule_retry(struct bridge_child *b) {
   unsigned delay = 1U << (b->failures < 6 ? b->failures : 6);

   if (b->failures < 6) {
      b->failures++;
   }
   b->next_retry = mono_us() + (uint64_t) delay * 1000000;
}
static char *bridge_program_path(void) {
   const char *configured = cfg_get("bridge.path");

   // Accept the original shipped relative default as automatic discovery too.
   if (configured && *configured && strcmp(configured, "auto") && strcmp(configured, "bin/rrbridge")) {
      return g_find_program_in_path(configured);
   }
   char *path = g_find_program_in_path("rrbridge");

   return path ? path : g_find_program_in_path("./bin/rrbridge");
}
static bool spawn_child(struct bridge_child *b) {
   char *path = bridge_program_path();

   if (!path) {
      Log(LOG_WARN, "bridge", "[%s] no executable rrbridge found (bridge.path, PATH, or ./bin/rrbridge)", bridge_public_name(b));

      return false;
   }
   const char *argv[] = {
      path, "--config", b->runtime_config ? b->runtime_config : config_path, "--bridge", b->name, NULL
   };
   int sv[2], media[2];

   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) {
      g_free(path);

      return false;
   }

   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, media)) {
      close(sv[0]);
      close(sv[1]);
      g_free(path);

      return false;
   }
   int capacity = 16384;
   setsockopt(media[1], SOL_SOCKET, SO_SNDBUF, &capacity, sizeof(capacity));
   subproc_fd_t map[] = { {
                             sv[1], RRP_BRIDGE_IPC_FD
                          }, {
                             media[1], RRP_BRIDGE_MEDIA_FD
                          } };
   int closes[] = {
      sv[0], sv[1], media[0], media[1]
   };
   bool ok = subproc_spawn_fds(&b->process, path, argv, map, 2, closes, 4);
   close(sv[1]);
   close(media[1]);
   g_free(path);

   if (!ok) {
      close(sv[0]);
      close(media[0]);

      return false;
   }
   b->fd = sv[0];
   b->media_fd = media[0];
   b->started = b->last_seen = mono_us();
   b->kill_deadline = 0;
   b->hello = b->ready = b->idling = false;
   Log(LOG_INFO, "bridge", "Spawned [%s] pid=%ld", bridge_public_name(b), (long)b->process.pid);

   return true;
}
bool rrserver_bridges_start(const char *file) {
   if (config_path) {
      return false;
   }
   stopping = false;
   int idle = cfg_get_int("websdr.idle-timeout", 60);
   temporary_idle_us = (uint64_t) CLAMP(idle, 1, 3600) * 1000000;

   if (!file || !(config_path = strdup(file))) {
      return false;
   }

   // Backend creation precedes VFO creation. Apply descriptions only once the
   // ordinary rig/VFO schemas exist, before servicing client discovery.
   for (struct bridge_child *b = children ; b ; b = b->next) {
      if (b->hint_source && !capabilities(b, &b->caps)) {
         return false;
      }

      for (unsigned i = 0 ; i < G_N_ELEMENTS(metadata_fields) ; i++) {
         char *value = rrserver_bridge_metadata_get(b->name, b->driver, metadata_fields[i]);

         if (value) {
            publish_metadata(b, metadata_fields[i], value, false);
         }
         g_free(value);
      }

      b->ready = false;         // Hints never satisfy the process handshake.
   }

   return true;
}
static bool capabilities(struct bridge_child *b, const rrp_bridge_ipc_message_t *m) {
   if (!(m->flags & RRP_BRIDGE_RX) || m->rx_max_hz > LONG_MAX) {
      return false;
   }
   // Generic rig modes use canonical names understood by existing clients.
   gchar **modes = g_strsplit(m->rx_modes, " ", -1);
   bool valid = true;
   unsigned count = 0;

   for (int i = 0 ; modes[i] ; i++) {
      if (*modes[i]) {
         rr_mode_t parsed = vfo_parse_mode(modes[i]);

         if (parsed == MODE_NONE || strlen(modes[i]) >= 16 || strcmp(modes[i], vfo_mode_name(parsed))) {
            valid = false;
         }
         count++;
      }
   }

   valid = valid && count;
   g_strfreev(modes);

   if (!valid) {
      return false;
   }
   rr_server_vfo_t *vfo = receiver(b);
   rr_property_descriptor_t d = {
      .name = RR_PROP_VFO_FREQUENCY, .type = VAL_LONG, .readable = true, .writable = true, .unit = "Hz",
      .has_min = true, .has_max = true, .has_step = true, .minimum.l = m->rx_min_hz, .maximum.l = m->rx_max_hz, .step.l = 1
   };

   if (rr_vfo_property_redefine(vfo, &d)) {
      return false;
   }
   d = (rr_property_descriptor_t) {
      .name = RR_PROP_VFO_MODE, .type = VAL_STR, .readable = true, .writable = true, .enum_values = m->rx_modes
   };

   if (rr_vfo_property_redefine(vfo, &d)) {
      return false;
   }
   b->caps = *m;
   b->ready = true;
   boolean(b->backend->owner, "rx.supported", true);
   boolean(b->backend->owner, "tx.supported", !!(m->flags & RRP_BRIDGE_TX));
   boolean(b->backend->owner, "rx.audio.supported", !!(m->flags & RRP_BRIDGE_RX_AUDIO));
   boolean(b->backend->owner, "rx.iq.supported", !!(m->flags & RRP_BRIDGE_RX_IQ));
   boolean(b->backend->owner, "rx.waterfall.supported", !!(m->flags & RRP_BRIDGE_WATERFALL));

   return true;
}
static bool handle(struct bridge_child *b, const rrp_bridge_ipc_message_t *m) {
   if (strcmp(m->name, b->name) || strcmp(m->driver, b->driver)) {
      return false;
   }

   if (!b->hello) {
      if (m->type != RRP_BRIDGE_IPC_HELLO) {
         return false;
      }
      b->hello = true;

      return true;
   }

   if (!b->ready) {
      bool ok = m->type == RRP_BRIDGE_IPC_CAPABILITIES && capabilities(b, m);

      if (ok) {
         capability_source(b, "driver");
      }

      return ok;
   }

   if (m->type == RRP_BRIDGE_IPC_METADATA) {
      return publish_metadata(b, m->metadata_key, m->metadata_value, true);
   }

   if (m->type == RRP_BRIDGE_IPC_HEARTBEAT) {
      return true;
   }

   if (m->type == RRP_BRIDGE_IPC_CAPABILITIES) {
      if (!capabilities(b, m)) {
         return false;
      }

      if (b->online && !b->temporary && !rrserver_bridge_hints_save(b->name, b->driver, &b->caps)) {
         Log(LOG_WARN, "bridge", "[%s] unable to refresh capability cache", bridge_public_name(b));
      }

      return true;
   }

   if (m->type == RRP_BRIDGE_IPC_STATE) {
      if (!m->available) {
         offline(b);

         return true;
      }

      if (m->frequency_hz < b->caps.rx_min_hz || m->frequency_hz > b->caps.rx_max_hz || !rrp_bridge_mode_valid(b->caps.rx_modes, m->mode)) {
         return false;
      }
      bool was_online = b->online;
      b->online = true;

      if (!was_online) {
         char stamp[32];
         snprintf(stamp, sizeof(stamp), "%lld", (long long)now);
         publish_metadata(b, "connected_at", stamp, true);
         capability_source(b, "live");

         if (!b->temporary && !rrserver_bridge_hints_save(b->name, b->driver, &b->caps)) {
            Log(LOG_WARN, "bridge", "[%s] unable to cache capability hints", bridge_public_name(b));
         }
      }
      boolean(b->backend->owner, "online", true);
      boolean(b->backend->owner, "rx.available", true);
      dict_value_t value = {
         .l = m->frequency_hz
      };
      rr_vfo_property_observe(receiver(b), RR_PROP_VFO_FREQUENCY, VAL_LONG, &value);
      value.s = m->mode;
      rr_vfo_property_observe(receiver(b), RR_PROP_VFO_MODE, VAL_STR, &value);

      if (!b->desired) {
         b->desired = true;
         b->desired_hz = m->frequency_hz;
         snprintf(b->desired_mode, sizeof(b->desired_mode), "%s", m->mode);
      } else if (!was_online && !b->pending && !send_setting(b, b->desired_hz, b->desired_mode, NULL, NULL)) {
         return false;
      }

      return true;
   }

   if (m->type == RRP_BRIDGE_IPC_RESULT && m->request == b->pending) {
      if (m->success) {
         b->desired_hz = b->requested_hz;
         snprintf(b->desired_mode, sizeof(b->desired_mode), "%s", b->requested_mode);
      }
      finish(b, m->success ? RR_CONTROL_OK : RR_CONTROL_BACKEND_FAILED);

      return true;
   }

   return false;
}
static void decoded_pcm(const char *name, const void *samples, size_t length, void *user) {
   (void)name;
   struct bridge_child *b = user;

   if (b->online) {
      rrserver_media_rx_pcm(b->backend->owner, samples, length);
   }
}
static void read_media(struct bridge_child *b) {
   for (int i = 0 ; i < 8 && b->media_fd >= 0 ; i++) {
      struct rrp_bridge_pcm frame;
      int got = rrp_bridge_media_recv(b->media_fd, &frame);

      if (!got) {
         break;
      }

      if (got < 0) {
         disconnect(b);
         break;
      }

      if (!b->online || !(b->caps.flags & RRP_BRIDGE_RX_AUDIO)) {
         continue;
      }

      if (b->sample_rate != frame.rate) {
         if (b->sample_rate) {
            fwdsp_processor_stop(b->resampler);
         }
         b->sample_rate = 0;
      }

      if (!b->sample_rate) {
         if (!fwdsp_audio_resampler_start(b->resampler, frame.rate, decoded_pcm, b)) {
            continue;
         }
         b->sample_rate = frame.rate;
      }
      uint8_t pcm[2 * RRP_BRIDGE_MEDIA_SAMPLES];

      for (size_t j = 0 ; j < frame.count ; j++) {
         pcm[2 * j] = (uint16_t) frame.samples[j];
         pcm[2 * j + 1] = (uint16_t) frame.samples[j] >> 8;
      }

      if (!fwdsp_processor_write(b->resampler, pcm, frame.count * 2)) {
         b->sample_rate = 0;
      }
   }
}
void rrserver_bridges_poll(void) {
   if (stopping || !config_path) {
      return;
   }
   uint64_t current = mono_us();

   for (struct bridge_child *b = children ; b ; b = b->next) {
      bool wanted = bridge_rx_requested(b, current);

      if (wanted) {
         b->idle_since = 0;
      } else if (!b->idle_since) {
         b->idle_since = current;
      }

      if (!wanted && !b->idling) {
         b->idling = true;
         b->failures = 0;
         b->next_retry = 0;

         if (b->fd >= 0) {
            rrp_bridge_ipc_message_t bye;
            message(b, &bye, RRP_BRIDGE_IPC_SHUTDOWN);
            (void)rrp_bridge_ipc_send(b->fd, &bye);
            offline(b);
            b->kill_deadline = current + 2000000;
            Log(LOG_INFO, "bridge", "[%s] receiver no longer requested; releasing connection", bridge_public_name(b));
         }
      }

      if (b->idling && b->fd >= 0 && b->process.pid > 0 && current >= b->kill_deadline) {
         disconnect(b);
      }

      if (b->fd >= 0) {
         for (int i = 0 ; i < 8 ; i++) {
            rrp_bridge_ipc_message_t m;
            int got = rrp_bridge_ipc_recv(b->fd, &m);

            if (!got) {
               break;
            }

            if (got > 0 && b->idling) {
               continue;
            }

            if (got < 0 || !handle(b, &m)) {
               if (!b->idling) {
                  Log(LOG_WARN, "bridge", "[%s] disconnected or invalid control record", bridge_public_name(b));
               }
               disconnect(b);
               break;
            }
            b->last_seen = current;
         }

         if ((b->pending && current >= b->deadline) || (!b->ready && current - b->started >= 5000000) || current - b->last_seen >= 10000000) {
            disconnect(b);
         }
      }

      if (b->fd < 0 && b->process.pid > 0 && current >= b->kill_deadline) {
         kill(b->process.pid, SIGKILL);
      }
      int status;

      if (b->process.pid > 0 && subproc_reap(&b->process, &status)) {
         disconnect(b);
         subproc_close(&b->process);

         if (wanted && !b->idling) {
            schedule_retry(b);
         } else {
            b->next_retry = 0;
         }
      }

      if (b->ready && b->media_fd >= 0) {
         read_media(b);
      }

      // Reset backoff only after a stable session, never on a crash loop.
      if (b->ready && current - b->started >= 30000000) {
         b->failures = 0;
      }

      if (wanted && b->process.pid <= 0 && current >= b->next_retry) {
         if (!spawn_child(b)) {
            schedule_retry(b);
         }
      }
   }

   /* One removal per poll keeps iteration and backend ownership straightforward. */
   for (struct bridge_child *b = children ; b ; b = b->next) {
      if (b->temporary && b->idle_since && current - b->idle_since >= temporary_idle_us) {
         websdr_remove(b->backend->owner);
         break;
      }
   }
}
void rrserver_bridges_stop(void) {
   stopping = true;

   for (struct bridge_child *b = children ; b ; b = b->next) {
      if (b->fd >= 0) {
         rrp_bridge_ipc_message_t bye;
         message(b, &bye, RRP_BRIDGE_IPC_SHUTDOWN);
         (void)rrp_bridge_ipc_send(b->fd, &bye);
      }
   }

   // Allow drivers to close their remote sessions before signal escalation.
   uint64_t deadline = mono_us() + 200000;
   bool running;
   do {
      running = false;

      for (struct bridge_child *b = children ; b ; b = b->next) {
         int status;

         if (b->process.pid > 0 && !subproc_reap(&b->process, &status)) {
            running = true;
         }
      }

      if (running) {
         poll(NULL, 0, 5);
      }
   } while (running && mono_us() < deadline);

   for (struct bridge_child *b = children ; b ; b = b->next) {
      disconnect(b);
      subproc_stop(&b->process, SIGTERM);
   }

   free(config_path);
   config_path = NULL;
}

bool rrserver_bridge_temporary(const rr_server_rig_t *radio) {
   rr_backend_t *backend = radio ? rr_server_rig_backend(radio) : NULL;

   if (!backend || backend->type != &type || !backend->data) {
      return false;
   }

   return ((struct bridge_child *)backend->data)->temporary;
}

/* The first supported endpoint provider is Kiwi. Keep request policy and lifecycle generic so further bridge drivers can supply endpoint parsers. */
static bool websdr_endpoint(const char *driver, const char *url, char **host, unsigned *port) {
   if (!driver || strcmp(driver, "kiwi") || !url || strlen(url) > 512) {
      return false;
   }
   GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);

   if (!uri) {
      return false;
   }
   const char *name = g_uri_get_host(uri), *scheme = g_uri_get_scheme(uri), *path = g_uri_get_path(uri);
   bool ok = scheme && !strcmp(scheme, "http") && name && *name && strlen(name) <= 253 &&
      !g_uri_get_userinfo(uri) && !g_uri_get_query(uri) && !g_uri_get_fragment(uri) && (!path || !*path || !strcmp(path, "/"));

   for (const char *p = name ; ok && p && *p ; p++) {
      ok = g_ascii_isalnum(*p) || *p == '.' || *p == '-';
   }

   int number = g_uri_get_port(uri);

   if (number == -1) {
      number = 80;
   }
   ok = ok && number > 0 && number <= 65535;

   if (ok) {
      *host = g_ascii_strdown(name, -1);
      *port = number;
   }
   g_uri_unref(uri);

   return ok;
}
static struct bridge_child *websdr_find(const char *driver, const char *host, unsigned port) {
   for (struct bridge_child *b = children ; b ; b = b->next) {
      const char *name = rrp_bridge_config_get(b->name, "host"), *number = rrp_bridge_config_get(b->name, "port");

      if (!name) {
         name = cfg_get("bridge.host");
      }

      if (!number) {
         number = cfg_get("bridge.port");
      }
      unsigned configured = number ? strtoul(number, NULL, 10) : 8073;

      if (!strcmp(b->driver, driver) && name && !g_ascii_strcasecmp(name, host) && configured == port) {
         return b;
      }
   }

   return NULL;
}
static void runtime_setting(const char *name, const char *field, const char *value) {
   char key[160];
   snprintf(key, sizeof(key), "bridge:%s.%s", name, field);
   dict_add(cfg, key, value);
}
static void websdr_remove(rr_server_rig_t *radio) {
   char uuid[64], alias[64];
   snprintf(uuid, sizeof(uuid), "%s", rr_server_rig_id(radio));
   snprintf(alias, sizeof(alias), "%s", rr_rig_registry_alias(rig.rigs, radio));

   for (unsigned i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] && !strcmp(media_channels[i].rig_uuid, uuid)) {
         media_send_chan_removed_all(&media_channels[i]);

         if (media_channels[i].codec[0]) {
            fwdsp_codec_stop_channel_immediate(media_channels[i].codec, true, media_channels[i].uuid);
         }
         media_chan_remove(media_channels[i].uuid);
      }
   }

   const char *room = rr_rig_registry_room(rig.rigs, radio);

   if (room) {
      ws_room_set_vfo_mask(room, 0);
#ifdef USE_SQLITE
      /* /LIST reads persistent rooms, not the live rig registry. Retire
       * temporary receiver rooms when their backend is reaped, but keep
       * the room history and VFO metadata for normal persistence rules. */
      bool exists = false, deleted = false;
      if (db_room_status(masterdb, room, &exists, &deleted)) {
         if (exists && !deleted && !db_room_delete(masterdb, room, "server", false, false))
            Log(LOG_WARN, "bridge", "Cannot retire temporary WebSDR room %s from /LIST", room);
      } else {
         Log(LOG_WARN, "bridge", "Cannot inspect temporary WebSDR room %s for cleanup", room);
      }
#endif
   }
   rr_rig_registry_remove(rig.rigs, uuid);
   const char *fields[] = {
      "driver", "host", "port"
   };
   char key[160];

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) ; i++) {
      snprintf(key, sizeof(key), "bridge:%s.%s", alias, fields[i]);
      dict_del(cfg, key);
   }

   snprintf(key, sizeof(key), "rig:%s.bridge", alias);
   dict_del(cfg, key);
}
static struct bridge_child *websdr_create(const char *driver, const char *host, unsigned port) {
   unsigned temporary = 0;

   for (struct bridge_child *b = children ; b ; b = b->next) {
      temporary += b->temporary;
   }

   int limit = cfg_get_int("websdr.max-temporary", 8);

   if (limit <= 0 || temporary >= (unsigned)limit || !config_path || stopping) {
      return NULL;
   }
   char *uuid = g_uuid_string_random(), *vfo_uuid = g_uuid_string_random();
   char alias[64], number[16], key[160];
   snprintf(alias, sizeof(alias), "websdr-%.16s", uuid);
   snprintf(number, sizeof(number), "%u", port);
   snprintf(key, sizeof(key), "rig:%s.bridge", alias);
   dict_add(cfg, key, alias);
   runtime_setting(alias, "driver", driver);
   runtime_setting(alias, "host", host);
   runtime_setting(alias, "port", number);
   rr_server_rig_t *radio = rr_rig_registry_add(rig.rigs, uuid, alias, host, &type);

   if (!radio) {
      g_free(uuid);
      g_free(vfo_uuid);

      return NULL;
   }
   struct bridge_child *b = rr_server_rig_backend(radio)->data;
   b->temporary = true;
   b->idle_since = mono_us();
   int fd = g_file_open_tmp("rustyrig-websdr-XXXXXX", &b->runtime_config, NULL);
   const char *modules = cfg_get("bridge.modules");
   char *contents = g_strdup_printf("[bridge]\nmodules = %s\n[bridge:%s]\ndriver = %s\nhost = %s\nport = %u\n", modules ? modules : "auto", alias, driver, host,
      port);
   bool file_ok = fd >= 0 && write(fd, contents, strlen(contents)) == (ssize_t) strlen(contents);

   if (fd >= 0) {
      close(fd);
   }
   g_free(contents);
   bool ok = file_ok && rr_server_vfo_add(radio, vfo_uuid, "A", "A", RR_VFO_PERSISTENT) &&
      !rr_rig_define_vfo_properties(radio, 'A') && !rrserver_rig_room_add(radio) && !rrserver_media_add_rig(radio);

   if (!ok) {
      websdr_remove(radio);
      b = NULL;
   } else {
      event_emit("object.model.added", NULL, uuid);
      b->ready = false;
   }
   g_free(uuid);
   g_free(vfo_uuid);

   return b;
}
static bool websdr_save(struct bridge_child *b) {
   if (!b->temporary) {
      return true;
   }
   char *old = NULL;
   gsize length;

   if (!config_path || !g_file_get_contents(config_path, &old, &length, NULL)) {
      return false;
   }
   const char *host = rrp_bridge_config_get(b->name, "host"), *port = rrp_bridge_config_get(b->name, "port");
   const char *roster = cfg_get("bridge.instances");
   char *names = g_strdup_printf("%s %s", roster ? roster : "", b->name);
   char *updated =
      g_strdup_printf("%s\n[bridge]\ninstances = %s\n[bridge:%s]\ndriver = %s\nhost = %s\nport = %s\n", old, names, b->name, b->driver, host, port);
   bool ok = g_file_set_contents(config_path, updated, -1, NULL);

   if (ok) {
      dict_add(cfg, "bridge.instances", names);
      b->temporary = false;

      if (b->online) {
         rrserver_bridge_hints_save(b->name, b->driver, &b->caps);
      }

      for (unsigned i = 0 ; i < G_N_ELEMENTS(metadata_fields) ; i++) {
         char property[64];
         snprintf(property, sizeof(property), "receiver.%s", metadata_fields[i]);
         rr_property_snapshot_t value;

         if (rr_rig_property_read(b->backend->owner, property, &value) && value.known && value.value_type == VAL_STR) {
            rrserver_bridge_metadata_set(b->name, b->driver, metadata_fields[i], value.value.s);
         }
      }

      if (b->runtime_config) {
         unlink(b->runtime_config);
      }
      g_clear_pointer(&b->runtime_config, g_free);
   }
   g_free(updated);
   g_free(names);
   g_free(old);

   return ok;
}
static void websdr_request(const char *event, const void *data, size_t length, rrconn_t *client, void *unused) {
   (void)event;
   (void)unused;

   if (length != sizeof(rr_server_io_t) || !client) {
      return;
   }
   rr_server_io_t *io = (rr_server_io_t *) data;

   if (io->handled || strcmp(dict_get(io->message, "msg.type", ""), "talk") || strcmp(dict_get(io->message, "talk.cmd", ""), "admin")) {
      return;
   }
   const char *action = dict_get(io->message, "talk.action", "");
   bool save = !strcmp(action, "websdr-add");

   if (!save && strcmp(action, "websdr-connect")) {
      return;
   }
   io->handled = io->result = true;
   dict *request = json2dict(dict_get(io->message, "talk.data", ""));
   const char *error = "Invalid or unsupported WebSDR endpoint";
   struct bridge_child *b = NULL;
   char *host = NULL;
   unsigned port;
   const char *driver = request ? dict_get(request, "driver", "") : "";

   if (!client->authenticated || !client->user || (save ? !rrserver_websdr_can_admin(client) : !rrserver_bridge_can_use(client))) {
      error = save ? "Adding receivers requires websdr.admin" : "WebSDR access requires websdr.listen or websdr.connect";
   } else if (request && websdr_endpoint(driver, dict_get(request, "url", ""), &host, &port)) {
      b = websdr_find(driver, host, port);

      if (!save && (!b || b->temporary) && !rrserver_websdr_can_connect(client)) {
         b = NULL;
         error = "This receiver is not configured; websdr.connect is required";
      } else if (!b && !(b = websdr_create(driver, host, port))) {
         error = "Unable to create receiver (temporary rig limit or server configuration)";
      }

      if (b && save && !websdr_save(b)) {
         b = NULL;
         error = "Unable to save server configuration";
      }
   }
   const char *selected_room = request ? dict_get(request, "room", "") : "";
   const char *target_room = b ? rr_rig_registry_room(rig.rigs, b->backend->owner) : "";
#ifdef USE_SQLITE
   if (b && !save && selected_room && *selected_room) {
      bool exists = false, deleted = false;
      /* Users may select rooms they already joined, or create ordinary
       * unscoped rooms.  Never create someone else's station namespace. */
      bool valid = ws_room_name_valid(selected_room) &&
         db_room_status(masterdb, selected_room, &exists, &deleted) && !deleted &&
         (exists ? ws_client_in_room(client, selected_room) :
          (!ws_room_station_scoped(selected_room)));
      rr_server_vfo_t *vfo = rr_server_vfo_find_alias(b->backend->owner, "A");
      if (!valid || !vfo) {
         error = "Room must be joined or be a valid new unscoped room";
         b = NULL;
      } else {
         /* Persist mapping and temporary lifecycle metadata atomically. */
         bool generated = request && dict_get_bool(request, "room.autogenerated", false) &&
            g_str_has_prefix(selected_room, "#sdr-") &&
            strlen(selected_room) == strlen("#sdr-") + 8;
         bool committed = sqlite3_exec(masterdb, "SAVEPOINT websdr_attach;", NULL, NULL, NULL) == SQLITE_OK;
         bool ok = committed && db_room_ensure(masterdb, selected_room, true, 0, client->chatname) &&
            db_room_vfo_add(masterdb, selected_room, rr_server_vfo_id(vfo)) &&
            (!generated || db_room_mark_autogenerated(masterdb, selected_room));
         if (committed) {
            if (!ok) sqlite3_exec(masterdb, "ROLLBACK TO websdr_attach;", NULL, NULL, NULL);
            committed = sqlite3_exec(masterdb, "RELEASE websdr_attach;", NULL, NULL, NULL) == SQLITE_OK;
         }
         if (!ok || !committed) {
            error = "Unable to attach receiver to selected room";
            b = NULL;
         }
      }
      if (b) {
         /* Temporary receivers reserve their generated base room only as a
          * compatibility address. The chosen room is the first live tuning
          * attachment; secondary rooms can add receive-only references. */
         if (rrserver_bridge_temporary(b->backend->owner)) {
            db_room_vfo_remove(masterdb, target_room, rr_server_vfo_id(vfo));
         }
         target_room = selected_room;
         /* Legacy media channels belong to the generated backend room.
          * Move their RX admission scope to the user's selected room.
          * Keep TX untouched (bridges never provision TX channels). */
         const char *rig_uuid = rr_server_rig_id(b->backend->owner);
         unsigned moved = 0;
         for (unsigned i = 0; i < MAX_MEDIA_CHANNELS; i++) {
            struct rr_mediachan *channel = &media_channels[i];
            if (channel->uuid[0] && !strcmp(channel->rig_uuid, rig_uuid) &&
                channel->direction == RR_BINFRAME_DIR_RX) {
               snprintf(channel->room, sizeof(channel->room), "%s", selected_room);
               moved++;
            }
         }
         Log(LOG_INFO, "bridge", "[%s] attached receiver room %s (%u RX channels)",
            bridge_public_name(b), selected_room, moved);
         /* The room JOIN preceded this attachment. Notify its members of
          * the newly bound receiver UUID so their VFO panels refresh. */
         char *vfos = db_room_vfo_list(masterdb, selected_room);
         dict *update = dict_new();
         if (update) {
            dict_add(update, "msg.type", "talk");
            dict_add(update, "talk.cmd", "room-vfo");
            dict_add(update, "talk.room", selected_room);
            dict_add(update, "talk.vfos", vfos ? vfos : "");
            dict_add_bool(update, "room.has-vfos", vfos && *vfos);
            dict_add_ulong(update, "room.vfo-mask", 0);
            dict_add_bool(update, "room.tx-control", false);
            dict_add_bool(update, "room.rx-tunable", true);
            ws_broadcast_room_dict(NULL, update, selected_room);
            dict_free(update);
         }
         free(vfos);
         for (rrconn_t *member = http_client_list; member; member = member->next) {
            if (member->authenticated && ws_client_in_room(member, selected_room))
               media_send_available_all(member);
         }
      }
   }
#endif
   if (b && !save) {
      /* Initiator and time are authoritative server observations.  Directory
       * values are client-provided hints, deliberately namespaced. */
      publish_metadata(b, "initiated_by", client->chatname, false);
      publish_metadata(b, "driver", b->driver, false);
      const char *endpoint = request ? dict_get(request, "url", NULL) : NULL;
      if (endpoint && *endpoint) publish_metadata(b, "url", endpoint, false);
      publish_metadata(b, "attached_room", target_room, false);
      char timestamp[40];
      snprintf(timestamp, sizeof(timestamp), "%lld", (long long)time(NULL));
      publish_metadata(b, "requested_at", timestamp, false);
      const char *directory_keys[] = {
         "name", "location", "country", "contact", "antenna", "hardware",
         "grid", "users", "users_max", "low_hz", "high_hz", "last_seen"
      };
      for (unsigned i = 0; i < G_N_ELEMENTS(directory_keys); i++) {
         char source_key[64], property_key[64];
         snprintf(source_key, sizeof(source_key), "directory.%s", directory_keys[i]);
         snprintf(property_key, sizeof(property_key), "directory_%s", directory_keys[i]);
         const char *value = request ? dict_get(request, source_key, NULL) : NULL;
         if (value && *value) {
            publish_metadata(b, property_key, value, false);
         }
      }
   }
   dict *result = dict_new(), *reply = dict_new();
   dict_add(result, "request", request ? dict_get(request, "request", "") : "");
   dict_add(result, "room", b ? target_room : "");
   dict_add(result, "error", b ? "" : error);
   char *json = dict2json(result);
   dict_add(reply, "msg.type", "talk");
   dict_add(reply, "talk.cmd", "admin");
   dict_add(reply, "talk.action", "websdr-result");
   dict_add(reply, "talk.target", "websdr");
   dict_add(reply, "talk.data", json);
   ws_send_dict(NULL, client, reply, WEBSOCKET_OP_TEXT);
   free(json);
   dict_free(result);
   dict_free(reply);
   dict_free(request);
   g_free(host);
}
void rrserver_websdr_register_events(void) {
   static const char *demand_events[] = {
      "room.join", "room.part", "room.add", "room.delete", "room.vfo",
      "authorized", "protocol.session.closed", "protocol.object.close", "serial.session.closed",
      "media.subscribed", "media.unsubscribed", "media.codec-select",
      "authdb.load", "user.cmd", "admin.cmd", "rehash", "server.link.ready", "server.link.lost"
   };
   for (size_t i = 0; i < G_N_ELEMENTS(demand_events); i++) {
      event_on(demand_events[i], bridge_demand_changed, NULL);
   }
   event_on_binary(RR_MEDIA_CATALOG_EVENT, bridge_demand_catalog, NULL);
   event_on_binary(RR_SERVER_ROUTE_EVENT, bridge_demand_request, NULL);
   event_on_binary(RR_SERVER_ROUTE_EVENT, websdr_request, NULL);
}
