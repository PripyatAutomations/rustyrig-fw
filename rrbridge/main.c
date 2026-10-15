// Isolated SDR bridge host. Device transports are supplied by modules.
// TODO: attach librrprotocol bridge session, descriptor publication, control, and media.
#include <librrprotocol/bridge.sdr.h>
#include <librrprotocol/bridge.ipc.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <librustyaxe/core.h>
#include <rrserver/cfg.bridge.h>  // Shared parser; move to librrprotocol when stabilized.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
   fprintf(stderr, "Usage: %s --config FILE --bridge NAME [--probe]\n", prog);
}

static void status(void *user, const char *message) {
   (void)user;
   if (message) Log(LOG_INFO, "rrbridge", "%s", message);
}

static void audio(void *user, const int16_t *pcm, size_t samples, unsigned rate) {
   (void)user;
   (void)pcm;
   (void)samples;
   (void)rate;
   // TODO: enqueue PCM into librrprotocol media path; don't block driver callback.
}

int main(int argc, char **argv) {
   const char *file = NULL, *name = NULL;
   bool probe = false;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--config") && i + 1 < argc) file = argv[++i];
      else if (!strcmp(argv[i], "--bridge") && i + 1 < argc) name = argv[++i];
      else if (!strcmp(argv[i], "--probe")) probe = true;
      else { usage(argv[0]); return EXIT_FAILURE; }
   }
   if (!file || !name) { usage(argv[0]); return EXIT_FAILURE; }
   logger_init("-", false);
   if (!rrserver_bridge_config_init() || !(cfg = cfg_load(file))) {
      fprintf(stderr, "rrbridge: failed to load bridge configuration\n");
      return EXIT_FAILURE;
   }
   const char *driver_name = rrserver_bridge_config_get(name, "driver");
   if (!driver_name || !*driver_name || strspn(driver_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(driver_name)) {
      fprintf(stderr, "rrbridge: invalid or missing driver for [%s]\n", name);
      return EXIT_FAILURE;
   }
   // Module search path is private to rrbridge, never rrserver's modules.
   const char *directory = cfg_get("bridge.modules");
   if (!directory || !*directory) directory = "bin";
   if (dict_add(cfg, "path.modules", directory) != 0) return EXIT_FAILURE;
   char module[128];
   int n = snprintf(module, sizeof(module), "rrbridge-%s", driver_name);
   if (n < 0 || (size_t)n >= sizeof(module) || rr_load_module(module)) {
      fprintf(stderr, "rrbridge: failed loading driver %s\n", driver_name);
      return EXIT_FAILURE;
   }
   const rrp_sdr_driver_t *driver = rrp_sdr_find(driver_name);
   rrp_sdr_host_t host = {.audio_pcm16 = audio, .status = status};
   void *instance = driver ? driver->create(&host) : NULL;
   rrp_sdr_capabilities_t caps = {0};
   if (!instance || !driver->capabilities(instance, &caps)) {
      fprintf(stderr, "rrbridge: driver %s could not be initialized\n", driver_name);
      if (instance) driver->destroy(instance);
      rr_unload_module(module);
      return EXIT_FAILURE;
   }
   fprintf(stdout, "rrbridge [%s]: driver=%s rx=%d tx=%d rx_audio=%d rx_iq=%d waterfall=%d\n",
           name, driver_name, caps.rx, caps.tx, caps.rx_audio, caps.rx_iq, caps.rx_waterfall);
   fflush(stdout);
   int result = EXIT_SUCCESS;
   if (!probe) {
      rrp_bridge_ipc_message_t m;
      rrp_bridge_ipc_init(&m, RRP_BRIDGE_IPC_HELLO);
      snprintf(m.name, sizeof(m.name), "%s", name);
      snprintf(m.driver, sizeof(m.driver), "%s", driver_name);
      if (!rrp_bridge_ipc_send(RRP_BRIDGE_IPC_FD, &m)) result = EXIT_FAILURE;
      if (result == EXIT_SUCCESS) {
         rrp_bridge_ipc_init(&m, RRP_BRIDGE_IPC_CAPABILITIES);
         snprintf(m.name, sizeof(m.name), "%s", name);
         snprintf(m.driver, sizeof(m.driver), "%s", driver_name);
         m.flags = (caps.rx ? 1u : 0u) | (caps.tx ? 2u : 0u) |
             (caps.rx_audio ? 4u : 0u) | (caps.rx_iq ? 8u : 0u) |
             (caps.rx_waterfall ? 16u : 0u) | (caps.tx_audio ? 32u : 0u);
         m.rx_min_hz = caps.rx_min_hz; m.rx_max_hz = caps.rx_max_hz;
         m.tx_min_hz = caps.tx_min_hz; m.tx_max_hz = caps.tx_max_hz;
         snprintf(m.rx_modes, sizeof(m.rx_modes), "%s", caps.rx_modes ? caps.rx_modes : "");
         snprintf(m.tx_modes, sizeof(m.tx_modes), "%s", caps.tx_modes ? caps.tx_modes : "");
         if (!rrp_bridge_ipc_send(RRP_BRIDGE_IPC_FD, &m)) result = EXIT_FAILURE;
      }
      while (result == EXIT_SUCCESS) {
         struct pollfd pfd = {.fd = RRP_BRIDGE_IPC_FD, .events = POLLIN};
         int rc = poll(&pfd, 1, -1);
         if (rc < 0) { if (errno == EINTR) continue; result = EXIT_FAILURE; break; }
         if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
         if (pfd.revents & POLLIN) {
            int got = rrp_bridge_ipc_recv(RRP_BRIDGE_IPC_FD, &m);
            if (got < 0 || (got == 1 && m.type == RRP_BRIDGE_IPC_SHUTDOWN)) break;
         }
         /* TODO: process tune/mode requests and start external Kiwi connection. */
      }
   }
   driver->destroy(instance);
   rr_unload_module(module);
   cfg_fini();
   return result;
}
