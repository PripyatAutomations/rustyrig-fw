/* Local SDR host: reuse the rrbridge process and Kiwi driver, no rrserver.
 * All control and resampler callbacks are owned by the client main context. */
#include <rrclient/local.bridge.h>
#include <librrprotocol/rig.command.h>
#include <rrclient/rooms.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/ui.h>
#include <rrclient/audio.h>
#include <rrclient/receiver.directory.h>
#include <librrprotocol/bridge.ipc.h>
#include <librrprotocol/bridge.media.h>
#include <rrbridge/spectrum.row.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <librustyaxe/subproc.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


typedef struct bridge_instance {
   int control, media, spectrum;
   pid_t pid;
   subproc_t process;
   guint timer, kill_timer;
   char *url, *config;
   char driver[32];
   char stream[64], resampler[64];
   unsigned slot;
   bool online, pcm_started;
   unsigned rate;
   gint64 resampler_retry_at;
   uint64_t pending, serial, frequency_hz, pending_hz;
   char pending_mode[16];
   bool queued_tune;
   uint64_t queued_hz;
   char queued_mode[16];
   char mode[16];
   rrp_bridge_ipc_message_t caps;
} bridge_instance;

static bridge_instance *bridges[26];

static void local_room_refresh(void) {
   uint32_t mask = 0;
   char bindings[512] = "";
   for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
      bridge_instance *b = bridges[i];
      if (!b || !b->online) continue;
      mask |= UINT32_C(1) << i;
      char token[32];
      g_snprintf(token, sizeof(token), "%slocal.sdr.vfo_%c", *bindings ? " " : "", 'a' + i);
      if (strlen(bindings) + strlen(token) < sizeof(bindings))
         strcat(bindings, token);
   }
   rrclient_room_join("&local-sdr");
   rrclient_room_set_vfos("&local-sdr", bindings);
   rrclient_room_set_vfo_mask("&local-sdr", mask);
   rrclient_room_set_control_flags("&local-sdr", false, mask != 0);
   rrclient_room_set_offline("&local-sdr", mask == 0);
   event_emit("client.local-bridge.vfo", NULL, NULL);
   event_emit("client.audio.streams.changed", NULL, NULL);
}

static bridge_instance *bridge_active(void) {
   unsigned slot = (unsigned)(rrclient_room_active_vfo("&local-sdr") - 'A');
   return slot < G_N_ELEMENTS(bridges) ? bridges[slot] : NULL;
}

