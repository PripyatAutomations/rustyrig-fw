// Local bridge supervision and mapping onto the existing radio/property model.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <glib.h>
#include <librrprotocol/bridge.ipc.h>
#include <librrprotocol/bridge.config.h>
#include <librustyaxe/core.h>
#include <rrserver/bridge.manager.h>
#include <rrserver/cfg.rig.h>
#include <rrserver/rig.properties.h>

struct bridge_child {
   char *name, *driver;
   rr_backend_t *backend;
   subproc_t process;
   int fd;
   bool hello, ready, online, desired;
   uint64_t next_retry, started, last_seen, pending, deadline, next_request, kill_deadline;
   uint64_t desired_hz, requested_hz;
   char desired_mode[16], requested_mode[16];
   unsigned failures;
   void (*complete)(rr_control_result_t, void *);
   void *complete_user;
   rrp_bridge_ipc_message_t caps;
   struct bridge_child *next;
};
static struct bridge_child *children;
static char *config_path;
static bool stopping;
static rr_server_vfo_t *receiver(struct bridge_child *b) {
   return rr_server_vfo_find_alias(b->backend->owner, "A");
}
static void message(struct bridge_child *b, rrp_bridge_ipc_message_t *m, uint32_t type) {
   rrp_bridge_ipc_init(m, type);
   snprintf(m->name, sizeof(m->name), "%s", b->name);
   snprintf(m->driver, sizeof(m->driver), "%s", b->driver);
}
static void boolean(rr_server_rig_t *rig, const char *name, bool value) {
   rr_property_descriptor_t d = {.name = name, .type = VAL_BOOL, .readable = true};
   rr_property_descriptor_t existing;
   if (!rr_rig_property_describe(rig, name, &existing)) rr_rig_property_define(rig, &d);
   dict_value_t v = {.i = value};
   rr_rig_property_observe(rig, name, VAL_BOOL, &v);
}
static void finish(struct bridge_child *b, rr_control_result_t result) {
   void (*callback)(rr_control_result_t, void *) = b->complete;
   void *user = b->complete_user;
   b->pending = 0;
   b->complete = NULL;
   b->complete_user = NULL;
   if (callback) callback(result, user);
}
static void offline(struct bridge_child *b) {
   b->online = false;
   boolean(b->backend->owner, "online", false);
   boolean(b->backend->owner, "rx.available", false);
   rr_server_vfo_t *vfo = receiver(b);
   if (vfo) {
      rr_vfo_property_unavailable(vfo, RR_PROP_VFO_FREQUENCY);
      rr_vfo_property_unavailable(vfo, RR_PROP_VFO_MODE);
   }
   if (b->pending) finish(b, RR_CONTROL_BACKEND_FAILED);
}
static void disconnect(struct bridge_child *b) {
   offline(b);
   if (b->fd >= 0) close(b->fd);
   b->fd = -1;
   b->hello = b->ready = false;
   if (b->process.pid > 0) {
      kill(b->process.pid, SIGTERM);
      b->kill_deadline = mono_us() + 2000000;
   }
}
static bool send_setting(struct bridge_child *b, uint64_t hz, const char *mode,
   void (*callback)(rr_control_result_t, void *), void *user) {
   if (!b->ready || !b->online || b->pending || hz < b->caps.rx_min_hz || hz > b->caps.rx_max_hz ||
      !rrp_bridge_mode_valid(b->caps.rx_modes, mode)) return false;
   rrp_bridge_ipc_message_t m;
   message(b, &m, RRP_BRIDGE_IPC_SET);
   m.request = ++b->next_request;
   if (!m.request) m.request = ++b->next_request;
   m.frequency_hz = hz;
   snprintf(m.mode, sizeof(m.mode), "%s", mode);
   if (!rrp_bridge_ipc_send(b->fd, &m)) { disconnect(b); return false; }
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
   if (r->rig != b->backend->owner || r->vfo != receiver(b)) return RR_CONTROL_INVALID;
   if (!b->desired || !b->online) return RR_CONTROL_BACKEND_FAILED;
   uint64_t hz = b->desired_hz;
   const char *mode = b->desired_mode;
   if (!strcmp(r->property, RR_PROP_VFO_FREQUENCY) && r->value_type == VAL_LONG) hz = r->value.l;
   else if (!strcmp(r->property, RR_PROP_VFO_MODE) && r->value_type == VAL_STR) mode = r->value.s;
   else return RR_CONTROL_UNSUPPORTED;
   return send_setting(b, hz, mode, r->complete, r->complete_user) ? RR_CONTROL_PENDING : RR_CONTROL_BACKEND_FAILED;
}
static bool create(rr_backend_t *backend) {
   const char *name = rr_backend_config_get(backend, "bridge");
   const char *driver = rrp_bridge_config_get(name, "driver");
   if (!name || !driver) return true;
   struct bridge_child *b = calloc(1, sizeof(*b));
   if (!b) return true;
   b->name = strdup(name); b->driver = strdup(driver);
   if (!b->name || !b->driver) { free(b->name); free(b->driver); free(b); return true; }
   b->backend = backend;
   b->process = (subproc_t)SUBPROC_INIT;
   b->fd = -1;
   b->next = children;
   children = b;
   backend->data = b;
   rr_server_rig_set_control_handler(backend->owner, control, b);
   boolean(backend->owner, "online", false);
   boolean(backend->owner, "rx.available", false);
   boolean(backend->owner, "rx.supported", false);
   boolean(backend->owner, "tx.supported", false);
   // TX availability is deliberately false even for a future TX-capable driver.
   boolean(backend->owner, "tx.available", false);
   return false;
}
static void destroy(rr_backend_t *backend) {
   struct bridge_child *b = backend->data;
   if (!b) return;
   disconnect(b);
   subproc_stop(&b->process, SIGTERM);
   struct bridge_child **p = &children;
   while (*p && *p != b) p = &(*p)->next;
   if (*p) *p = b->next;
   free(b->name); free(b->driver); free(b);
   backend->data = NULL;
}
static bool supported(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   return rr_server_vfo_owner(vfo) == backend->owner && !strcmp(rr_server_vfo_alias(vfo), "A");
}
static bool set_freq(rr_backend_t *backend, rr_server_vfo_t *vfo, int hz) {
   rr_control_request_t r = {.rig = backend->owner, .vfo = vfo, .property = RR_PROP_VFO_FREQUENCY, .value_type = VAL_LONG, .value.l = hz};
   return rr_rig_control(&r) != RR_CONTROL_PENDING;
}
static bool set_mode(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_mode_t mode) {
   rr_control_request_t r = {.rig = backend->owner, .vfo = vfo, .property = RR_PROP_VFO_MODE, .value_type = VAL_STR, .value.s = vfo_mode_name(mode)};
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
   if (!supported(backend, vfo)) return NULL;
   rr_vfo_data_t *state = calloc(1, sizeof(*state));
   if (!state) return NULL;
   rr_property_snapshot_t s;
   if (rr_vfo_property_read(vfo, RR_PROP_VFO_FREQUENCY, &s) && s.known && s.available) state->freq = s.value.l;
   state->mode = get_mode(backend, vfo);
   state->id = VFO_A;
   return state;
}
static const rr_backend_funcs_t api = {.create = create, .destroy = destroy, .vfo_supported = supported, .freq_set = set_freq, .mode_set = set_mode,
   .freq_get = get_freq, .mode_get = get_mode, .mode_get_str = get_mode_string, .poll_state = poll_state};
