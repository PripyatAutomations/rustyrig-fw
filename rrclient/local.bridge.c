/* Local SDR host: reuse the rrbridge process and Kiwi driver, no rrserver.
 * All control and resampler callbacks are owned by the client main context. */
#include <rrclient/local.bridge.h>
#include <rrclient/audio.h>
#include <rrclient/receiver.directory.h>
#include <librrprotocol/bridge.ipc.h>
#include <librrprotocol/bridge.media.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
extern char **environ;

static struct {
   int control, media;
   pid_t pid;
   guint timer;
   char *url, *config;
   bool online, pcm_started;
   unsigned rate;
   uint64_t pending, serial;
   rrp_bridge_ipc_message_t caps;
} local = { .control = -1, .media = -1 };

#define LOCAL_STREAM "local.kiwi.rx"
#define LOCAL_RESAMPLER "local.kiwi.resampler"

bool rrclient_local_bridge_connected(const char *url) {
   return local.pid > 0 && local.url && url && !strcmp(local.url, url);
}
bool rrclient_local_bridge_active(void) {
   return local.pid > 0 && local.online;
}
bool rrclient_local_bridge_tune(uint64_t hz, const char *mode) {
   if (!rrclient_local_bridge_active() || local.control < 0 ||
       local.pending || !(local.caps.flags & RRP_BRIDGE_RX) ||
       hz < local.caps.rx_min_hz || hz > local.caps.rx_max_hz ||
       !mode || !rrp_bridge_mode_valid(local.caps.rx_modes, mode))
      return false;
   rrp_bridge_ipc_message_t command;
   rrp_bridge_ipc_init(&command, RRP_BRIDGE_IPC_SET);
   command.request = ++local.serial;
   command.frequency_hz = hz;
   g_strlcpy(command.mode, mode, sizeof(command.mode));
   if (!rrp_bridge_ipc_send(local.control, &command)) return false;
   local.pending = command.request;
   return true;
}
static void local_pcm(const char *name, const void *pcm, size_t bytes, void *unused) {
   (void)name;
   (void)unused;
   if (local.online && bytes && !(bytes % sizeof(int16_t)))
      audio_local_rx_push(LOCAL_STREAM, pcm, bytes / sizeof(int16_t));
}
void rrclient_local_bridge_disconnect(void) {
   if (local.timer) {
      g_source_remove(local.timer);
      local.timer = 0;
   }
   if (local.control >= 0) {
      rrp_bridge_ipc_message_t message;
      rrp_bridge_ipc_init(&message, RRP_BRIDGE_IPC_SHUTDOWN);
      rrp_bridge_ipc_send(local.control, &message);
      close(local.control);
      local.control = -1;
   }
   if (local.media >= 0) {
      close(local.media);
      local.media = -1;
   }
   if (local.pcm_started) {
      fwdsp_processor_stop(LOCAL_RESAMPLER);
      local.pcm_started = false;
   }
   audio_local_rx_stop(LOCAL_STREAM);
   if (local.pid > 0) {
      /* Child is asked to exit; do not block the GTK main thread. */
      local.pid = 0; /* Child watch reaps the process after exit. */
   }
   if (local.config) {
      g_unlink(local.config);
      g_clear_pointer(&local.config, g_free);
   }
   g_clear_pointer(&local.url, g_free);
   local.online = false;
   local.rate = 0;
   local.pending = 0;
   memset(&local.caps, 0, sizeof(local.caps));
   event_emit("client.receiver-directory.connection", NULL, "Local SDR disconnected.");
   event_emit("client.audio.streams.changed", NULL, NULL);
}
static void local_child_exited(GPid pid, gint status, gpointer unused) {
   (void)status;
   (void)unused;
   if (local.pid == pid) {
      Log(LOG_WARN, "local.bridge", "Local SDR process %ld exited", (long)pid);
      rrclient_local_bridge_disconnect();
   }
   g_spawn_close_pid(pid);
}
static gboolean poll_local(gpointer unused) {
   (void)unused;
   if (local.pid <= 0) return G_SOURCE_REMOVE;
   for (unsigned n = 0; n < 16; n++) {
      rrp_bridge_ipc_message_t m;
      int got = rrp_bridge_ipc_recv(local.control, &m);
      if (!got) break;
      if (got < 0) {
         local.timer = 0;
         rrclient_local_bridge_disconnect();
         return G_SOURCE_REMOVE;
      }
      if (m.type == RRP_BRIDGE_IPC_CAPABILITIES) {
         local.caps = m;
      } else if (m.type == RRP_BRIDGE_IPC_RESULT) {
         if (m.request == local.pending) {
            local.pending = 0;
            if (!m.success)
               Log(LOG_WARN, "local.bridge", "Local SDR tuning request rejected");
         }
      } else if (m.type == RRP_BRIDGE_IPC_STATE) {
         bool was_online = local.online;
         local.online = m.available != 0;
         if (local.online && !was_online) {
            receiver_directory_record_connection(local.url);
            Log(LOG_INFO, "local.bridge", "Local Kiwi online: %s", local.url);
            event_emit("client.receiver-directory.connection", NULL, "Local Kiwi connected.");
            event_emit("client.audio.streams.changed", NULL, NULL);
         }
      } else if (m.type == RRP_BRIDGE_IPC_LOG) {
         Log(LOG_INFO, "local.bridge", "%s", m.metadata_value);
      }
   }
   for (unsigned n = 0; n < 16; n++) {
      struct rrp_bridge_pcm frame;
      int got = rrp_bridge_media_recv(local.media, &frame);
      if (!got) break;
      if (got < 0) {
         local.timer = 0;
         rrclient_local_bridge_disconnect();
         return G_SOURCE_REMOVE;
      }
      if (!local.online || !frame.count) continue;
      if (!local.pcm_started || local.rate != frame.rate) {
         if (local.pcm_started) fwdsp_processor_stop(LOCAL_RESAMPLER);
         local.pcm_started = fwdsp_audio_resampler_start(LOCAL_RESAMPLER, frame.rate, local_pcm, NULL);
         local.rate = frame.rate;
      }
      if (local.pcm_started) {
         uint8_t samples[2 * RRP_BRIDGE_MEDIA_SAMPLES];
         for (size_t i = 0; i < frame.count; i++) {
            samples[2*i] = (uint8_t)frame.samples[i];
            samples[2*i+1] = (uint16_t)frame.samples[i] >> 8;
         }
         fwdsp_processor_write(LOCAL_RESAMPLER, samples, frame.count * 2);
      }
   }
   return G_SOURCE_CONTINUE;
}
bool rrclient_local_bridge_connect(const char *url) {
   if (!url) return false;
   if (rrclient_local_bridge_connected(url)) return true;
   GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
   if (!uri) return false;
   const char *scheme = g_uri_get_scheme(uri), *host = g_uri_get_host(uri);
   int port = g_uri_get_port(uri);
   bool valid = scheme && (!g_ascii_strcasecmp(scheme, "http") || !g_ascii_strcasecmp(scheme, "https")) &&
      host && *host && strspn(host, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-") == strlen(host);
   if (!valid) {
      g_uri_unref(uri);
      return false;
   }
   if (port <= 0) port = !g_ascii_strcasecmp(scheme, "https") ? 443 : 80;
   char *program = g_find_program_in_path("rrbridge");
   if (!program && g_file_test("./bin/rrbridge", G_FILE_TEST_IS_EXECUTABLE))
      program = g_strdup("./bin/rrbridge");
   if (!program) {
      Log(LOG_WARN, "local.bridge", "rrbridge executable unavailable");
      g_uri_unref(uri);
      return false;
   }
   rrclient_local_bridge_disconnect();
   char *config = NULL;
   int tmp = g_file_open_tmp("rustyrig-local-bridge-XXXXXX", &config, NULL);
   if (tmp < 0) {
      g_free(program);
      g_uri_unref(uri);
      return false;
   }
   close(tmp);
   char *text = g_strdup_printf("[bridge]\nmodules = auto\n[bridge:local]\ndriver=kiwi\nhost=%s\nport=%d\nsecure=%s\nfrequency=14074000\nmode=USB\n", host, port, !g_ascii_strcasecmp(scheme, "https") ? "true" : "false");
   bool written = g_file_set_contents(config, text, -1, NULL);
   g_free(text);
   g_uri_unref(uri);
   if (!written) {
      g_unlink(config);
      g_free(config);
      g_free(program);
      return false;
   }
   int control[2], media[2];
   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control)) {
      g_unlink(config); g_free(config); g_free(program);
      return false;
   }
   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, media)) {
      close(control[0]); close(control[1]);
      g_unlink(config); g_free(config); g_free(program);
      return false;
   }
   posix_spawn_file_actions_t actions;
   posix_spawn_file_actions_init(&actions);
   posix_spawn_file_actions_adddup2(&actions, control[1], RRP_BRIDGE_IPC_FD);
   posix_spawn_file_actions_adddup2(&actions, media[1], RRP_BRIDGE_MEDIA_FD);
   /* Parent sockets have CLOEXEC. Closing their numeric descriptors in
    * spawn file-actions could accidentally close target fd 3 or fd 4
    * after dup2 when the process starts with few open descriptors. */
   char *const argv[] = { program, "--config", config, "--bridge", "local", NULL };
   pid_t pid = 0;
   int rc = posix_spawn(&pid, program, &actions, NULL, argv, environ);
   posix_spawn_file_actions_destroy(&actions);
   close(control[1]);
   close(media[1]);
   g_free(program);
   if (rc) {
      close(control[0]); close(media[0]);
      g_unlink(config); g_free(config);
      Log(LOG_WARN, "local.bridge", "Unable to spawn rrbridge: %s", g_strerror(rc));
      return false;
   }
   fcntl(control[0], F_SETFL, fcntl(control[0], F_GETFL) | O_NONBLOCK);
   fcntl(media[0], F_SETFL, fcntl(media[0], F_GETFL) | O_NONBLOCK);
   local.control = control[0];
   local.media = media[0];
   local.pid = pid;
   local.url = g_strdup(url);
   local.config = config;
   audio_local_rx_start(LOCAL_STREAM);
   g_child_watch_add(pid, local_child_exited, NULL);
   local.timer = g_timeout_add(10, poll_local, NULL);
   event_emit("client.receiver-directory.connection", NULL, "Local SDR connecting…");
   Log(LOG_INFO, "local.bridge", "Started local Kiwi bridge pid=%ld for %s", (long)pid, url);
   return true;
}