bool rrclient_local_bridge_connected(const char *url) {
   if (!url) return false;
   for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++)
      if (bridges[i] && bridges[i]->pid > 0 && bridges[i]->url &&
          !strcmp(bridges[i]->url, url)) return true;
   return false;
}
bool rrclient_local_bridge_waterfall(bool enabled, unsigned zoom) {
   bridge_instance *b = bridge_active();
   if (!b || b->control < 0 || !(b->caps.flags & RRP_BRIDGE_WATERFALL) ||
       zoom < b->caps.waterfall_zoom_min || zoom > b->caps.waterfall_zoom_max) return false;
   rrp_bridge_ipc_message_t msg;
   rrp_bridge_ipc_init(&msg, RRP_BRIDGE_IPC_WATERFALL);
   g_strlcpy(msg.name, "local", sizeof(msg.name));
   g_strlcpy(msg.driver, b->driver, sizeof(msg.driver));
   msg.available = enabled;
   msg.flags = zoom;
   return rrp_bridge_ipc_send(b->control, &msg);
}
bool rrclient_local_bridge_active(void) {
   bridge_instance *b = bridge_active();
   return b && b->pid > 0 && b->online;
}
bool rrclient_local_bridge_state_vfo(char vfo, uint64_t *hz, const char **mode) {
   unsigned slot = (unsigned)(vfo - 'A');
   if (slot >= G_N_ELEMENTS(bridges)) return false;
   bridge_instance *b = bridges[slot];
   if (!b || !b->online || b->pid <= 0) return false;
   if (hz) *hz = b->frequency_hz;
   if (mode) *mode = b->mode;
   return true;
}
bool rrclient_local_bridge_state(uint64_t *hz, const char **mode) {
   return rrclient_local_bridge_state_vfo(rrclient_room_active_vfo("&local-sdr"), hz, mode);
}
bool rrclient_local_bridge_tune_vfo(char vfo, uint64_t hz, const char *mode) {
   unsigned slot = (unsigned)(vfo - 'A');
   if (slot >= G_N_ELEMENTS(bridges)) return false;
   bridge_instance *b = bridges[slot];
   const char *reason = !b ? "VFO has no bridge" :
      !b->online ? "receiver offline" :
      b->control < 0 ? "control channel unavailable" :
      !(b->caps.flags & RRP_BRIDGE_RX) ? "RX capability not advertised" :
      hz < b->caps.rx_min_hz || hz > b->caps.rx_max_hz ? "frequency outside supported range" :
      !mode || !rrp_bridge_mode_valid(b->caps.rx_modes, mode) ? "mode unsupported by receiver" : NULL;
   if (reason) {
      Log(LOG_WARN, "local.bridge", "local.sdr.%c tune rejected: %s (hz=%llu mode=%s caps=%u range=%llu-%llu modes='%s')",
         vfo, reason, (unsigned long long)hz, mode ? mode : "(null)",
         b ? b->caps.flags : 0u,
         (unsigned long long)(b ? b->caps.rx_min_hz : 0),
         (unsigned long long)(b ? b->caps.rx_max_hz : 0),
         b ? b->caps.rx_modes : "");
      ui_print("&local-sdr", "local.sdr.%c: cannot tune: %s", vfo, reason);
      return false;
   }
   if (b->pending) {
      b->queued_tune = true;
      b->queued_hz = hz;
      g_strlcpy(b->queued_mode, mode, sizeof(b->queued_mode));
      return true;
   }
   rrp_bridge_ipc_message_t command;
   rrp_bridge_ipc_init(&command, RRP_BRIDGE_IPC_SET);
   g_strlcpy(command.name, "local", sizeof(command.name));
   g_strlcpy(command.driver, b->driver, sizeof(command.driver));
   command.request = ++b->serial;
   command.frequency_hz = hz;
   g_strlcpy(command.mode, mode, sizeof(command.mode));
   if (!rrp_bridge_ipc_send(b->control, &command)) return false;
   b->pending = command.request;
   b->pending_hz = hz;
   g_strlcpy(b->pending_mode, mode, sizeof(b->pending_mode));
   return true;
}
bool rrclient_local_bridge_tune(uint64_t hz, const char *mode) {
   return rrclient_local_bridge_tune_vfo(rrclient_room_active_vfo("&local-sdr"), hz, mode);
}
/* Frontend-independent controls: GTK, TUI and commands all target a VFO ID. */
bool rrclient_local_bridge_set_frequency(char vfo, uint64_t hz) {
   unsigned slot = (unsigned)(vfo - 'A');
   if (slot >= G_N_ELEMENTS(bridges) || !bridges[slot]) return false;
   bridge_instance *b = bridges[slot];
   const char *mode = b->queued_tune ? b->queued_mode :
      b->pending ? b->pending_mode : b->mode;
   return rrclient_local_bridge_tune_vfo(vfo, hz, mode);
}
bool rrclient_local_bridge_set_mode(char vfo, const char *mode) {
   unsigned slot = (unsigned)(vfo - 'A');
   if (slot >= G_N_ELEMENTS(bridges) || !bridges[slot]) return false;
   bridge_instance *b = bridges[slot];
   uint64_t hz = b->queued_tune ? b->queued_hz :
      b->pending ? b->pending_hz : b->frequency_hz;
   return hz && rrclient_local_bridge_tune_vfo(vfo, hz, mode);
}
bool rrclient_local_bridge_set_width(char vfo, const char *width) {
   /* The bridge SET IPC currently carries frequency and mode, not passband.
    * Do not silently report success or send a command to a remote rig. */
   Log(LOG_WARN, "local.bridge", "local.sdr.%c cannot set width %s: bridge protocol has no width field",
      vfo, width ? width : "(null)");
   ui_print("&local-sdr", "local.sdr.%c: width control is not supported by this bridge protocol yet", vfo);
   return false;
}