static const rr_backend_type_t type = {.name = "bridge", .description = "Isolated SDR bridge", .uses_property_state = true, .event_driven = true, .api = &api};
const rr_backend_type_t *rrserver_bridge_backend_type(void) { return &type; }

// Expand the named bridge roster into ordinary configured rigs before registry
// initialization, so UUID persistence, rooms and client discovery stay generic.
bool rrserver_bridges_prepare(void) {
   const char *roster = cfg_get("bridge.instances");
   if (!roster || !*roster) return true;
   gchar **names = g_strsplit_set(roster, " ,\t\r\n", -1);
   GString *rigs = g_string_new(cfg_get("rig.instances") ? cfg_get("rig.instances") : "");
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   gchar **existing = g_strsplit_set(rigs->str, " ,\t\r\n", -1);
   for (int i = 0; existing[i]; i++) if (*existing[i]) g_hash_table_add(seen, g_strdup(existing[i]));
   g_strfreev(existing);
   bool ok = true;
   for (int i = 0; names[i] && ok; i++) {
      const char *name = names[i];
      if (!*name) continue;
      const char *enabled = rrp_bridge_config_get(name, "enabled");
      if (enabled && (!strcasecmp(enabled, "false") || !strcmp(enabled, "0"))) continue;
      if (enabled && strcasecmp(enabled, "true") && strcmp(enabled, "1")) { ok = false; break; }
      const char *alias = rrp_bridge_config_get(name, "rig");
      if (!alias || !*alias) alias = name;
      const char *driver = rrp_bridge_config_get(name, "driver");
      if (!rr_rig_config_alias_valid(name) || strlen(name) >= 64 || !rr_rig_config_alias_valid(alias) || strlen(alias) >= 64 ||
         !rr_rig_config_alias_valid(driver) || strlen(driver) >= 64 || g_hash_table_contains(seen, alias)) { ok = false; break; }
      // Do not silently replace a physical rig with a bridge.
      const char *configured = rr_rig_config_get(alias, "backend");
      if (configured && *configured) { ok = false; break; }
      g_hash_table_add(seen, g_strdup(alias));
      char key[256];
      snprintf(key, sizeof(key), "rig:%s.backend", alias); dict_add(cfg, key, "bridge");
      snprintf(key, sizeof(key), "rig:%s.bridge", alias); dict_add(cfg, key, name);
      snprintf(key, sizeof(key), "rig:%s.vfos", alias); dict_add(cfg, key, "A");
      g_string_append_printf(rigs, " %s", alias);
   }
   if (ok) ok = dict_add(cfg, "rig.instances", rigs->str) == 0;
   else Log(LOG_CRIT, "bridge", "Invalid bridge roster or conflicting rig alias");
   g_string_free(rigs, true); g_strfreev(names); g_hash_table_destroy(seen);
   return ok;
}
static void schedule_retry(struct bridge_child *b) {
   unsigned delay = 1U << (b->failures < 6 ? b->failures : 6);
   if (b->failures < 6) b->failures++;
   b->next_retry = mono_us() + (uint64_t)delay * 1000000;
}
static bool spawn_child(struct bridge_child *b) {
   const char *path = cfg_get("bridge.path");
   if (!path || !*path) path = "bin/rrbridge";
   const char *argv[] = {path, "--config", config_path, "--bridge", b->name, NULL};
   int sv[2];
   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv)) return false;
   subproc_fd_t map[] = {{sv[1], RRP_BRIDGE_IPC_FD}};
   int closes[] = {sv[0], sv[1]};
   bool ok = subproc_spawn_fds(&b->process, path, argv, map, 1, closes, 2);
   close(sv[1]);
   if (!ok) { close(sv[0]); return false; }
   b->fd = sv[0];
   b->started = b->last_seen = mono_us();
   b->kill_deadline = 0;
   b->hello = b->ready = false;
   Log(LOG_INFO, "bridge", "Spawned [%s] pid=%ld", b->name, (long)b->process.pid);
   return true;
}
bool rrserver_bridges_start(const char *file) {
   if (config_path) return false;
   stopping = false;
   if (!children) return true;
   if (!file || !(config_path = strdup(file))) return false;
   for (struct bridge_child *b = children; b; b = b->next) if (!spawn_child(b)) schedule_retry(b);
   return true;
}
static bool capabilities(struct bridge_child *b, const rrp_bridge_ipc_message_t *m) {
   if (!(m->flags & RRP_BRIDGE_RX) || m->rx_max_hz > LONG_MAX) return false;
   // Generic rig modes use canonical names understood by existing clients.
   gchar **modes = g_strsplit(m->rx_modes, " ", -1);
   bool valid = true;
   unsigned count = 0;
   for (int i = 0; modes[i]; i++) if (*modes[i]) {
      rr_mode_t parsed = vfo_parse_mode(modes[i]);
      if (parsed == MODE_NONE || strlen(modes[i]) >= 16 || strcmp(modes[i], vfo_mode_name(parsed))) valid = false;
      count++;
   }
   valid = valid && count;
   g_strfreev(modes);
   if (!valid) return false;
   rr_server_vfo_t *vfo = receiver(b);
   rr_property_descriptor_t d = {.name = RR_PROP_VFO_FREQUENCY, .type = VAL_LONG, .readable = true, .writable = true, .unit = "Hz",
      .has_min = true, .has_max = true, .has_step = true, .minimum.l = m->rx_min_hz, .maximum.l = m->rx_max_hz, .step.l = 1};
   if (rr_vfo_property_redefine(vfo, &d)) return false;
   d = (rr_property_descriptor_t){.name = RR_PROP_VFO_MODE, .type = VAL_STR, .readable = true, .writable = true, .enum_values = m->rx_modes};
   if (rr_vfo_property_redefine(vfo, &d)) return false;
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
   if (strcmp(m->name, b->name) || strcmp(m->driver, b->driver)) return false;
   if (!b->hello) {
      if (m->type != RRP_BRIDGE_IPC_HELLO) return false;
      b->hello = true;
      return true;
   }
   if (!b->ready) return m->type == RRP_BRIDGE_IPC_CAPABILITIES && capabilities(b, m);
   if (m->type == RRP_BRIDGE_IPC_HEARTBEAT) return true;
   if (m->type == RRP_BRIDGE_IPC_STATE) {
      if (!m->available) { offline(b); return true; }
      if (m->frequency_hz < b->caps.rx_min_hz || m->frequency_hz > b->caps.rx_max_hz || !rrp_bridge_mode_valid(b->caps.rx_modes, m->mode)) return false;
      bool was_online = b->online;
      b->online = true;
      boolean(b->backend->owner, "online", true);
      boolean(b->backend->owner, "rx.available", true);
      dict_value_t value = {.l = m->frequency_hz};
      rr_vfo_property_observe(receiver(b), RR_PROP_VFO_FREQUENCY, VAL_LONG, &value);
      value.s = m->mode;
      rr_vfo_property_observe(receiver(b), RR_PROP_VFO_MODE, VAL_STR, &value);
      if (!b->desired) {
         b->desired = true; b->desired_hz = m->frequency_hz;
         snprintf(b->desired_mode, sizeof(b->desired_mode), "%s", m->mode);
      } else if (!was_online && !b->pending && !send_setting(b, b->desired_hz, b->desired_mode, NULL, NULL)) return false;
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
void rrserver_bridges_poll(void) {
   if (stopping || !config_path) return;
   uint64_t current = mono_us();
   for (struct bridge_child *b = children; b; b = b->next) {
      if (b->fd >= 0) {
         for (int i = 0; i < 8; i++) {
            rrp_bridge_ipc_message_t m;
            int got = rrp_bridge_ipc_recv(b->fd, &m);
            if (!got) break;
            if (got < 0 || !handle(b, &m)) {
               Log(LOG_WARN, "bridge", "[%s] disconnected or invalid control record", b->name);
               disconnect(b); break;
            }
            b->last_seen = current;
         }
         if ((b->pending && current >= b->deadline) || (!b->ready && current - b->started >= 5000000) || current - b->last_seen >= 10000000)
            disconnect(b);
      }
      if (b->fd < 0 && b->process.pid > 0 && current >= b->kill_deadline) kill(b->process.pid, SIGKILL);
      int status;
      if (b->process.pid > 0 && subproc_reap(&b->process, &status)) {
         disconnect(b);
         subproc_close(&b->process);
         schedule_retry(b);
      }
      // Reset backoff only after a stable session, never on a crash loop.
      if (b->ready && current - b->started >= 30000000) b->failures = 0;
      if (b->process.pid <= 0 && current >= b->next_retry) if (!spawn_child(b)) schedule_retry(b);
   }
}
void rrserver_bridges_stop(void) {
   stopping = true;
   for (struct bridge_child *b = children; b; b = b->next) {
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
      for (struct bridge_child *b = children; b; b = b->next) {
         int status;
         if (b->process.pid > 0 && !subproc_reap(&b->process, &status)) running = true;
      }
      if (running) poll(NULL, 0, 5);
   } while (running && mono_us() < deadline);
   for (struct bridge_child *b = children; b; b = b->next) {
      disconnect(b);
      subproc_stop(&b->process, SIGTERM);
   }
   free(config_path); config_path = NULL;
}
