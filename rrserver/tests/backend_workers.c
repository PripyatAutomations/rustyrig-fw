// A blocked rig must not block another rig or the server's socket loop.
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <rrserver/backend.worker.h>
#include <rrserver/rig.registry.h>

time_t now;
bool dying, restarting;
static pthread_t main_thread;
static atomic_uint destroyed, hardware_stops, hardware_starts;
static unsigned completions, failures, observations;
static unsigned pongs;
static gint64 request_at, worst_rtt;
static struct mg_mgr manager;
static struct mg_connection *peer;

typedef struct fixture {
   pthread_mutex_t mutex;
   pthread_cond_t condition;
   bool blocked, entered, released, thread_known, ptt;
   pthread_t thread;
   long frequency;
   unsigned polls, writes;
   bool stop_preceded_writes;
} fixture_t;
static void own_thread(fixture_t *io) {
   assert(!pthread_equal(main_thread, pthread_self()));

   if (!io->thread_known) {
      io->thread = pthread_self();
      io->thread_known = true;
   }
   assert(pthread_equal(io->thread, pthread_self()));
}
static bool create(rr_backend_t *backend) {
   fixture_t *io = calloc(1, sizeof(*io));
   assert(io);
   pthread_mutex_init(&io->mutex, NULL);
   pthread_cond_init(&io->condition, NULL);
   io->frequency = 145000000;
   rr_backend_instance_set_data(backend, io);

   return false;
}
static void destroy(rr_backend_t *backend) {
   fixture_t *io = rr_backend_instance_data(backend);
   own_thread(io);
   assert(!io->ptt);
   pthread_cond_destroy(&io->condition);
   pthread_mutex_destroy(&io->mutex);
   atomic_fetch_add(&destroyed, 1);
   free(io);
   rr_backend_instance_set_data(backend, NULL);
}
static bool ptt_get(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   fixture_t *io = rr_backend_instance_data(backend);
   own_thread(io);

   return io->ptt;
}
static bool ptt_set(rr_backend_t *backend, rr_server_vfo_t *vfo, bool state) {
   fixture_t *io = rr_backend_instance_data(backend);
   own_thread(io);
   io->ptt = state;

   if (state) {
      atomic_fetch_add(&hardware_starts, 1);
   } else {
      if (!io->writes) {
         io->stop_preceded_writes = true;
      }
      atomic_fetch_add(&hardware_stops, 1);
   }

   return false;
}
static bool freq_set(rr_backend_t *backend, rr_server_vfo_t *vfo, int frequency) {
   fixture_t *io = rr_backend_instance_data(backend);
   own_thread(io);
   io->writes++;

   if (frequency <= 0) {
      return true;
   }
   io->frequency = frequency;

   return false;
}
static rr_vfo_data_t *poll_state(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   fixture_t *io = rr_backend_instance_data(backend);
   own_thread(io);
   pthread_mutex_lock(&io->mutex);
   io->polls++;
   io->entered = true;
   pthread_cond_signal(&io->condition);
   while (io->blocked && !io->released) {
      pthread_cond_wait(&io->condition, &io->mutex);
   }
   pthread_mutex_unlock(&io->mutex);
   dict_value_t value = {
      .l = io->frequency
   };
   rr_backend_property_observe(backend, vfo, RR_PROP_VFO_FREQUENCY, VAL_LONG, &value);
   rr_vfo_data_t *state = calloc(1, sizeof(*state));
   assert(state);
   state->freq = io->frequency;
   state->mode = MODE_FM;
   state->width = 12500;

   return state;
}
static void published(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_vfo_data_t *state) {
   assert(pthread_equal(main_thread, pthread_self()));
   observations++;
}
static void completed(rr_backend_t *backend, rr_server_vfo_t *vfo, bool failed, void *user) {
   assert(pthread_equal(main_thread, pthread_self()));
   completions++;
   failures += failed;
}
static void property_changed(const char *event, const char *data, rrconn_t *client, void *user) {
   assert(pthread_equal(main_thread, pthread_self()));
}
static const rr_backend_funcs_t api = {
   .create = create, .destroy = destroy,
   .poll_state = poll_state, .freq_set = freq_set, .ptt_set = ptt_set, .ptt_get = ptt_get
};
static const rr_backend_type_t type = {
   .name = "worker-fixture", .api = &api
};
static void ping_peer(void) {
   request_at = g_get_monotonic_time();
   mg_ws_send(peer, "worker-test", 11, WEBSOCKET_OP_PING);
}
static void socket_event(struct mg_connection *connection, int event, void *data) {
   if (event == MG_EV_HTTP_MSG) {
      mg_ws_upgrade(connection, data, NULL);
   }

   if (event == MG_EV_WS_OPEN && connection->is_client) {
      peer = connection;
      ping_peer();
   }

   if (event == MG_EV_WS_CTL && connection->is_client) {
      struct mg_ws_message *message = data;

      if ((message->flags & 15) == WEBSOCKET_OP_PONG) {
         gint64 elapsed = g_get_monotonic_time() - request_at;

         if (elapsed > worst_rtt) {
            worst_rtt = elapsed;
         }

         if (++pongs < 4) {
            ping_peer();
         }
      }
   }
}
static void drain_until_idle(rr_backend_t *backend) {
   gint64 deadline = g_get_monotonic_time() + 3000000;
   while (rr_backend_worker_busy(backend)) {
      assert(g_get_monotonic_time() < deadline);
      rr_backend_worker_drain(backend);
      g_usleep(1000);
   }
}
int main(void) {
   main_thread = pthread_self();
   event_init();
   event_on(RR_PROPERTY_CHANGED_EVENT, property_changed, NULL);
   rr_rig_registry_t *registry = rr_rig_registry_new();
   rr_server_rig_t *slow = rr_rig_registry_add(registry, "slow", "slow", "slow", &type);
   rr_server_rig_t *fast = rr_rig_registry_add(registry, "fast", "fast", "fast", &type);
   assert(slow && fast);
   rr_server_vfo_t *a = rr_server_vfo_add(slow, "slow-a", "A", "A", RR_VFO_PERSISTENT);
   rr_server_vfo_t *b = rr_server_vfo_add(fast, "fast-a", "A", "A", RR_VFO_PERSISTENT);
   rr_property_descriptor_t frequency = {
      .name = RR_PROP_VFO_FREQUENCY, .type = VAL_LONG, .readable = true, .writable = true
   };
   assert(!rr_vfo_property_define(a, &frequency) && !rr_vfo_property_define(b, &frequency));
   rr_backend_t *s = rr_server_rig_backend(slow), *f = rr_server_rig_backend(fast);
   fixture_t *blocked = rr_backend_instance_data(s);
   blocked->blocked = true;
   assert(!rr_backend_worker_start(s, published) && !rr_backend_worker_start(f, published));
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_POLL, (dict_value_t) { 0 }, NULL, NULL));
   pthread_mutex_lock(&blocked->mutex);
   while (!blocked->entered) {
      pthread_cond_wait(&blocked->condition, &blocked->mutex);
   }
   pthread_mutex_unlock(&blocked->mutex);

   for (int i = 0 ; i < 100 ; i++) {
      assert(!rr_backend_worker_submit(s, a, RR_BACKEND_POLL, (dict_value_t) { 0 }, NULL, NULL));
   }

   assert(!rr_backend_worker_submit(f, b, RR_BACKEND_FREQ, (dict_value_t) { .i = 7100000 }, completed, NULL));
   drain_until_idle(f);
   assert(completions == 1 && !failures);
   rr_property_snapshot_t snapshot;
   assert(rr_vfo_property_read(b, RR_PROP_VFO_FREQUENCY, &snapshot) && snapshot.known && snapshot.value.l == 7100000);
   assert(rr_vfo_property_read(a, RR_PROP_VFO_FREQUENCY, &snapshot) && !snapshot.known);
   // Exercise real WebSocket sockets while the other rig's poll stays blocked.
   mg_mgr_init(&manager);
   mg_log_set(MG_LL_ERROR);
   struct mg_connection *listener = mg_http_listen(&manager, "http://127.0.0.1:0", socket_event, NULL);
   assert(listener);
   char url[96];
   snprintf(url, sizeof(url), "ws://127.0.0.1:%u/ws", mg_ntohs(listener->loc.port));
   assert(mg_ws_connect(&manager, url, socket_event, NULL, NULL));
   gint64 deadline = g_get_monotonic_time() + 2000000;
   while (pongs < 4 && g_get_monotonic_time() < deadline) {
      mg_mgr_poll(&manager, 0);
      g_usleep(1000);
   }
   assert(pongs == 4 && worst_rtt < 500000);
   printf("PASS: blocked backend leaves other rigs and WebSocket traffic responsive (worst ping %.2f ms)\n", worst_rtt / 1000.0);
   mg_mgr_free(&manager);
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_PTT, (dict_value_t) { .i = true }, completed, NULL));
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_FREQ, (dict_value_t) { .i = 7200000 }, completed, NULL));
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_PTT, (dict_value_t) { .i = false }, completed, NULL));
   // A second release shares the hardware operation and cancels a newer ON.
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_PTT, (dict_value_t) { .i = true }, completed, NULL));
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_PTT, (dict_value_t) { .i = false }, completed, NULL));
   rr_backend_worker_drain(s); // Cancelled key-down is completed on this thread.
   assert(failures == 2 && !atomic_load(&hardware_starts));
   unsigned accepted = 0;
   while (!rr_backend_worker_submit(s, a, RR_BACKEND_FREQ, (dict_value_t) { .i = 7300000 }, completed, NULL)) {
      assert(++accepted < 65);
   }
   assert(accepted > 0);
   pthread_mutex_lock(&blocked->mutex);
   blocked->released = true;
   pthread_cond_signal(&blocked->condition);
   pthread_mutex_unlock(&blocked->mutex);
   drain_until_idle(s);
   assert(blocked->stop_preceded_writes && !atomic_load(&hardware_starts) && atomic_load(&hardware_stops) == 1);
   assert(blocked->polls == blocked->writes + 1); // Repeated poll requests coalesced.
   unsigned errors = failures;
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_FREQ, (dict_value_t) { .i = -1 }, completed, NULL));
   drain_until_idle(s);
   assert(failures == errors + 1);
   // A successful key-down is physically released before worker destruction.
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_PTT, (dict_value_t) { .i = true }, completed, NULL));
   drain_until_idle(s);
   assert(rr_backend_api(s)->ptt_get(s, a));
   // Teardown preserves queued releases as well as dekeying an idle worker.
   assert(!rr_backend_worker_submit(s, a, RR_BACKEND_PTT, (dict_value_t) { .i = false }, completed, NULL));
   assert(!rr_backend_worker_submit(f, b, RR_BACKEND_PTT, (dict_value_t) { .i = true }, completed, NULL));
   drain_until_idle(f);
   rr_rig_registry_free(registry);
   assert(atomic_load(&destroyed) == 2 && atomic_load(&hardware_stops) == 3);
   event_shutdown();
   puts("PASS: worker ownership, main-thread observations, bounded queues, stop priority, stale key-down cancellation and shutdown release");

   return 0;
}