/* The local room uses the same chat command spelling as remote rig rooms.
 * This dispatcher is frontend-agnostic; commands never cross the wire. */
static bool local_rig_action(const rr_rig_cmd_t *cmd, void *unused) {
   (void)unused;
   char vfo = rrclient_room_active_vfo("&local-sdr");
   if (cmd->kind == RR_RIG_STATUS) {
      for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
         bridge_instance *b = bridges[i];
         if (b)
            ui_print("&local-sdr", "local.sdr.%c: %s, %llu Hz %s (%s)", 'A' + i,
               b->online ? "online" : "connecting", (unsigned long long)b->frequency_hz,
               *b->mode ? b->mode : "unknown", b->url ? b->url : "");
      }
      return true;
   }
   if (cmd->kind == RR_RIG_VFO) {
      char target = g_ascii_toupper(cmd->value[0]);
      if (cmd->value[1] || target < 'A' || target > 'Z' ||
          !rrclient_local_bridge_state_vfo(target, NULL, NULL)) {
         ui_print("&local-sdr", "VFO %s is not connected", cmd->value);
         return false;
      }
      rrclient_room_set_active_vfo("&local-sdr", target);
      event_emit("client.local-bridge.vfo", NULL, NULL);
      ui_print("&local-sdr", "Selected local.sdr.%c", target);
      return true;
   }
   bool ok = false;
   if (cmd->kind == RR_RIG_FREQ) {
      uint64_t hz;
      ok = rr_rig_parse_frequency(cmd->value, &hz) &&
         rrclient_local_bridge_set_frequency(vfo, hz);
   } else if (cmd->kind == RR_RIG_MODE) {
      ok = rrclient_local_bridge_set_mode(vfo, cmd->value);
   } else if (cmd->kind == RR_RIG_WIDTH) {
      ok = rrclient_local_bridge_set_width(vfo, cmd->value);
   }
   if (!ok)
      ui_print("&local-sdr", "local.sdr.%c: failed to apply command value %s", vfo, cmd->value);
   return ok;
}
bool rrclient_local_bridge_command(const char *line) {
   if (!line || *line != '!') return false;
   if (rr_rig_cmd_kind(line + 1) < 0) {
      /* The first keyword may be followed by whitespace. */
      char name[32]; size_t n = 0;
      for (const char *p = line + 1; *p && !isspace((unsigned char)*p) && n + 1 < sizeof(name); p++)
         name[n++] = *p;
      name[n] = '\0';
      if (rr_rig_cmd_kind(name) < 0) return false;
   }
   if (!rr_rig_parse_commands(line, local_rig_action, NULL))
      ui_print("&local-sdr", "Invalid, unsupported or failed local rig command: %s", line);
   return true;
}
bool rrclient_local_bridge_cli(int argc, char **argv) {
   if (argc < 3) {
      ui_print("&local-sdr", "Usage: /bridge <driver> list [c]|open <url>|close <VFO|url|all>|caps [VFO]");
      return true;
   }
   const char *driver = argv[1], *action = argv[2];
   if (!g_ascii_strcasecmp(action, "list") || !g_ascii_strcasecmp(action, "l")) {
      bool connected = argc > 3 && (!g_ascii_strcasecmp(argv[3], "c") || !g_ascii_strcasecmp(argv[3], "connected"));
      unsigned count = 0;
      for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
         bridge_instance *b = bridges[i];
         if (!b || g_ascii_strcasecmp(b->driver, driver) || (connected && !b->online)) continue;
         ui_print("&local-sdr", "%c: %s %s (%s)", 'A' + i, b->url ? b->url : "",
            b->online ? "connected" : "connecting", b->stream);
         count++;
      }
      if (!count) ui_print("&local-sdr", "No %s bridges %s", driver, connected ? "connected" : "open");
   } else if (!g_ascii_strcasecmp(action, "open") || !g_ascii_strcasecmp(action, "o")) {
      if (argc != 4 || !rrclient_local_bridge_connect_driver(argv[3], driver))
         ui_print("&local-sdr", "Usage: /bridge %s open <http[s]://host:port> (connection failed or invalid URL)", driver);
   } else if (!g_ascii_strcasecmp(action, "close") || !g_ascii_strcasecmp(action, "c")) {
      if (argc != 4) { ui_print("&local-sdr", "Usage: /bridge %s close <VFO|url|all>", driver); return true; }
      unsigned count = 0;
      for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
         bridge_instance *b = bridges[i];
         if (!b || g_ascii_strcasecmp(b->driver, driver)) continue;
         if (g_ascii_strcasecmp(argv[3], "all") &&
             !(strlen(argv[3]) == 1 && g_ascii_toupper(argv[3][0]) == 'A' + (int)i) &&
             (!b->url || strcmp(b->url, argv[3]))) continue;
         bridge_stop(b);
         count++;
      }
      if (!count) ui_print("&local-sdr", "No matching %s bridge", driver);
   } else if (!g_ascii_strcasecmp(action, "caps") || !g_ascii_strcasecmp(action, "capabilities")) {
      for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
         bridge_instance *b = bridges[i];
         if (!b || g_ascii_strcasecmp(b->driver, driver) ||
             (argc > 3 && !(strlen(argv[3]) == 1 && g_ascii_toupper(argv[3][0]) == 'A' + (int)i))) continue;
         ui_print("&local-sdr", "%c: flags=0x%x RX=%llu-%llu Hz modes=%s waterfall=%s zoom=%u..%u",
            'A' + i, b->caps.flags, (unsigned long long)b->caps.rx_min_hz,
            (unsigned long long)b->caps.rx_max_hz, b->caps.rx_modes,
            (b->caps.flags & RRP_BRIDGE_WATERFALL) ? "yes" : "no",
            b->caps.waterfall_zoom_min, b->caps.waterfall_zoom_max);
      }
   } else ui_print("&local-sdr", "Unknown bridge action: %s", action);
   return true;
}
size_t rrclient_local_bridge_streams(char ids[][64], char vfos[], size_t capacity) {
   size_t count = 0;
   for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
      if (!bridges[i] || !bridges[i]->online) continue;
      if (count < capacity) {
         if (ids) g_strlcpy(ids[count], bridges[i]->stream, 64);
         if (vfos) vfos[count] = (char)('A' + i);
      }
      count++;
   }
   return count;
}
static void local_pcm(const char *name, const void *pcm, size_t bytes, void *user) {
   (void)name;
   bridge_instance *b = user;
   if (b && b->online && bytes && !(bytes % sizeof(int16_t)))
      audio_local_rx_push(b->stream, pcm, bytes / sizeof(int16_t));
}
/* A disconnected bridge may ignore SIGTERM. Reap only in the child watch,
 * and escalate without blocking GTK or signaling an unrelated reused PID. */
