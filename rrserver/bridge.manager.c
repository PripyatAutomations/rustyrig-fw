// One supervised rrbridge child for each configured SDR endpoint.
#include <ctype.h>
#include <signal.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <librrprotocol/bridge.ipc.h>
#include <librustyaxe/core.h>
#include <librustyaxe/subproc.h>
#include <rrserver/cfg.bridge.h>
#include <rrserver/bridge.manager.h>

struct bridge_child {
   char *name;
   subproc_t process;
   int control_fd;
   bool hello, ready;
   time_t next_retry;
   unsigned failures;
   struct bridge_child *next;
};
static struct bridge_child *children;
static char *config_path;

static bool spawn_child(struct bridge_child *b) {
   const char *path = cfg_get("bridge.path");
   if (!path || !*path) path = "bin/rrbridge";
   const char *argv[] = { path, "--config", config_path, "--bridge", b->name, NULL };
   /* Do not expose receiver credentials on the process command line. */
   int sv[2] = {-1, -1};
   if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv)) return false;
   subproc_fd_t map[] = {{sv[1], RRP_BRIDGE_IPC_FD}};
   int closes[] = {sv[0], sv[1]};
   bool ok = subproc_spawn_fds(&b->process, path, argv, map, 1, closes, 2);
   close(sv[1]);
   if (!ok) close(sv[0]);
   else {
      if (b->control_fd >= 0) close(b->control_fd);
      b->control_fd = sv[0];
      b->hello = b->ready = false;
   }
   if (ok) Log(LOG_INFO, "bridge", "Spawned [%s] pid=%ld", b->name, (long)b->process.pid);
   else Log(LOG_WARN, "bridge", "Could not spawn [%s] using %s", b->name, path);
   return ok;
}

static void schedule_retry(struct bridge_child *b) {
   unsigned delay = 1U << (b->failures < 6 ? b->failures : 6);
   if (b->failures < 6) b->failures++;
   b->next_retry = time(NULL) + (time_t)delay;
}

bool rrserver_bridges_start(const char *file) {
   if (children || config_path) return false;
   const char *instances = cfg_get("bridge.instances");
   if (!instances || !*instances) return true;
   if (!file || !(config_path = strdup(file))) return false;
   char *names = strdup(instances);
   if (!names) { free(config_path); config_path = NULL; return false; }
   bool ok = true;
   char *save = NULL;
   for (char *name = strtok_r(names, " ,\t\r\n", &save); name; name = strtok_r(NULL, " ,\t\r\n", &save)) {
      const char *driver = rrserver_bridge_config_get(name, "driver");
      const char *enabled = rrserver_bridge_config_get(name, "enabled");
      if (enabled && (!strcasecmp(enabled, "false") || !strcmp(enabled, "0"))) continue;
      if (!driver || !*driver) {
         Log(LOG_CRIT, "bridge", "[%s] has no driver", name);
         ok = false;
         continue;
      }
      bool duplicate = false;
      for (struct bridge_child *b = children; b; b = b->next)
         if (!strcmp(b->name, name)) duplicate = true;
      if (duplicate) { Log(LOG_WARN, "bridge", "Duplicate bridge name %s", name); ok = false; continue; }
      struct bridge_child *b = calloc(1, sizeof(*b));
      if (!b || !(b->name = strdup(name))) { free(b); ok = false; break; }
      b->process = (subproc_t)SUBPROC_INIT;
      b->control_fd = -1;
      b->next = children;
      children = b;
      if (!spawn_child(b)) schedule_retry(b);
   }
   free(names);
   if (!ok) rrserver_bridges_stop();
   return ok;
}

static void read_control(struct bridge_child *b) {
   if (b->control_fd < 0) return;
   for (int i = 0; i < 8; i++) { /* bounded work per main-loop iteration */
      rrp_bridge_ipc_message_t m;
      int ret = rrp_bridge_ipc_recv(b->control_fd, &m);
      if (ret == 0) break;
      if (ret < 0 || strcmp(m.name, b->name)) {
         Log(LOG_WARN, "bridge", "Bridge [%s] control channel disconnected or invalid", b->name);
         close(b->control_fd); b->control_fd = -1;
         if (b->process.pid > 0) kill(b->process.pid, SIGTERM);
         break;
      }
      if (m.type == RRP_BRIDGE_IPC_HELLO && !b->hello) {
         b->hello = true;
         Log(LOG_INFO, "bridge", "Bridge [%s] connected (%s)", b->name, m.driver);
      } else if (m.type == RRP_BRIDGE_IPC_CAPABILITIES && b->hello && !b->ready) {
         b->ready = true;
         Log(LOG_INFO, "bridge", "Bridge [%s] capabilities: RX=%u TX=%u freq=%llu-%llu Hz", b->name,
             !!(m.flags & 1), !!(m.flags & 2), (unsigned long long)m.rx_min_hz, (unsigned long long)m.rx_max_hz);
         /* TODO: register this source in rig.registry and translate to property descriptors. */
      } else {
         Log(LOG_WARN, "bridge", "Bridge [%s] unexpected control message %u", b->name, m.type);
      }
   }
}

void rrserver_bridges_poll(void) {
   time_t current = time(NULL);
   for (struct bridge_child *b = children; b; b = b->next) {
      read_control(b);
      int status;
      if (b->process.pid > 0 && subproc_reap(&b->process, &status)) {
         Log(LOG_WARN, "bridge", "Bridge [%s] exited (status=%d)", b->name, status);
         subproc_close(&b->process);
         if (b->control_fd >= 0) close(b->control_fd);
         b->control_fd = -1; b->hello = b->ready = false;
         schedule_retry(b);
      }
      if (b->process.pid <= 0 && current >= b->next_retry)
         if (!spawn_child(b)) schedule_retry(b);
   }
}

void rrserver_bridges_stop(void) {
   while (children) {
      struct bridge_child *b = children;
      children = b->next;
      if (b->control_fd >= 0) {
         rrp_bridge_ipc_message_t bye;
         rrp_bridge_ipc_init(&bye, RRP_BRIDGE_IPC_SHUTDOWN);
         (void)rrp_bridge_ipc_send(b->control_fd, &bye);
         close(b->control_fd);
      }
      subproc_stop(&b->process, SIGTERM);
      free(b->name);
      free(b);
   }
   free(config_path);
   config_path = NULL;
}
