// Validate the actual supervisor state machine with local IPC, no remote SDR.
#include <assert.h>
#include <sys/stat.h>
#include <rrserver/bridge.manager.c>
struct GlobalState rig;
const char *rr_rig_registry_public_alias(const rr_rig_registry_t *registry, const rr_server_rig_t *radio) {
   (void)registry;
   (void)radio;

   return "bridge-test";
}
int rrserver_bridge_hints_load(const char *name, const char *driver, rrp_bridge_ipc_message_t *out) {
   return 0;
}
bool rrserver_bridge_hints_save(const char *name, const char *driver, const rrp_bridge_ipc_message_t *caps) {
   return true;
}

char *rrserver_bridge_metadata_get(const char *name, const char *driver, const char *key) {
   return NULL;
}
bool rrserver_bridge_metadata_set(const char *name, const char *driver, const char *key, const char *value) {
   return true;
}

time_t now = 100;
bool dying, restarting;
static unsigned completions;
static rr_control_result_t completed;
static void done(rr_control_result_t result, void *user) {
   assert(user == &completed);
   completed = result;
   completions++;
}
static void program_lookup(void) {
   char work[] = "/tmp/rrbridge-path-XXXXXX";
   assert(mkdtemp(work));
   char *cwd = g_get_current_dir(), *search = g_strdup(g_getenv("PATH"));
   assert(!chdir(work) && !mkdir("bin", 0700) && !mkdir("system", 0700));
   FILE *f = fopen("bin/rrbridge", "w");
   assert(f);
   fclose(f);
   assert(!chmod("bin/rrbridge", 0700));
   f = fopen("system/rrbridge", "w");
   assert(f);
   fclose(f);
   assert(!chmod("system/rrbridge", 0700));
   cfg = dict_new();
   assert(cfg && !setenv("PATH", "system", 1));
   char *path = bridge_program_path();
   assert(path && g_str_has_suffix(path, "/system/rrbridge"));
   g_free(path);
   assert(!dict_add(cfg, "bridge.path", "bin/rrbridge"));
   path = bridge_program_path();
   assert(path && g_str_has_suffix(path, "/system/rrbridge"));
   g_free(path);
   assert(!chmod("system/rrbridge", 0600));
   path = bridge_program_path();
   assert(path && g_str_has_suffix(path, "/bin/rrbridge"));
   g_free(path);
   assert(!chmod("system/rrbridge", 0700));
   assert(!dict_add(cfg, "bridge.path", "./bin/rrbridge"));
   path = bridge_program_path();
   assert(path && g_str_has_suffix(path, "/bin/rrbridge"));
   g_free(path);
   assert(!dict_add(cfg, "bridge.path", "/nonexistent/explicit-rrbridge"));
   assert(!bridge_program_path());
   dict_free(cfg);
   cfg = NULL;
   unlink("bin/rrbridge");
   unlink("system/rrbridge");
   rmdir("bin");
   rmdir("system");
   assert(!chdir(cwd) && !rmdir(work));

   if (search) {
      setenv("PATH", search, 1);
   } else {
      unsetenv("PATH");
   }
   g_free(search);
   g_free(cwd);
}
int main(void) {
   program_lookup();
   rr_server_rig_t *radio = rr_server_rig_new("test-rig", "receiver");
   assert(radio);
   rr_server_vfo_t *vfo = rr_server_vfo_add(radio, "test-vfo", "A", "A", RR_VFO_PERSISTENT);
   assert(vfo && !rr_rig_define_vfo_properties(radio, 'A'));
   rr_backend_t backend = {
      .owner = radio
   };
   struct bridge_child b = {
      .name = "east", .driver = "mock", .backend = &backend, .process = SUBPROC_INIT, .fd = -1, .media_fd = -1
   };
   boolean(radio, "online", false);
   rrp_bridge_ipc_message_t m;
   message(&b, &m, RRP_BRIDGE_IPC_CAPABILITIES);
   assert(!handle(&b, &m));     // No capabilities before HELLO.
   message(&b, &m, RRP_BRIDGE_IPC_HELLO);
   strcpy(m.driver, "wrong");
   assert(!handle(&b, &m));
   strcpy(m.driver, "mock");
   assert(handle(&b, &m));
   assert(!handle(&b, &m));     // No duplicate HELLO.
   message(&b, &m, RRP_BRIDGE_IPC_CAPABILITIES);
   m.flags = RRP_BRIDGE_RX;
   m.rx_min_hz = 100000;
   m.rx_max_hz = 30000000;
   strcpy(m.rx_modes, "USB AM");
   assert(handle(&b, &m) && b.ready && !b.online);
   rr_property_snapshot_t snapshot;
   assert(rr_rig_property_read(radio, "online", &snapshot) && !snapshot.value.i);
   message(&b, &m, RRP_BRIDGE_IPC_STATE);
   m.available = 1;
   m.frequency_hz = 14074000;
   strcpy(m.mode, "USB");
   assert(handle(&b, &m) && b.online);
   assert(get_freq(&backend, vfo) == 14074000 && get_mode(&backend, vfo) == MODE_USB);
   rr_vfo_data_t *fresh = poll_state(&backend, vfo);
   assert(fresh && fresh->freq == 14074000 && fresh->mode == MODE_USB);
   free(fresh);
   m.frequency_hz = 99999;
   assert(!handle(&b, &m));
   m.frequency_hz = 14074000;
   strcpy(m.mode, "FM");
   assert(!handle(&b, &m));
   int fd[2];
   assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd));
   b.fd = fd[0];
   rr_control_request_t r = {
      .rig = radio, .vfo = vfo, .property = RR_PROP_VFO_FREQUENCY,
      .value_type = VAL_LONG, .value.l = 7100000, .complete = done, .complete_user = &completed
   };
   assert(control(&r, &b) == RR_CONTROL_PENDING);
   assert(control(&r, &b) == RR_CONTROL_BACKEND_FAILED); // Bounded one-command queue.
   assert(rrp_bridge_ipc_recv(fd[1], &m) == 1 && m.type == RRP_BRIDGE_IPC_SET && m.frequency_hz == 7100000);
   uint64_t request = m.request;
   message(&b, &m, RRP_BRIDGE_IPC_RESULT);
   m.request = request + 1;
   assert(!handle(&b, &m) && !completions);
   m.request = request;
   m.success = false;
   assert(handle(&b, &m) && completions == 1 && completed == RR_CONTROL_BACKEND_FAILED);
   assert(b.desired_hz == 14074000);
   /* UI-originated tune requests don't carry callbacks. The most recent frequency wins while the bridge has one hardware request pending. */
   rr_control_request_t ui = r;
   ui.complete = NULL;
   ui.complete_user = NULL;
   ui.value.l = 7200000;
   assert(control(&ui, &b) == RR_CONTROL_PENDING);
   assert(rrp_bridge_ipc_recv(fd[1], &m) == 1 && m.type == RRP_BRIDGE_IPC_SET && m.frequency_hz == 7200000);
   uint64_t first_ui_request = m.request;
   ui.value.l = 7274000;
   assert(control(&ui, &b) == RR_CONTROL_PENDING);
   assert(b.queued_tune && b.queued_hz == 7274000);
   ui.value.l = 7200100;
   assert(control(&ui, &b) == RR_CONTROL_PENDING);
   assert(b.queued_tune && b.queued_hz == 7200100);
   message(&b, &m, RRP_BRIDGE_IPC_RESULT);
   m.request = first_ui_request;
   m.success = true;
   assert(handle(&b, &m) && !b.queued_tune && b.pending);
   assert(rrp_bridge_ipc_recv(fd[1], &m) == 1 && m.type == RRP_BRIDGE_IPC_SET && m.frequency_hz == 7200100);
   message(&b, &m, RRP_BRIDGE_IPC_RESULT);
   m.request = b.pending;
   m.success = true;
   assert(handle(&b, &m) && !b.pending && b.desired_hz == 7200100);
   assert(control(&r, &b) == RR_CONTROL_PENDING);
   offline(&b);
   assert(completions == 2 && !b.pending && !b.online);
   assert(rr_vfo_property_read(vfo, RR_PROP_VFO_FREQUENCY, &snapshot) && !snapshot.available);
   assert(get_freq(&backend, vfo) == 0 && get_mode(&backend, vfo) == MODE_NONE);
   close(fd[0]);
   close(fd[1]);
   rr_server_rig_free(radio);
   puts("PASS: bridge handshake identity/order, actual availability, queued UI tune coalescing, bounded callback controls and failure completion");

   return 0;
}