static gboolean bridge_kill_timeout(gpointer user) {
   bridge_instance *b = user;
   b->kill_timer = 0;
   if (b->process.pid > 0 && b->pid > 0)
      kill(b->process.pid, SIGKILL);
   return G_SOURCE_REMOVE;
}
static void bridge_stop(bridge_instance *b) {
   if (!b) return;
   if (b->timer) { g_source_remove(b->timer); b->timer = 0; }
   b->queued_tune = false;
   if (b->control >= 0) {
      rrp_bridge_ipc_message_t m;
      rrp_bridge_ipc_init(&m, RRP_BRIDGE_IPC_SHUTDOWN);
      rrp_bridge_ipc_send(b->control, &m);
      close(b->control); b->control = -1;
   }
   if (b->media >= 0) { close(b->media); b->media = -1; }
   if (b->spectrum >= 0) { close(b->spectrum); b->spectrum = -1; }
   if (b->pcm_started) { fwdsp_processor_stop(b->resampler); b->pcm_started = false; }
   audio_local_rx_stop(b->stream);
   if (b->config) { g_unlink(b->config); g_clear_pointer(&b->config, g_free); }
   g_clear_pointer(&b->url, g_free);
   if (b->online || b->pid > 0)
      ui_print("&local-sdr", "%s: disconnected", b->stream);
   b->online = false;
   if (b->process.pid > 0) {
      kill(b->process.pid, SIGTERM);
      if (!b->kill_timer)
         b->kill_timer = g_timeout_add_seconds(2, bridge_kill_timeout, b);
   }
   b->pid = 0;
   if (b->slot < G_N_ELEMENTS(bridges) && bridges[b->slot] == b)
      bridges[b->slot] = NULL;
   local_room_refresh();
}
void rrclient_local_bridge_disconnect_url(const char *url) {
   if (!url) return;
   for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++) {
      bridge_instance *b = bridges[i];
      if (b && b->url && !strcmp(b->url, url)) { bridge_stop(b); return; }
   }
}
void rrclient_local_bridge_disconnect(void) {
   for (unsigned i = 0; i < G_N_ELEMENTS(bridges); i++)
      if (bridges[i]) bridge_stop(bridges[i]);
}
static void local_child_exited(GPid pid, gint status, gpointer user) {
   (void)status;
   bridge_instance *b = user;
   if (b && b->pid == pid) {
      Log(LOG_WARN, "local.bridge", "Local SDR process %ld exited (status %d)", (long)pid, status);
      ui_print("&local-sdr", "%s: rrbridge exited unexpectedly (status %d)", b->stream, status);
      bridge_stop(b);
   }
   if (b) {
      if (b->kill_timer) { g_source_remove(b->kill_timer); b->kill_timer = 0; }
      b->process.pid = -1; /* GChildWatch already reaped this child. */
      subproc_close(&b->process);
   }
   g_free(b);
   g_spawn_close_pid(pid);
}
static gboolean poll_local(gpointer unused) {
   bridge_instance *b = unused;
   if (b->pid <= 0) return G_SOURCE_REMOVE;
   for (unsigned n = 0; n < 16; n++) {
      rrp_bridge_ipc_message_t m;
      int got = rrp_bridge_ipc_recv(b->control, &m);
      if (!got) break;
      if (got < 0) {
         ui_print("&local-sdr", "%s: rrbridge control channel closed or failed", b->stream);
         b->timer = 0;
         bridge_stop(b);
         return G_SOURCE_REMOVE;
      }
      if (m.type == RRP_BRIDGE_IPC_CAPABILITIES) {
         b->caps = m;
      } else if (m.type == RRP_BRIDGE_IPC_RESULT) {
         if (m.request == b->pending) {
            b->pending = 0;
            if (!m.success) {
               Log(LOG_WARN, "local.bridge", "%s tuning request rejected", b->stream);
               ui_print("&local-sdr", "%s: tuning rejected by receiver", b->stream);
            }
            if (b->queued_tune) {
               uint64_t hz = b->queued_hz;
               char mode[sizeof(b->queued_mode)];
               g_strlcpy(mode, b->queued_mode, sizeof(mode));
               b->queued_tune = false;
               rrclient_local_bridge_tune_vfo((char)('A' + b->slot), hz, mode);
            }
         }
      } else if (m.type == RRP_BRIDGE_IPC_STATE) {
         bool was_online = b->online;
         b->online = m.available != 0;
         if (b->online) {
            b->frequency_hz = m.frequency_hz;
            g_strlcpy(b->mode, m.mode, sizeof(b->mode));
            event_emit("client.local-bridge.vfo", NULL, NULL);
         }
         if (b->online != was_online) local_room_refresh();
         if (b->online && !was_online) {
            receiver_directory_record_connection(b->url);
            Log(LOG_INFO, "local.bridge", "Local Kiwi online: %s", b->url);
            event_emit("client.receiver-directory.connection", NULL, "Local SDR connected.");
            ui_print("&local-sdr", "Connected: %s (%s)", b->stream, b->url ? b->url : "unknown");
            event_emit("client.audio.streams.changed", NULL, NULL);
         }
      } else if (m.type == RRP_BRIDGE_IPC_LOG) {
         Log(LOG_INFO, "local.bridge", "%s", m.metadata_value);
         ui_print("&local-sdr", "%s: %s", b->stream, m.metadata_value);
      }
   }
   for (unsigned n = 0; n < 16; n++) {
      struct rrp_bridge_pcm frame;
      int got = rrp_bridge_media_recv(b->media, &frame);
      if (!got) break;
      if (got < 0) {
         ui_print("&local-sdr", "%s: rrbridge media channel closed or failed", b->stream);
         b->timer = 0;
         bridge_stop(b);
         return G_SOURCE_REMOVE;
      }
      if (!b->online || !frame.count) continue;
      if (!b->pcm_started || b->rate != frame.rate) {
         if (b->pcm_started) {
            fwdsp_processor_stop(b->resampler);
            b->pcm_started = false;
         }
         gint64 current = g_get_monotonic_time();
         if (current < b->resampler_retry_at) continue;
         b->pcm_started = fwdsp_audio_resampler_start(b->resampler, frame.rate, local_pcm, b);
         b->rate = frame.rate;
         b->resampler_retry_at = b->pcm_started ? 0 : current + 5000000;
         if (!b->pcm_started)
            ui_print("&local-sdr", "%s: PCM resampler failed for input rate %u Hz; retrying", b->stream, frame.rate);
         Log(b->pcm_started ? LOG_INFO : LOG_WARN, "local.bridge",
             "Local PCM resampler %s (input=%u Hz, output=16000 Hz)%s",
             b->pcm_started ? "started" : "failed", frame.rate,
             b->pcm_started ? "" : "; retrying in 5 seconds");
      }
      if (b->pcm_started) {
         uint8_t samples[2 * RRP_BRIDGE_MEDIA_SAMPLES];
         for (size_t i = 0; i < frame.count; i++) {
            samples[2*i] = (uint8_t)frame.samples[i];
            samples[2*i+1] = (uint16_t)frame.samples[i] >> 8;
         }
         if (!fwdsp_processor_write(b->resampler, samples, frame.count * 2)) {
            Log(LOG_WARN, "local.bridge", "Local PCM resampler input write failed");
            ui_print("&local-sdr", "%s: PCM resampler input write failed", b->stream);
            fwdsp_processor_stop(b->resampler);
            b->pcm_started = false;
            b->resampler_retry_at = g_get_monotonic_time() + 5000000;
         }
      }
   }
   /* Consume a bounded number of RF rows per tick; old rows are
    * discarded by the seqpacket sender when the UI falls behind. */
   for (unsigned i = 0; i < 4 && b->spectrum >= 0; i++) {
      rr_bridge_spectrum_row row;
      int received = rr_bridge_spectrum_recv(b->spectrum, &row);
      if (received == 0) break;
      if (received < 0) {
         ui_print("&local-sdr", "%s: waterfall channel closed or failed", b->stream);
         close(b->spectrum);
         b->spectrum = -1;
         break;
      }
      if (rrclient_room_active_vfo("&local-sdr") == (char)('A' + b->slot))
         event_emit_binary(RR_BRIDGE_SPECTRUM_EVENT, NULL, &row, sizeof(row));
   }
   return G_SOURCE_CONTINUE;
}
bool rrclient_local_bridge_connect_driver(const char *url, const char *driver) {
   if (!driver || !*driver) driver = "kiwi";
   if (strlen(driver) > 32 ||
       strspn(driver, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(driver))
      return false;
   if (!url) return false;
   if (rrclient_local_bridge_connected(url)) return true;
   GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
   if (!uri) return false;
   const char *scheme = g_uri_get_scheme(uri), *host = g_uri_get_host(uri);
   int port = g_uri_get_port(uri);
   bool tls = scheme && !g_ascii_strcasecmp(scheme, "https");
   bool valid = scheme && (!g_ascii_strcasecmp(scheme, "http") || !g_ascii_strcasecmp(scheme, "https")) &&
      host && *host && strspn(host, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-") == strlen(host);
   if (!valid) {
      g_uri_unref(uri);
      return false;
   }
   if (port <= 0) port = tls ? 443 : 80;
   char *program = g_find_program_in_path("rrbridge");
   if (!program && g_file_test("./bin/rrbridge", G_FILE_TEST_IS_EXECUTABLE))
      program = g_strdup("./bin/rrbridge");
   if (!program) {
      Log(LOG_WARN, "local.bridge", "rrbridge executable unavailable");
      g_uri_unref(uri);
      return false;
   }
   unsigned slot = 0;
   while (slot < G_N_ELEMENTS(bridges) && bridges[slot]) slot++;
   if (slot == G_N_ELEMENTS(bridges)) {
      g_free(program); g_uri_unref(uri); return false;
   }
   bridge_instance *b = g_new0(bridge_instance, 1);
   b->slot = slot;
   g_strlcpy(b->driver, driver, sizeof(b->driver));
   b->control = b->media = b->spectrum = -1;
   b->process = (subproc_t) SUBPROC_INIT;
   g_snprintf(b->stream, sizeof(b->stream), "local.sdr.%c", 'A' + slot);
   g_snprintf(b->resampler, sizeof(b->resampler), "proc.local.sdr.%c", 'A' + slot);
   bridges[slot] = b;
   char *config = NULL;
   int tmp = g_file_open_tmp("rustyrig-local-bridge-XXXXXX", &config, NULL);
   if (tmp < 0) {
      g_free(program);
      g_uri_unref(uri);
      bridge_stop(b); g_free(b); return false;
   }
   close(tmp);
   char *text = g_strdup_printf("[bridge]\nmodules = auto\n[bridge:local]\ndriver=%s\nhost=%s\nport=%d\nsecure=%s\nwaterfall=true\nfrequency=14074000\nmode=USB\n", driver, host, port, tls ? "true" : "false");
   bool written = g_file_set_contents(config, text, -1, NULL);
   g_free(text);
   g_uri_unref(uri);
   if (!written) {
      g_unlink(config);
      g_free(config);
      g_free(program);
      bridge_stop(b); g_free(b); return false;
   }
   int control[2], media[2], spectrum[2];
   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control)) {
      g_unlink(config); g_free(config); g_free(program);
      bridge_stop(b); g_free(b); return false;
   }
   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, media)) {
      close(control[0]); close(control[1]);
      g_unlink(config); g_free(config); g_free(program);
      bridge_stop(b); g_free(b); return false;
   }
   if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, spectrum)) {
      close(control[0]); close(control[1]); close(media[0]); close(media[1]);
      g_unlink(config); g_free(config); g_free(program);
      bridge_stop(b); g_free(b); return false;
   }
   const char *const argv[] = { program, "--config", config, "--bridge", "local", NULL };
   subproc_fd_t mappings[] = {
      { control[1], RRP_BRIDGE_IPC_FD },
      { media[1], RRP_BRIDGE_MEDIA_FD },
      { spectrum[1], RR_BRIDGE_SPECTRUM_FD }
   };
   int closes[] = { control[0], media[0], spectrum[0], control[1], media[1], spectrum[1] };
   bool spawned = subproc_spawn_fds(&b->process, program, argv,
      mappings, G_N_ELEMENTS(mappings), closes, G_N_ELEMENTS(closes));
   close(control[1]);
   close(media[1]);
   close(spectrum[1]);
   g_free(program);
   if (!spawned) {
      close(control[0]); close(media[0]); close(spectrum[0]);
      g_unlink(config); g_free(config);
      Log(LOG_WARN, "local.bridge", "Unable to spawn rrbridge");
      bridge_stop(b); g_free(b); return false;
   }
   pid_t pid = b->process.pid;
   fcntl(control[0], F_SETFL, fcntl(control[0], F_GETFL) | O_NONBLOCK);
   fcntl(media[0], F_SETFL, fcntl(media[0], F_GETFL) | O_NONBLOCK);
   fcntl(spectrum[0], F_SETFL, fcntl(spectrum[0], F_GETFL) | O_NONBLOCK);
   b->control = control[0];
   b->media = media[0];
   b->spectrum = spectrum[0];
   b->pid = pid;
   b->url = g_strdup(url);
   b->config = config;
   audio_local_rx_start(b->stream);
   g_child_watch_add(pid, local_child_exited, b);
   b->timer = g_timeout_add(10, poll_local, b);
   rrclient_room_set_active_vfo("&local-sdr", (char)('A' + slot));
   char *status = g_strdup_printf("Connecting to %s at %s", driver, url);
   event_emit("client.receiver-directory.connection", NULL, status);
   ui_print("&local-sdr", "%s", status);
   g_free(status);
   Log(LOG_INFO, "local.bridge", "Started local %s bridge pid=%ld for %s", driver, (long)pid, url);
   return true;
}

bool rrclient_local_bridge_connect(const char *url) {
   return rrclient_local_bridge_connect_driver(url, "kiwi");
}
