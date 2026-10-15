//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Isolated SDR host; drivers own their external device transport.
#include <librrprotocol/bridge.sdr.h>
#include <librrprotocol/bridge.ipc.h>
#include <librrprotocol/bridge.media.h>
#include <time.h>
#include <ext/libmongoose/mongoose.h>
#include <librrprotocol/bridge.config.h>
#include <librustyaxe/core.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

time_t now;

struct bridge_host {
   const char *name, *driver;
   bool failed;
   uint64_t pending;
   bool metadata_online;
   const rrp_sdr_driver_t *api;
   void *instance;
   rrp_bridge_ipc_message_t caps;
};
static void message(struct bridge_host *host, rrp_bridge_ipc_message_t *m, uint32_t type) {
   rrp_bridge_ipc_init(m, type);
   snprintf(m->name, sizeof(m->name), "%s", host->name);
   snprintf(m->driver, sizeof(m->driver), "%s", host->driver);
}
static void send_message(struct bridge_host *host, const rrp_bridge_ipc_message_t *m) {
   // No unbounded output queue. A stalled parent forces a clean reconnect.
   if (!rrp_bridge_ipc_send(RRP_BRIDGE_IPC_FD, m)) {
      host->failed = true;
   }
}
static void audio(void *user, const int16_t *samples, size_t count, unsigned rate) {
   struct bridge_host *host = user;

   if (rrp_bridge_media_send(RRP_BRIDGE_MEDIA_FD, samples, count, rate) < 0) {
      host->failed = true;
   }
}
static void metadata(void *user, const char *key, const char *value) {
   struct bridge_host *host = user;
   if (!key || !value || !*value) {
      return;
   }
   rrp_bridge_ipc_message_t m;
   message(host, &m, RRP_BRIDGE_IPC_METADATA);
   snprintf(m.metadata_key, sizeof(m.metadata_key), "%s", key);
   snprintf(m.metadata_value, sizeof(m.metadata_value), "%s", value);
   send_message(host, &m);
}
static void status(void *user, const char *text) {
   now = time(NULL);

   if (text) {
      Log(LOG_INFO, "rrbridge", "%s", text);
   }
}
static const char *setting(void *user, const char *key) {
   struct bridge_host *host = user;
   const char *value = rrp_bridge_config_get(host->name, key);

   if (!value) {
      char name[96];
      snprintf(name, sizeof(name), "bridge.%s", key);
      value = cfg_get(name);
   }

   return value;
}
static bool capability_message(struct bridge_host *host, rrp_bridge_ipc_message_t *m) {
   rrp_sdr_capabilities_t caps = {
      0
   };

   if (!host->api || !host->instance || !host->api->capabilities(host->instance, &caps)) {
      return false;
   }
   message(host, m, RRP_BRIDGE_IPC_CAPABILITIES);
   m->flags = (caps.rx ? RRP_BRIDGE_RX : 0) | (caps.tx ? RRP_BRIDGE_TX : 0) |
      (caps.rx_audio ? RRP_BRIDGE_RX_AUDIO : 0) | (caps.rx_iq ? RRP_BRIDGE_RX_IQ : 0) |
      (caps.rx_waterfall ? RRP_BRIDGE_WATERFALL : 0) | (caps.tx_audio ? RRP_BRIDGE_TX_AUDIO : 0);
   m->rx_min_hz = caps.rx_min_hz;
   m->rx_max_hz = caps.rx_max_hz;
   m->tx_min_hz = caps.tx_min_hz;
   m->tx_max_hz = caps.tx_max_hz;

   if ((caps.rx_modes && strlen(caps.rx_modes) >= sizeof(m->rx_modes)) || (caps.tx_modes && strlen(caps.tx_modes) >= sizeof(m->tx_modes))) {
      return false;
   }
   snprintf(m->rx_modes, sizeof(m->rx_modes), "%s", caps.rx_modes ? caps.rx_modes : "");
   snprintf(m->tx_modes, sizeof(m->tx_modes), "%s", caps.tx_modes ? caps.tx_modes : "");

   return rrp_bridge_ipc_valid(m);
}
static void state(void *user, bool available, uint64_t hz, const char *mode) {
   struct bridge_host *host = user;
   rrp_bridge_ipc_message_t m;

   if (available) {
      if (!capability_message(host, &m)) {
         host->failed = true;
         return;
      }

      if (memcmp(&host->caps, &m, sizeof(m))) {
         send_message(host, &m);
         host->caps = m;
      }
   }
   if (available && !host->metadata_online) {
      char low[32], high[32];
      snprintf(low, sizeof(low), "%llu", (unsigned long long)host->caps.rx_min_hz);
      snprintf(high, sizeof(high), "%llu", (unsigned long long)host->caps.rx_max_hz);
      metadata(host, "driver", host->driver);
      metadata(host, "low_hz", low); metadata(host, "high_hz", high);
   }
   host->metadata_online = available;
   message(host, &m, RRP_BRIDGE_IPC_STATE);
   m.available = available;
   m.frequency_hz = hz;
   snprintf(m.mode, sizeof(m.mode), "%s", mode ? mode : "");
   send_message(host, &m);
}
static void complete(void *user, uint64_t request, bool success) {
   struct bridge_host *host = user;

   if (!request || request != host->pending) {
      host->failed = true;
      return;
   }
   rrp_bridge_ipc_message_t m;
   message(host, &m, RRP_BRIDGE_IPC_RESULT);
   m.request = request;
   m.success = success;
   host->pending = 0;
   send_message(host, &m);
}
static void usage(const char *prog) {
   fprintf(stderr, "Usage: %s --config FILE --bridge NAME [--probe]\n", prog);
}
int main(int argc, char **argv) {
   const char *file = NULL, *name = NULL;
   bool probe = false;

   for (int i = 1 ; i < argc ; i++) {
      if (!strcmp(argv[i], "--config") && i + 1 < argc) {
         file = argv[++i];
      } else if (!strcmp(argv[i], "--bridge") && i + 1 < argc) {
         name = argv[++i];
      } else if (!strcmp(argv[i], "--probe")) {
         probe = true;
      } else {
         usage(argv[0]);
         return EXIT_FAILURE;
      }
   }

   if (!file || !name || strlen(name) >= 64) {
      usage(argv[0]);
      return EXIT_FAILURE;
   }
   // Keep connection diagnostics without logging every audio socket read/write.
   mg_log_set(MG_LL_INFO);
   now = time(NULL);
   logger_init("-", false);

   if (!rrp_bridge_config_init() || !(cfg = cfg_load(file))) {
      return EXIT_FAILURE;
   }
   const char *driver_name = rrp_bridge_config_get(name, "driver");

   if (!driver_name || !*driver_name || strlen(driver_name) >= 64 ||
      strspn(driver_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(driver_name)) {
      return EXIT_FAILURE;
   }
   const char *directory = cfg_get("bridge.modules");

   if (!directory || !*directory || !strcmp(directory, "auto") || !strcmp(directory, "bin")) {
      char installed[256];
      snprintf(installed, sizeof(installed), "/usr/lib/rustyrig/modules/rrbridge/rrbridge-%s.so", driver_name);
      directory = access(installed, R_OK) == 0 ? "/usr/lib/rustyrig/modules/rrbridge" : "bin";
   }

   if (dict_add(cfg, "path.modules", directory)) {
      return EXIT_FAILURE;
   }
   char module[128];
   snprintf(module, sizeof(module), "rrbridge-%s", driver_name);

   if (rr_load_module(module)) {
      return EXIT_FAILURE;
   }
   struct bridge_host host = {
      .name = name, .driver = driver_name
   };
   rrp_sdr_host_t callbacks = {
      .audio_pcm16 = audio, .metadata = metadata, .status = status, .state = state, .complete = complete, .setting = setting, .user = &host
   };
   const rrp_sdr_driver_t *driver = rrp_sdr_find(driver_name);
   void *instance = driver ? driver->create(&callbacks) : NULL;
   host.api = driver;
   host.instance = instance;
   rrp_bridge_ipc_message_t m = { 0 };
   bool ok = instance && capability_message(&host, &m);
   host.caps = m;
   rrp_sdr_capabilities_t caps = { 0 };

   if (ok) {
      ok = driver->capabilities(instance, &caps);
   }

   if (ok && probe) {
      printf("rrbridge [%s]: driver=%s rx=%d tx=%d rx_audio=%d\n", name, driver_name, caps.rx, caps.tx, caps.rx_audio);
   }

   if (ok && !probe) {
      rrp_bridge_ipc_message_t hello;
      message(&host, &hello, RRP_BRIDGE_IPC_HELLO);
      send_message(&host, &hello);
      send_message(&host, &m);
      state(&host, false, 0, NULL);
      ok = !host.failed && driver->start(instance);

      if (!ok) {
         Log(LOG_WARN, "rrbridge", "Driver %s could not start [%s]; check bridge configuration", driver_name, name);
      }
      uint64_t heartbeat = mono_us();
      while (ok && !host.failed) {
         now = time(NULL);
         struct pollfd fd = {
            .fd = RRP_BRIDGE_IPC_FD, .events = POLLIN
         };
         int rc = poll(&fd, 1, 10);

         if (rc < 0 && errno != EINTR) {
            ok = false;
            break;
         }

         if (fd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
         }

         for (int i = 0 ; i < 8 && !host.failed ; i++) {
            rrp_bridge_ipc_message_t command;
            int got = rrp_bridge_ipc_recv(RRP_BRIDGE_IPC_FD, &command);

            if (!got) {
               break;
            }

            if (got < 0 || strcmp(command.name, name) || strcmp(command.driver, driver_name)) {
               host.failed = true;
               break;
            }

            if (command.type == RRP_BRIDGE_IPC_SHUTDOWN) {
               goto finished;
            }

            if (command.type != RRP_BRIDGE_IPC_SET || host.pending) {
               host.failed = true;
               break;
            }
            host.pending = command.request;

            if (command.frequency_hz < host.caps.rx_min_hz || command.frequency_hz > host.caps.rx_max_hz ||
               !rrp_bridge_mode_valid(host.caps.rx_modes, command.mode) || !driver->set_receiver(instance, command.request, command.frequency_hz, command.mode))
            {
               complete(&host, command.request, false);
            }
         }

         driver->poll(instance);
         uint64_t current = mono_us();

         if (current - heartbeat >= 1000000) {
            message(&host, &m, RRP_BRIDGE_IPC_HEARTBEAT);
            send_message(&host, &m);
            heartbeat = current;
         }
      }
   }

finished:
   if (instance) {
      driver->destroy(instance);
   }
   rr_unload_module(module);
   cfg_fini();

   return ok && !host.failed ? EXIT_SUCCESS : EXIT_FAILURE;
}
