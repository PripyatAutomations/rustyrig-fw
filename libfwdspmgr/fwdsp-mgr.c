//
// libfwdsp/fwpdsp-mgr.c: Deal with starting and stopping fwdsp instances as needed
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// codec negotiation should call fwdsp_create
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#endif
#include <librustyaxe/core.h>
#include <librustyaxe/reactor.glib.h>
#include <librustyaxe/util.file.h>
#include <librrprotocol/rrprotocol.h>
#include <fwdsp/default-pipelines.h>
#include <libfwdspmgr/pcm-backlog.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <libfwdspmgr/fwdsp-ctl.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#define FWDSP_MAX_SUBPROCS 100

defconfig_t defcfg_fwdsp[] = {
   {
      "codecs.allowed", FWDSP_DEFAULT_CODECS, "Preferred codecs"
   },
#ifdef _WIN32
   {
      "fwdsp:path", "bin/fwdsp.exe", "Path to fwdsp binary"
   },
#else
   {
      "fwdsp:path", "bin/fwdsp", "Path to fwdsp binary"
   },
#endif
   {
      "fwdsp:recording.path", "$HOME/recordings", "Path to audio recordings"
   },
   {
      "recording.codec", "ogg", "Recording container/codec: flac or ogg"
   },
   {
      "recording.codec.modem", "flac", "Recording codec for modem recordings: flac or ogg"
   },
   {
      "fwdsp:recording.rx", "false", "Record received audio"
   },
   {
      "fwdsp:recording.tx", "false", "Record transmitted audio"
   },
   {
      "fwdsp:subproc.max", "16", "Maximum allowed de/encoder processes"
   },
   {
      "fwdsp:hangtime", "60", "How long to keep unused encoders alive after last use; decoders stop immediately"
   },
   {
      NULL, NULL, NULL
   }
};

const char *fwdsp_path = NULL;
bool fwdsp_mgr_ready = false;
static int active_slots = 0;
static int max_subprocs = FWDSP_MAX_SUBPROCS;
static struct fwdsp_subproc *fwdsp_subprocs;
static int next_channel_id = 1;
/* Keep restart history outside reusable fwdsp slots so a crashed child
 * cannot bypass the guard simply by obtaining a fresh slot. */
#define FWDSP_SPAWN_GUARDS 128
struct fwdsp_spawn_guard {
   char key[384];
   uint64_t window_started, blocked_until;
   unsigned attempts;
};
static struct fwdsp_spawn_guard spawn_guards[FWDSP_SPAWN_GUARDS];
static bool fwdsp_spawn_permitted(const struct fwdsp_subproc *sp) {
   char key[384];
   /* Group dynamically named endpoints by pipeline rather than by PID/slot. */
   if (sp->pipeline[0])
      snprintf(key, sizeof(key), "pipeline:%s", sp->pipeline);
   else if (sp->role == FWDSP_ROLE_PROCESSOR)
      snprintf(key, sizeof(key), "processor:%s", sp->processor_name);
   else
      snprintf(key, sizeof(key), "codec:%.4s:%c", sp->pl_id, sp->is_tx ? 'T' : 'R');
   uint64_t current = mono_us();
   struct fwdsp_spawn_guard *guard = NULL, *oldest = &spawn_guards[0];
   for (size_t i = 0; i < FWDSP_SPAWN_GUARDS; ++i) {
      struct fwdsp_spawn_guard *candidate = &spawn_guards[i];
      if (!strcmp(candidate->key, key)) { guard = candidate; break; }
      if (!candidate->key[0]) oldest = candidate;
      else if (oldest->key[0] && candidate->window_started < oldest->window_started)
         oldest = candidate;
   }
   if (!guard) {
      guard = oldest;
      memset(guard, 0, sizeof(*guard));
      snprintf(guard->key, sizeof(guard->key), "%s", key);
   }
   if (current < guard->blocked_until) return false;
   if (!guard->window_started || current - guard->window_started >= UINT64_C(30000000)) {
      guard->window_started = current;
      guard->attempts = 0;
   }
   if (guard->attempts >= 3) {
      guard->blocked_until = current + UINT64_C(60000000);
      Log(LOG_WARN, "fwdsp", "Spawn rate limit for %.120s: more than 3 starts in 30s; retry after 60s", key);
      return false;
   }
   guard->attempts++;
   return true;
}

extern const char *config_file; // librustyaxe/config.c
static fwdsp_exit_cb_t on_fwdsp_exit = NULL;
extern time_t now;

/* Local subprocess descriptors are not network transports. They use the
 * reusable GLib fd reactor directly, independently of WebSocket/TLS. */
struct fwdsp_io_conn {
   struct fwdsp_subproc *sp;
   rr_reactor_watch_t *watch;
   GByteArray *recv;
   int fd;
   bool is_stderr, processing, detached;
};
static bool fwdsp_fd_ready(int fd, GIOCondition events, void *arg);
static void fwdsp_reader_detach(struct fwdsp_io_conn *ctx);

static bool fwdsp_send_control(struct fwdsp_subproc *sp, uint8_t type, uint8_t value);
static bool fwdsp_destroy(struct fwdsp_subproc *sp);

static bool fwdsp_slot_active(const struct fwdsp_subproc *sp) {
   return sp && sp->role != FWDSP_ROLE_NONE;
}

static void fwdsp_subproc_exit_cb(struct fwdsp_subproc *sp, int status) {
   if (sp && sp->role == FWDSP_ROLE_PROCESSOR) {
      Log(LOG_INFO, "fwdsp", "Processor %s exited (status=%d)", sp->processor_name, status);
   } else if (sp) {
      Log(LOG_INFO, "fwdsp", "Pipeline %s.%s exited (status=%d)", sp->pl_id, sp->is_tx ? "tx" : "rx", status);
   }
   // XXX: notify websocket clients, or log, etc.
}

static void fwdsp_set_exit_cb(fwdsp_exit_cb_t cb) {
   on_fwdsp_exit = cb;
}

static volatile sig_atomic_t fwdsp_sigchld_pending = 0;

static void fwdsp_sigchld(int sig) {
   // Async-signal-safe: only set a flag. waitpid(), Log() and touching the
   // subprocs array happen in fwdsp_reap_children() on the main loop.
   fwdsp_sigchld_pending = 1;
}

#define FWDSP_FRAME_HEADER_SIZE 4
#define FWDSP_MAX_FRAME_SIZE (64U * 1024U * 1024U)
#define FWDSP_FRAME_STREAM_HEADER 0x80000000U
#define FWDSP_FRAME_PCM_TAP 0x40000000U
#define FWDSP_FRAME_DISCONT 0x20000000U
#define FWDSP_FRAME_LENGTH_MASK 0x1FFFFFFFU

static void frame_length_encode(uint8_t header[FWDSP_FRAME_HEADER_SIZE], uint32_t len) {
   header[0] = (uint8_t) (len >> 24);
   header[1] = (uint8_t) (len >> 16);
   header[2] = (uint8_t) (len >> 8);
   header[3] = (uint8_t) len;
}

static uint32_t frame_length_decode(const uint8_t header[FWDSP_FRAME_HEADER_SIZE]) {
   return ((uint32_t) header[0] << 24) | ((uint32_t) header[1] << 16) | ((uint32_t) header[2] << 8) | (uint32_t) header[3];
}

static bool fwdsp_write_all(int fd, const uint8_t *data, size_t len) {
   while (len > 0) {
      ssize_t written = write(fd, data, len);

      if (written > 0) {
         data += written;
         len -= (size_t)written;
      } else if (written < 0 && (errno == EINTR)) {
         continue;
      } else if (written < 0 && (errno == EAGAIN || (EWOULDBLOCK != EAGAIN && errno == EWOULDBLOCK))) {
#ifndef _WIN32
         /* Never let a stalled GStreamer child block the server event loop. A media frame may be dropped under backpressure; the caller will retire the
          * unhealthy child and recreate it on the next tick. */
         struct pollfd pfd = {
            .fd = fd, .events = POLLOUT
         };
         int ready;
         do {
            ready = poll(&pfd, 1, 5);
         } while (ready < 0 && errno == EINTR);

         if (ready > 0 && (pfd.revents & POLLOUT)) {
            continue;
         }
#endif

         return false;
      } else {
         return false;
      }
   }
   return true;
}

static bool fwdsp_write_frame_flags(int fd, const uint8_t *data, size_t len, uint32_t flags) {
   uint8_t header[FWDSP_FRAME_HEADER_SIZE];

   if (!data || len == 0 || len > UINT32_MAX || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }

   frame_length_encode(header, (uint32_t) len | flags);

   if (!fwdsp_write_all(fd, header, sizeof(header))) {
      return false;
   }

   return fwdsp_write_all(fd, data, len);
}

static bool fwdsp_write_frame(int fd, const uint8_t *data, size_t len) {
   return fwdsp_write_frame_flags(fd, data, len, 0);
}

/* Count complete newer decoded samples already received from a child. Keep encoded packets untouched; the PCM playout path may discard old sound. */
static size_t newer_pcm_bytes(const uint8_t *data, size_t size, size_t position) {
   size_t bytes = 0;
   while (position + FWDSP_FRAME_HEADER_SIZE <= size) {
      uint32_t flags = frame_length_decode(data + position);
      size_t len = flags & FWDSP_FRAME_LENGTH_MASK;

      if (!len || len > size - position - FWDSP_FRAME_HEADER_SIZE) {
         break;
      }

      if ((flags & FWDSP_FRAME_PCM_TAP) && !(flags & FWDSP_FRAME_STREAM_HEADER) && !(len & 1)) {
         bytes += len;

         if (bytes >= FWDSP_PCM_BACKLOG_BYTES) {
            return FWDSP_PCM_BACKLOG_BYTES;
         }
      }
      position += FWDSP_FRAME_HEADER_SIZE + len;
   }
   return bytes;
}

// Called from the main event loop; reaps dead fwdsp children safely.
void fwdsp_reap_children(void) {
   if (!fwdsp_sigchld_pending) {
      return;
   }
   fwdsp_sigchld_pending = 0;

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];
      int status;

      if (subproc_reap(&sp->process, &status)) {
         if (fwdsp_slot_active(sp)) {
            char name[100];
            if (sp->role == FWDSP_ROLE_PROCESSOR)
               snprintf(name, sizeof(name), "%s", sp->processor_name);
            else
               snprintf(name, sizeof(name), "%.4s.%s", sp->pl_id, sp->is_tx ? "tx" : "rx");
            if (WIFSIGNALED(status))
               Log(LOG_WARN, "fwdsp", "Pipeline %s pid=%ld terminated by signal %d",
                  name, (long)sp->process.pid, WTERMSIG(status));
            else if (WIFEXITED(status) && WEXITSTATUS(status))
               Log(LOG_WARN, "fwdsp", "Pipeline %s pid=%ld exited with failure status %d",
                  name, (long)sp->process.pid, WEXITSTATUS(status));
            else
               Log(LOG_INFO, "fwdsp", "Pipeline %s pid=%ld exited", name, (long)sp->process.pid);
         }
         if (on_fwdsp_exit) {
            on_fwdsp_exit(sp, status);
         }
         fwdsp_destroy(sp);
      }
   }
}

// Header packets keep their existing encoded payload on the network. Only
// child IPC carries the marker used to cache them for late subscribers.
static void fwdsp_replay_stream_headers(struct fwdsp_subproc *sp, struct rr_mediachan *channel, rrconn_t *cptr) {
   for (size_t pos = 0 ; pos + 4 <= sp->stream_headers_len ; ) {
      uint32_t len = frame_length_decode(sp->stream_headers + pos);
      pos += 4;

      if (len > sp->stream_headers_len - pos) {
         break;
      }
      ws_media_send_setup_frame(channel, cptr, sp->stream_headers + pos, len, sp->pl_id);
      pos += len;
   }
}

void fwdsp_send_stream_headers(const char *uuid, rrconn_t *cptr) {
   struct rr_mediachan *channel = media_chan_find_uuid(uuid);

   if (!channel || channel->direction != RR_BINFRAME_DIR_RX) {
      return;
   }
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(channel->codec, true, uuid);

   // A resumed encoder will replay to all subscribers before its next packet.
   if (sp && !sp->replay_headers) {
      fwdsp_replay_stream_headers(sp, channel, cptr);
   }
}

static void fwdsp_parse_read(struct fwdsp_io_conn *ctx) {
   if (!ctx->sp || !ctx->recv) return;

      if (ctx->is_stderr) {
         for ( ;; ) {
            uint8_t *newline = memchr(ctx->recv->data, '\n', ctx->recv->len);

            if (!newline) {
               break;
            }

            size_t line_len = (size_t)(newline - (uint8_t *) ctx->recv->data);
            size_t consumed = line_len + 1;

            if (line_len > 0 && ctx->recv->data[line_len - 1] == '\r') {
               line_len--;
            }

            char *message = malloc(line_len + 1);

            if (!message) {
               Log(LOG_CRIT, "fwdsp", "OOM buffering fwdsp stderr");
               g_byte_array_remove_range(ctx->recv, 0, consumed);
               continue;
            }

            memcpy(message, ctx->recv->data, line_len);
            message[line_len] = '\0';
            /* A pipeline's actual GStreamer failure must never be hidden
             * behind the later EPIPE/decoder restart warning. Other verbose
             * stderr remains at debug level. */
            bool important = strstr(message, "GStreamer error:") ||
               strstr(message, "GStreamer pipeline parse error:") ||
               strstr(message, "GStreamer details:") ||
               strstr(message, "Failed to set pipeline") ||
               strstr(message, "CRIT") || strstr(message, "ERROR");
            if (important)
               Log(LOG_WARN, "fwdsp", "[%s.%s pid=%ld stderr] %.700s",
                  ctx->sp->role == FWDSP_ROLE_PROCESSOR ? ctx->sp->processor_name : ctx->sp->pl_id,
                  ctx->sp->role == FWDSP_ROLE_PROCESSOR ? "processor" : ctx->sp->is_tx ? "tx" : "rx",
                  (long)ctx->sp->process.pid, message);
            else
               Log(LOG_DEBUG, "fwdsp", "[stderr %s]", message);
            free(message);

            g_byte_array_remove_range(ctx->recv, 0, consumed);
         }

         if (ctx->recv->len > FWDSP_MAX_FRAME_SIZE) {
            Log(LOG_WARN, "fwdsp", "Discarding oversized unterminated stderr data");
            g_byte_array_remove_range(ctx->recv, 0, ctx->recv->len);
         }
      } else {
         while (ctx->sp && ctx->recv->len >= FWDSP_FRAME_HEADER_SIZE) {
            uint32_t frame_flags = frame_length_decode((const uint8_t *)ctx->recv->data);
            bool is_header = (frame_flags & FWDSP_FRAME_STREAM_HEADER) != 0;
            bool is_pcm_tap = (frame_flags & FWDSP_FRAME_PCM_TAP) != 0;
            uint32_t frame_len = frame_flags & FWDSP_FRAME_LENGTH_MASK;

            if (frame_len == 0 || frame_len > FWDSP_MAX_FRAME_SIZE) {
               Log(LOG_CRIT, "fwdsp", "Invalid frame length %u from %s.%s", frame_len, ctx->sp->role == FWDSP_ROLE_PROCESSOR ?
                  ctx->sp->processor_name : ctx->sp->pl_id, ctx->sp->is_tx ? "tx" : "rx");
               g_byte_array_remove_range(ctx->recv, 0, ctx->recv->len);
               break;
            }

            size_t total_len = FWDSP_FRAME_HEADER_SIZE + (size_t)frame_len;

            if (ctx->recv->len < total_len) {
               break;
            }

            if (is_pcm_tap) {
               struct fwdsp_subproc *sp = ctx->sp;

               if (is_header || (frame_len & 1U) != 0) {
                  Log(LOG_WARN, "fwdsp", "Discarding malformed PCM tap frame from %s.%s", sp->pl_id, sp->is_tx ? "tx" : "rx");
               } else if (sp->role == FWDSP_ROLE_CODEC && !sp->is_tx && sp->processor_output) {
                  fwdsp_processor_output_cb output_cb = sp->processor_output;
                  void *output_data = sp->processor_output_data;
                  char name[sizeof(sp->channel_uuid)];
                  snprintf(name, sizeof(name), "%s", sp->channel_uuid);
                  size_t newer = newer_pcm_bytes(ctx->recv->data, ctx->recv->len, total_len);
                  size_t keep = FWDSP_PCM_BACKLOG_BYTES - newer;

                  if (keep > frame_len) {
                     keep = frame_len;
                  }

                  if (keep) {
                     output_cb(name, (const uint8_t *)ctx->recv->data + FWDSP_FRAME_HEADER_SIZE + frame_len - keep, keep, output_data);
                  }
               }
               g_byte_array_remove_range(ctx->recv, 0, total_len);
               continue;
            }

            if (ctx->sp->role == FWDSP_ROLE_PROCESSOR) {
               struct fwdsp_subproc *sp = ctx->sp;

               if (is_header) {
                  Log(LOG_WARN, "fwdsp", "Ignoring codec header from PCM processor %s", sp->processor_name);
               } else if ((frame_len & 1U) != 0) {
                  Log(LOG_WARN, "fwdsp", "Discarding odd-byte PCM output from processor %s", sp->processor_name);
               } else if (sp->processor_io == FWDSP_PROCESSOR_IO_PLAYBACK) {
                  Log(LOG_WARN, "fwdsp", "Discarding unexpected PCM output from playback endpoint %s", sp->processor_name);
               } else if (sp->processor_output) {
                  // The callback may stop this processor, clearing its slot.
                  // Copy the name and callback state before entering user code.
                  char name[sizeof(sp->processor_name)];
                  snprintf(name, sizeof(name), "%s", sp->processor_name);
                  fwdsp_processor_output_cb output_cb = sp->processor_output;
                  void *output_data = sp->processor_output_data;
                  output_cb(name, (const uint8_t *)ctx->recv->data + FWDSP_FRAME_HEADER_SIZE, frame_len, output_data);
               }
               g_byte_array_remove_range(ctx->recv, 0, total_len);
               continue;
            }

            if (ctx->sp->is_video) {
               struct fwdsp_subproc *sp = ctx->sp;

               if (sp->refcount > 0) {
                  if (sp->processor_output) {
                     sp->processor_output(sp->channel_uuid, (const uint8_t *)ctx->recv->data + FWDSP_FRAME_HEADER_SIZE, frame_len, sp->processor_output_data);
                  } else if (frame_len <= RR_BINFRAME_MAX_PAYLOAD) {
                     char event[32];
                     snprintf(event, sizeof(event), "fwdsp.frame.%.4s", sp->pl_id);
                     event_emit_binary(event, NULL, ctx->recv->data + FWDSP_FRAME_HEADER_SIZE, frame_len);
                  }
               }
               g_byte_array_remove_range(ctx->recv, 0, total_len);
               continue;
            }

            if (is_header) {
               struct fwdsp_subproc *sp = ctx->sp;

               // Bound retained codec setup data, independent of media payload size.
               if (total_len <= 256 * 1024 - sp->stream_headers_len) {
                  uint8_t *headers = realloc(sp->stream_headers, sp->stream_headers_len + total_len);

                  if (headers) {
                     sp->stream_headers = headers;
                     frame_length_encode(headers + sp->stream_headers_len, frame_len);
                     memcpy(headers + sp->stream_headers_len + 4, ctx->recv->data + 4, frame_len);
                     sp->stream_headers_len += total_len;
                  }
               } else {
                  Log(LOG_WARN, "fwdsp", "Codec initialization data exceeds cache limit");
               }
            }

            struct rr_mediachan *channel = ctx->sp->channel_uuid[0] != '\0' ?
               media_chan_find_uuid(ctx->sp->channel_uuid) :
               media_chan_find(RR_BINFRAME_SUBSYS_AUDIO, ctx->sp->is_tx ? RR_BINFRAME_DIR_RX : RR_BINFRAME_DIR_TX, 0, 0);

            if (channel && ctx->sp->refcount > 0 && strncmp(channel->codec, ctx->sp->pl_id, 4) == 0) {
               if (ctx->sp->replay_headers) {
                  fwdsp_replay_stream_headers(ctx->sp, channel, NULL);
                  ctx->sp->replay_headers = false;
               }

               if (is_header) {
                  ws_media_send_setup_frame(channel, NULL, (const uint8_t *)ctx->recv->data + FWDSP_FRAME_HEADER_SIZE, frame_len, ctx->sp->pl_id);
               } else {
                  ws_media_broadcast_subscribed(channel, (const uint8_t *)ctx->recv->data + FWDSP_FRAME_HEADER_SIZE, frame_len, ctx->sp->pl_id);
               }
            } else if (!channel && ctx->sp->channel_uuid[0] != '\0' && ctx->sp->refcount > 0) {
               /* A pipeline can produce packets briefly while its media channel is being replaced or unsubscribed.  Do not emit a warning for every packet in
                * that transient state.  Idle encoders are intentionally retained for hangtime and may still drain a final buffer after their channel is gone.
                */
               if (ctx->sp->last_no_channel_warn == 0 || now < ctx->sp->last_no_channel_warn || now - ctx->sp->last_no_channel_warn >= 5) {
                  // Encoders intentionally linger for fwdsp:hangtime after
                  // their last subscriber leaves. Packets produced while
                  // that channel is being unsubscribed have no destination,
                  // but are harmless and should not look like a client error.
                  Log(LOG_DEBUG, "fwdsp", "No media channel for %s.%s output", ctx->sp->pl_id, ctx->sp->is_tx ? "tx" : "rx");
                  ctx->sp->last_no_channel_warn = now;
               }
            }

            g_byte_array_remove_range(ctx->recv, 0, total_len);
         }
      }

}
/* Detachment during a media callback cannot free the reader until the
 * current dispatch unwinds. The watch owns neither fd nor buffer. */
static void fwdsp_reader_release(struct fwdsp_io_conn *ctx) {
   if (!ctx) return;
   if (ctx->watch) { rr_reactor_watch_cancel(ctx->watch); ctx->watch = NULL; }
   if (ctx->processing) { ctx->detached = true; return; }
   if (ctx->fd >= 0) close(ctx->fd);
   if (ctx->recv) g_byte_array_unref(ctx->recv);
   free(ctx);
}
static void fwdsp_reader_detach(struct fwdsp_io_conn *ctx) {
   if (!ctx) return;
   struct fwdsp_subproc *sp = ctx->sp;
   if (sp) {
      if (ctx->is_stderr) {
         if (sp->stderr_reader == ctx) sp->stderr_reader = NULL;
         sp->fw_stderr = -1;
      } else {
         if (sp->stdout_reader == ctx) sp->stdout_reader = NULL;
         sp->fw_stdout = -1;
      }
   }
   ctx->sp = NULL;
   fwdsp_reader_release(ctx);
}
static bool fwdsp_fd_ready(int fd, GIOCondition events, void *arg) {
   struct fwdsp_io_conn *ctx = arg;
   ctx->processing = true;
   uint8_t buf[16384];
   ssize_t count = read(fd, buf, sizeof(buf));
   if (count > 0 && ctx->sp) {
      g_byte_array_append(ctx->recv, buf, (guint)count);
      fwdsp_parse_read(ctx);
   }
   bool done = count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK);
   if (events & (G_IO_ERR | G_IO_NVAL)) done = true;
   if ((events & G_IO_HUP) && count <= 0) done = true;
   if (!ctx->sp || ctx->detached) done = true;
   if (done && ctx->sp) {
      if (ctx->is_stderr && ctx->recv->len) {
         char *line = g_strndup((const char *)ctx->recv->data, ctx->recv->len);
         Log(LOG_DEBUG, "fwdsp", "[stderr %s]", line);
         g_free(line);
      }
      /* Do not cancel this watch in its own callback; returning false
       * removes the source and its allocated watch object. */
      ctx->watch = NULL;
      fwdsp_reader_detach(ctx);
   }
   ctx->processing = false;
   if (done || ctx->detached) {
      if (ctx->fd >= 0) close(ctx->fd);
      g_byte_array_unref(ctx->recv);
      free(ctx);
      return false;
   }
   return true;
}
static struct fwdsp_io_conn *fwdsp_reader_attach(struct fwdsp_subproc *sp, int fd, bool stderr_stream) {
   if (fd < 0) return NULL;
   struct fwdsp_io_conn *ctx = calloc(1, sizeof(*ctx));
   if (!ctx) return NULL;
   ctx->sp = sp;
   ctx->fd = fd;
   ctx->is_stderr = stderr_stream;
   ctx->recv = g_byte_array_new();
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
   ctx->watch = rr_reactor_watch_fd(NULL, fd, G_IO_IN, fwdsp_fd_ready, ctx);
   if (!ctx->watch) {
      g_byte_array_unref(ctx->recv);
      free(ctx);
      return NULL;
   }
   return ctx;
}

bool fwdsp_init(void) {
   if (fwdsp_mgr_ready) {
      return false;
   }

   const char *max_subprocs_s = cfg_get("fwdsp:subproc.max");

   if (max_subprocs_s) {
      max_subprocs = atoi(max_subprocs_s);
      Log(LOG_DEBUG, "fwdsp-mgr", "fwdspmgr initializing with %d slots available", max_subprocs);
   } else {
      Log(LOG_CRIT, "config", "fwdsp:subproc.max must be set in config for fwdsp manager to work!");

      return true;
   }

   // Sanity check as some hams are crazy? ;)
   if (max_subprocs <= 0 || max_subprocs > FWDSP_MAX_SUBPROCS) {
      Log(LOG_CRIT, "config", "fwdsp:subproc.max <%d> is invalid: range=0-%d", max_subprocs, FWDSP_MAX_SUBPROCS);

      return true;
   }

   char *record_dir = cfg_get_path("fwdsp:recording.path");
   bool record_dir_owned = record_dir != NULL;

   if (!record_dir || !*record_dir) {
      free((char *)record_dir);
      record_dir = expand_path("$HOME/recordings");

      if (!record_dir) {
         Log(LOG_CRIT, "fwdsp-mgr", "Unable to allocate recording directory path");

         return true;
      }
      record_dir_owned = true;
   }

   if (!mkdir_p(record_dir)) {
      Log(LOG_CRIT, "fwdsp-mgr", "Unable to create recording directory %s: %s", record_dir, strerror(errno));

      if (record_dir_owned) {
         free((char *)record_dir);
      }

      return true;
   }

   if (record_dir_owned) {
      free((char *)record_dir);
   }

   // if not allocated, try to allocate it
   if (!fwdsp_subprocs) {
      fwdsp_subprocs = calloc(max_subprocs + 1, sizeof(struct fwdsp_subproc));

      if (!fwdsp_subprocs) {
         Log(LOG_CRIT, "fwdsp-mgr", "fwdsp_init failed to allocate fwdsp_subprocs! errno %d", errno);

         return true;
      }
   }

   // did we OOM?
   if (!fwdsp_subprocs) {
      fprintf(stderr, "OOM in fwdsp_init\n");
      exit(1);
   }
   // Setup signal handling for SIGCHLD. We only record the reaping request
   // here; the actual waitpid()/cleanup runs in fwdsp_reap_children() from the
   // main event loop. Doing waitpid/Log/memset inside the handler is not
   // async-signal-safe and corrupted the heap when clients connected.
   struct sigaction sa = {
      .sa_handler = fwdsp_sigchld,
      .sa_flags = SA_RESTART | SA_NOCLDSTOP
   };
   sigemptyset(&sa.sa_mask);
   sigaction(SIGCHLD, &sa, NULL);
   /*
    * Child IPC uses write(2) on Unix socketpairs.  A child can close its input while a final encoded page is being delivered; ignore SIGPIPE so that the
    * manager reports the failed write and reaps the child instead of terminating the host process.
    */
   signal(SIGPIPE, SIG_IGN);
   fwdsp_set_exit_cb(fwdsp_subproc_exit_cb);

   // Find the fwdsp path
   fwdsp_path = cfg_get_path("fwdsp:path");

   if (!fwdsp_path) {
      Log(LOG_CRIT, "fwdsp", "You must set [fwdsp] path to point at fwdsp binary");

      return true;
   }
   fwdsp_mgr_ready = true;

   return false;
}

bool fwdsp_fini(void) {
   if (!fwdsp_mgr_ready && !fwdsp_subprocs) {
      return false;
   }

   /*
    * Tear down every remaining subprocess, including encoders retained by fwdsp:hangtime. fwdsp_destroy() also closes IPC and releases any cached stream
    * headers associated with the slot.
    */
   if (fwdsp_subprocs) {
      for (int i = 0 ; i < max_subprocs ; i++) {
         if (fwdsp_slot_active(&fwdsp_subprocs[i])) {
            fwdsp_destroy(&fwdsp_subprocs[i]);
         }
      }

      free(fwdsp_subprocs);
      fwdsp_subprocs = NULL;
   }

   free((char *)fwdsp_path);
   fwdsp_path = NULL;

   active_slots = 0;
   next_channel_id = 1;
   fwdsp_mgr_ready = false;
   fwdsp_sigchld_pending = 0;

   return false;
}

static int fwdsp_find_offset(const char *id, bool is_tx) {
   if (!id || max_subprocs <= 0 || !active_slots || !fwdsp_subprocs) {
      return -1;
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      if (fwdsp_subprocs[i].role != FWDSP_ROLE_CODEC) {
         continue;
      }

      if (strncmp(id, fwdsp_subprocs[i].pl_id, 4) == 0 && fwdsp_subprocs[i].is_tx == is_tx) {
         return i;
      }
   }

   return -1;
}

struct fwdsp_subproc *fwdsp_find_channel_instance(const char *id, bool is_tx, const char *channel_uuid) {
   if (!id || !channel_uuid || !*channel_uuid || !fwdsp_subprocs) {
      return NULL;
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (sp->role == FWDSP_ROLE_CODEC && sp->is_tx == is_tx &&
         strncmp(sp->pl_id, id, 4) == 0 && strncmp(sp->channel_uuid, channel_uuid, sizeof(sp->channel_uuid)) == 0) {
         return sp;
      }
   }

   return NULL;
}

struct fwdsp_subproc *fwdsp_find_instance(const char *id, bool is_tx) {
   int i = fwdsp_find_offset(id, is_tx);

   return (i >= 0) ? &fwdsp_subprocs[i] : NULL;
}

static struct fwdsp_subproc *fwdsp_create(const char *id, enum fwdsp_io_type io_type, bool is_tx, const char *channel_uuid, bool is_video, bool capture) {
   if (!id) {
      Log(LOG_CRIT, "fwdsp", "create: Invalid parameters: id == NULL");

      return NULL;
   }

   if (active_slots >= max_subprocs) {
      Log(LOG_CRIT, "fwdsp", "We're out of fwdsp slots. %d of %d used", active_slots, max_subprocs);

      return NULL;
   }

   if (!fwdsp_subprocs) {
      return NULL;
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (sp && sp->role == FWDSP_ROLE_NONE) {
         Log(LOG_DEBUG, "fwdsp", "Assigning fwdsp slot %d to new codec %s.%s", i, id, (is_tx ? "tx" : "rx"));
         // Clear the memory for reuse
         memset(sp, 0, sizeof(struct fwdsp_subproc));
         // Fill the struct
         sp->role = FWDSP_ROLE_CODEC;
         memcpy(sp->pl_id, id, 4);

         if (channel_uuid) {
            snprintf(sp->channel_uuid, sizeof(sp->channel_uuid), "%s", channel_uuid);
         }

         sp->is_tx = is_tx;
         sp->is_video = is_video;
         sp->video_capture = capture;

         if (is_video && channel_uuid && !strncmp(channel_uuid, "video.", 6)) {
            char key[128];
            snprintf(key, sizeof(key), "pipeline:%s", channel_uuid);
            const char *pipeline = cfg_get(key);

            if (!pipeline || strlen(pipeline) >= sizeof(sp->pipeline)) {
               memset(sp, 0, sizeof(*sp));

               return NULL;
            }
            snprintf(sp->pipeline, sizeof(sp->pipeline), "%s", pipeline);
         }
         sp->chan_id = next_channel_id++;
         sp->io_type = io_type;
         active_slots++;

         // Connect IO
         switch (io_type) {
            case FW_IO_STDIO: {
               break;
            }
            default:
            case FW_IO_NONE: {
               break;
            }
         }

         if (fwdsp_spawn(sp)) {
            return sp;
         }

         memset(sp, 0, sizeof(*sp));

         if (active_slots > 0) {
            active_slots--;
         }

         return NULL;
      }
   }

   Log(LOG_CRIT, "fwdsp", "Out of subproc slots?! %d > %d", active_slots, max_subprocs);

   return NULL;
}

struct fwdsp_subproc *fwdsp_find_or_create(const char *id, enum fwdsp_io_type io_type, bool is_tx) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(id, is_tx);

   if (!sp) {
      sp = fwdsp_create(id, io_type, is_tx, NULL, false, false);

      if (!sp) {
         Log(LOG_CRIT, "fwdsp", "Failure in fwdsp_create call");
      } else {
         Log(LOG_DEBUG, "fwdsp", "Spawned new instance of fwdsp at %p for codec %s.%s", (void *)sp, id, (is_tx ? "tx" : "rx"));
      }
   } else {
      Log(LOG_DEBUG, "fwdsp", "Using existing fwdsp instance %p for codec %s.%s", (void *)sp, id, (is_tx ? "tx" : "rx"));
   }

   return sp;
}

/* Mark video before create launches the child: camera capture must use -v -t. */
struct fwdsp_subproc *fwdsp_find_or_create_video(const char *id, bool is_tx) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(id, is_tx);

   return sp ? (sp->is_video ? sp : NULL) : fwdsp_create(id, FW_IO_STDIO, is_tx, NULL, true, true);
}

int fwdsp_video_pipeline_start(const char codec[5], const char *identity, bool capture, fwdsp_processor_output_cb output, void *user) {
   if (!codec || strlen(codec) != 4 || !identity || !*identity || !output || fwdsp_init()) {
      return -1;
   }
   char key[128];
   snprintf(key, sizeof(key), "pipeline:%s%s", !strncmp(identity, "video.", 6) ? identity : codec, !strncmp(identity, "video.", 6) ? "" : capture ? ".tx" :
      ".rx");
   const char *pipeline = cfg_get(key);

   if (!pipeline || !*pipeline || strlen(identity) >= sizeof(((struct fwdsp_subproc *) 0)->channel_uuid)) {
      return -1;
   }
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec, capture, identity);

   if (!sp) {
      sp = fwdsp_create(codec, FW_IO_STDIO, capture, identity, true, capture);
   }

   if (!sp || !sp->is_video) {
      return -1;
   }
   sp->processor_output = output;
   sp->processor_output_data = user;
   sp->refcount = 1;

   return sp->chan_id;
}

static bool fwdsp_endpoint_identity(const char *name, const char *default_namespace, char namespace_out[8], char name_out[64]) {
   if (!name || !*name || !namespace_out || !name_out) {
      return false;
   }
   const char *dot = strchr(name, '.');
   const char *base = name;
   const char *namespace = default_namespace;

   if (dot) {
      size_t namespace_len = (size_t)(dot - name);

      if (namespace_len == 0 || namespace_len >= 8 || !dot[1]) {
         return false;
      }
      memcpy(namespace_out, name, namespace_len);
      namespace_out[namespace_len] = '\0';
      namespace = namespace_out;
      base = dot + 1;
   } else {
      if (!default_namespace || strlen(default_namespace) >= 8) {
         return false;
      }
      snprintf(namespace_out, 8, "%s", default_namespace);
   }

   if (strcmp(namespace, "proc") != 0 && strcmp(namespace, "src") != 0 && strcmp(namespace, "sink") != 0 && strcmp(namespace, "recode") != 0) {
      return false;
   }

   if (strlen(base) >= 64) {
      return false;
   }
   snprintf(name_out, 64, "%s", base);

   return true;
}

static struct fwdsp_subproc *fwdsp_find_processor(const char *name, const char *default_namespace) {
   char namespace[8];
   char base[64];

   if (!fwdsp_endpoint_identity(name, default_namespace, namespace, base) || !fwdsp_subprocs) {
      return NULL;
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (sp->role == FWDSP_ROLE_PROCESSOR && strcmp(sp->processor_namespace, namespace) == 0 && strcmp(sp->processor_name, base) == 0) {
         return sp;
      }
   }

   return NULL;
}

static bool fwdsp_audio_endpoint_start(const char *name, const char *pipeline, enum fwdsp_processor_io processor_io, fwdsp_processor_output_cb output_cb, void *
   user_data) {
   if (!fwdsp_mgr_ready || !name || !*name || strlen(name) >= sizeof(((struct fwdsp_subproc *) 0)->processor_name) || (pipeline
      && strlen(pipeline) >= sizeof(((struct fwdsp_subproc *) 0)->pipeline)) || (processor_io != FWDSP_PROCESSOR_IO_PLAYBACK && !output_cb)
      || (processor_io == FWDSP_PROCESSOR_IO_PLAYBACK && output_cb)) {
      return false;
   }
   const char *namespace = processor_io == FWDSP_PROCESSOR_IO_CAPTURE ? "src" : processor_io == FWDSP_PROCESSOR_IO_PLAYBACK ? "sink" : "proc";
   char endpoint_namespace[8];
   char endpoint_name[64];

   if (!fwdsp_endpoint_identity(name, namespace, endpoint_namespace, endpoint_name) ||
      strcmp(endpoint_namespace, namespace) != 0 || fwdsp_find_processor(name, namespace) || active_slots >= max_subprocs || !fwdsp_subprocs) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)endpoint_name ; *p ; p++) {
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.')) {
         Log(LOG_WARN, "fwdsp", "Rejecting invalid processor name '%s'", name);

         return false;
      }
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (sp->role != FWDSP_ROLE_NONE) {
         continue;
      }

      memset(sp, 0, sizeof(*sp));
      sp->role = FWDSP_ROLE_PROCESSOR;
      sp->processor_io = processor_io;
      snprintf(sp->processor_namespace, sizeof(sp->processor_namespace), "%s", namespace);
      snprintf(sp->processor_name, sizeof(sp->processor_name), "%s", endpoint_name);

      if (pipeline) {
         snprintf(sp->pipeline, sizeof(sp->pipeline), "%s", pipeline);
      }
      sp->processor_output = output_cb;
      sp->processor_output_data = user_data;
      sp->io_type = FW_IO_STDIO;
      sp->fw_stdin = -1;
      sp->fw_stdout = -1;
      sp->fw_stderr = -1;
      sp->fw_control = -1;
      sp->chan_id = next_channel_id++;
      active_slots++;

      if (!fwdsp_spawn(sp)) {
         fwdsp_destroy(sp);

         return false;
      }
      Log(LOG_INFO, "fwdsp", "Started %s audio endpoint %s", processor_io == FWDSP_PROCESSOR_IO_CAPTURE ? "capture" :
         processor_io == FWDSP_PROCESSOR_IO_PLAYBACK ? "playback" : "PCM processor", endpoint_name);

      return true;
   }

   return false;
}

bool fwdsp_processor_start(const char *name, const char *pipeline, fwdsp_processor_output_cb output_cb, void *user_data) {
   return fwdsp_audio_endpoint_start(name, pipeline, FWDSP_PROCESSOR_IO_BIDIRECTIONAL, output_cb, user_data);
}

bool fwdsp_audio_capture_start(const char *name, const char *pipeline, fwdsp_processor_output_cb output_cb, void *user_data) {
   return fwdsp_audio_endpoint_start(name, pipeline, FWDSP_PROCESSOR_IO_CAPTURE, output_cb, user_data);
}

bool fwdsp_audio_playback_start(const char *name, const char *pipeline) {
   return fwdsp_audio_endpoint_start(name, pipeline, FWDSP_PROCESSOR_IO_PLAYBACK, NULL, NULL);
}

bool fwdsp_audio_resampler_start(const char *name, unsigned rate, fwdsp_processor_output_cb output_cb, void *user) {
   if (rate < 8000 || rate > 48000) {
      return false;
   }
   char pipeline[512];
   snprintf(pipeline, sizeof(pipeline), "appsrc name=processor-src is-live=true format=time do-timestamp=true "
      "caps=audio/x-raw,format=S16LE,rate=%u,channels=1,layout=interleaved ! "
      "audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! "
      "appsink name=processor-sink sync=false max-buffers=8 drop=true", rate);

   return fwdsp_audio_endpoint_start(name, pipeline, FWDSP_PROCESSOR_IO_RESAMPLE, output_cb, user);
}

bool fwdsp_processor_write(const char *name, const void *samples, size_t len) {
   struct fwdsp_subproc *sp = fwdsp_find_processor(name, "proc");

   if (!sp) {
      sp = fwdsp_find_processor(name, "sink");
   }

   if (!sp || sp->process.pid <= 0 || sp->fw_stdin < 0 || !samples || len == 0 ||
      sp->processor_io == FWDSP_PROCESSOR_IO_CAPTURE || (len & 1) != 0 || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }

   if (fwdsp_write_frame(sp->fw_stdin, samples, len)) {
      return true;
   }
   fwdsp_destroy(sp);

   return false;
}

bool fwdsp_processor_setvol(const char *name, int percent) {
   struct fwdsp_subproc *sp = fwdsp_find_processor(name, "proc");

   if (!sp) {
      sp = fwdsp_find_processor(name, "sink");
   }

   if (!sp || sp->process.pid <= 0 || sp->fw_control < 0) {
      return true;
   }

   if (percent < 0) {
      percent = 0;
   }

   if (percent > 100) {
      percent = 100;
   }

   return fwdsp_send_control(sp, FWDSP_CTRL_SET_VOLUME, (uint8_t) percent);
}

/* A real-time client mixer may borrow a duplicate of the playback socket.
 * The manager remains the sole owner of the subprocess and registry. */
int fwdsp_processor_dup_input(const char *name) {
   struct fwdsp_subproc *sp = fwdsp_find_processor(name, "sink");
   if (!sp || sp->role != FWDSP_ROLE_PROCESSOR || sp->fw_stdin < 0 || sp->destroying)
      return -1;
   return dup(sp->fw_stdin);
}

/* Stateless framed writer for a caller-owned duplicated descriptor.
 * Never touches the manager registry or GStreamer state. */
bool fwdsp_pcm_write_fd(int fd, const void *pcm, size_t bytes) {
   return fd >= 0 && fwdsp_write_frame(fd, pcm, bytes);
}

bool fwdsp_processor_stop(const char *name) {
   struct fwdsp_subproc *sp = fwdsp_find_processor(name, "proc");

   if (!sp) {
      sp = fwdsp_find_processor(name, "src");
   }

   if (!sp) {
      sp = fwdsp_find_processor(name, "sink");
   }

   if (!sp) {
      sp = fwdsp_find_processor(name, "recode");
   }

   return sp ? fwdsp_destroy(sp) : false;
}

static bool fwdsp_destroy(struct fwdsp_subproc *sp) {
   if (!fwdsp_slot_active(sp) || sp->destroying) {
      return true;
   }
   // Mark the slot before touching any child or Mongoose state. Teardown can
   // be reached from expiry, SIGCHLD handling, codec switching and shutdown;
   // a late callback must never free the same slot-owned buffers twice.
   sp->destroying = true;
   /* Tear down reactor watches before the process slot is recycled. */
   if (sp->stdout_reader) fwdsp_reader_detach(sp->stdout_reader);
   if (sp->stderr_reader) fwdsp_reader_detach(sp->stderr_reader);

   // Reap only our worker; other components own their own children.
   if (sp->process.pid > 0) {
      subproc_stop(&sp->process, SIGTERM);
   }

   // Close file descriptors
   if (sp->io_type == FW_IO_STDIO) {
      if (sp->fw_stdin > 0) {
         close(sp->fw_stdin);
      }

      if (sp->fw_stdout > 0) {
         close(sp->fw_stdout);
      }

      if (sp->fw_stderr > 0) {
         close(sp->fw_stderr);
      }

      if (sp->fw_control > 0) {
         close(sp->fw_control);
      }
   }

   uint8_t *stream_headers = sp->stream_headers;
   sp->stream_headers = NULL;
   sp->stream_headers_len = 0;
   free(stream_headers);
   // Clear struct
   memset(sp, 0, sizeof(*sp));

   if (active_slots > 0) {
      active_slots--;
   }

   return true;
}

bool fwdsp_spawn(struct fwdsp_subproc *sp) {
   if (!sp) {
      return false;
   }
   if (!fwdsp_spawn_permitted(sp)) return false;

   int in_pipe[2] = {
      -1, -1
   }, out_pipe[2] = {
      -1, -1
   };
   int err_pipe[2] = {
      -1, -1
   }, control_pipe[2] = {
      -1, -1
   };

   if (sp->io_type == FW_IO_STDIO) {
#ifndef _WIN32

      if (socketpair(AF_UNIX, SOCK_STREAM, 0, in_pipe) ||
         socketpair(AF_UNIX, SOCK_STREAM, 0, out_pipe) ||
         socketpair(AF_UNIX, SOCK_STREAM, 0, err_pipe) || socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control_pipe)) {
#else

      if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe) || pipe(control_pipe)) {
#endif
         perror("pipe");

         goto failed;
      }
   }

   char *fwdsp_path = cfg_get_path("fwdsp:path");
   const char *fwdsp_config = config_file;
   char pipeline_key[128];

   if (sp->role == FWDSP_ROLE_PROCESSOR) {
      snprintf(pipeline_key, sizeof(pipeline_key), "pipeline:%s.%s", sp->processor_namespace, sp->processor_name);
   } else {
      snprintf(pipeline_key, sizeof(pipeline_key), "pipeline:%s.%s", sp->pl_id, sp->is_video ? (sp->video_capture ? "tx" : "rx") : (sp->is_tx ? "tx" : "rx"));
   }
   // The parent passes the selected pipeline so the child does not silently
   // substitute its standalone defaults or select a different processing chain.
   const char *child_pipeline = sp->pipeline[0] ? sp->pipeline : cfg_get(pipeline_key);
   bool emit_pcm_tap = sp->role == FWDSP_ROLE_CODEC && !sp->is_tx && (sp->local_lifetime || cfg_get_bool("fwdsp:pcm-hub", false));

   if (!fwdsp_path || fwdsp_path[0] == '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set [fwdsp] path to point at fwdsp bin");

      free(fwdsp_path);
      goto failed;
   }

   const char *args[20];
   size_t n = 0;
   args[n++] = fwdsp_path;
   args[n++] = "-f";
   args[n++] = fwdsp_config;

   if (sp->role == FWDSP_ROLE_PROCESSOR) {
      args[n++] = "-T";
      args[n++] = "-M";
      args[n++] =
         sp->processor_io == FWDSP_PROCESSOR_IO_CAPTURE ? "capture" : sp->processor_io == FWDSP_PROCESSOR_IO_PLAYBACK ? "playback" : sp->processor_io ==
         FWDSP_PROCESSOR_IO_RESAMPLE ? "resample" : "process";
      args[n++] = "-P";
      args[n++] = pipeline_key + strlen("pipeline:");
   } else {
      args[n++] = "-c";
      args[n++] = sp->pl_id;
   }
   args[n++] = "-C";
   args[n++] = "3";
   args[n++] = "-p";
   args[n++] = child_pipeline ? child_pipeline : "";

   if (sp->role != FWDSP_ROLE_PROCESSOR) {
      if (sp->is_video) {
         args[n++] = "-v";

         if (sp->video_capture) {
            args[n++] = "-t";
         }
      } else if (sp->is_tx) {
         args[n++] = "-t";
      } else if (emit_pcm_tap) {
         args[n++] = "-H";
      }
   }
   args[n] = NULL;
   subproc_fd_t mappings[] = { {
                                  in_pipe[0], 0
                               }, {
                                  out_pipe[1], 1
                               }, {
                                  err_pipe[1], 2
                               }, {
                                  control_pipe[0], 3
                               } };
   int close_fds[] = {
      in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1], control_pipe[0], control_pipe[1]
   };

   for (size_t i = 0 ; i < 8 ; i++) {
      if (close_fds[i] >= 0 && fcntl(close_fds[i], F_SETFD, FD_CLOEXEC) < 0) {
         free(fwdsp_path);
         goto failed;
      }
   }

   bool started = subproc_spawn_fds(&sp->process, fwdsp_path, args, mappings, sp->io_type == FW_IO_STDIO ? 4 : 0, close_fds, 8);

   if (!started) {
      free(fwdsp_path);
      goto failed;
   }

   // cfg_get_exp() returns a malloc'd string we own
   free((char *)fwdsp_path);
   fwdsp_path = NULL;

   if (sp->io_type == FW_IO_STDIO) {
      close(in_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[1]);
      close(control_pipe[0]);
      sp->fw_stdin = in_pipe[1];
      sp->fw_stdout = out_pipe[0];
      sp->fw_stderr = err_pipe[0];
      sp->fw_control = control_pipe[1];
#ifndef _WIN32
      int stdin_flags = fcntl(sp->fw_stdin, F_GETFL, 0);

      if (stdin_flags >= 0) {
         (void)fcntl(sp->fw_stdin, F_SETFL, stdin_flags | O_NONBLOCK);
      }
#endif

      sp->stdout_reader = fwdsp_reader_attach(sp, sp->fw_stdout, false);
      sp->stderr_reader = fwdsp_reader_attach(sp, sp->fw_stderr, true);
      if (!sp->stdout_reader || !sp->stderr_reader) {
         Log(LOG_CRIT, "fwdsp", "Failed to attach subprocess pipes to GLib reactor");
         fwdsp_destroy(sp);
         return false;
      }

   }

   if (sp->role == FWDSP_ROLE_PROCESSOR) {
      Log(LOG_DEBUG, "fwdsp", "Spawned audio processor %s at pid %d", sp->processor_name, sp->process.pid);
   } else {
      Log(LOG_DEBUG, "fwdsp", "Spawned codec %s.%s at pid %d", sp->pl_id, sp->is_tx ? "tx" : "rx", sp->process.pid);
   }

   // The application starts recording with its user and radio direction.

   return true;
failed:

   for (int i = 0 ; i < 2 ; i++) {
      if (in_pipe[i] >= 0) {
         close(in_pipe[i]);
      }

      if (out_pipe[i] >= 0) {
         close(out_pipe[i]);
      }

      if (err_pipe[i] >= 0) {
         close(err_pipe[i]);
      }

      if (control_pipe[i] >= 0) {
         close(control_pipe[i]);
      }
   }

   return false;
}

struct fwdsp_subproc *fwdsp_start_stdio_from_list(const char *codec_list, bool tx_mode) {
   if (!codec_list || !*codec_list) {
      return NULL;
   }

   char *tmp = strdup(codec_list);

   if (!tmp) {
      return NULL;
   }

   char *saveptr = NULL;
   char *token = strtok_r(tmp, " ", &saveptr);
   while (token) {
      // A codec entry is a 4-char magic such as "mu08"; accept it as-is
      if (strlen(token) == 4) {
         struct fwdsp_subproc *sp = fwdsp_find_or_create(token, FW_IO_STDIO, tx_mode);

         if (!sp || sp->process.pid <= 0) {
            if (!sp || !fwdsp_spawn(sp)) {
               Log(LOG_CRIT, "fwdsp", "Failed to spawn fwdsp for codec %s.%s", token, (tx_mode ? "tx" : "rx"));

               if (sp) {
                  fwdsp_destroy(sp);
               }
               sp = NULL;
            }
         }
         free(tmp);

         return sp;
      }
      token = strtok_r(NULL, " ", &saveptr);
   }
   free(tmp);
   Log(LOG_CRIT, "fwdsp", "No usable codecs found in list: %s for %s", codec_list, (tx_mode ? "tx" : "rx"));

   return NULL;
}

int fwdsp_get_chan_id(const char *magic, bool is_tx) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(magic, is_tx);

   return (sp) ? sp->chan_id : -1;
}

bool fwdsp_write_audio_samples(const char codec[5], const char *channel_uuid, const void *data, size_t len, bool discontinuity) {
   struct fwdsp_subproc *sp = channel_uuid && *channel_uuid ? fwdsp_find_channel_instance(codec, false, channel_uuid) : fwdsp_find_instance(codec, false);

   if (!sp || sp->fw_stdin < 0 || !data || !len || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }

   if (fwdsp_write_frame_flags(sp->fw_stdin, data, len, discontinuity ? FWDSP_FRAME_DISCONT : 0)) {
      return true;
   }
   /* A partial framed write cannot be skipped; retire the damaged IPC stream.
    * Log the OS failure *before* the reusable slot is cleared. This is the
    * primary clue for distinguishing a crashed pipeline from backpressure. */
   int saved_errno = errno;
   Log(LOG_WARN, "fwdsp", "Decoder %s.rx identity=%s pid=%ld: failed writing %zu encoded bytes (%s); retiring IPC stream",
      codec, channel_uuid ? channel_uuid : "(default)", (long)sp->process.pid,
      len, saved_errno ? strerror(saved_errno) : "unknown write failure");
   fwdsp_destroy(sp);

   return false;
}

bool fwdsp_write_samples(const char codec_id[5], bool is_tx, const void *data, size_t len) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, is_tx);

   if (!sp) {
      Log(LOG_WARN, "fwdsp", "write_samples: no fwdsp instance for %s.%s", codec_id, is_tx ? "tx" : "rx");

      return true;
   }

   if (sp->fw_stdin <= 0) {
      Log(LOG_WARN, "fwdsp", "write_samples: %s.%s has invalid stdin fd %d pid %d", codec_id, is_tx ? "tx" : "rx", sp->fw_stdin, sp->process.pid);

      return true;
   }

   if (!data || len == 0) {
      Log(LOG_WARN, "fwdsp", "write_samples: %s.%s got empty frame", codec_id, is_tx ? "tx" : "rx");

      return true;
   }

   if (len > UINT32_MAX) {
      Log(LOG_WARN, "fwdsp", "write_samples: %s.%s frame too large: %zu bytes", codec_id, is_tx ? "tx" : "rx", len);

      return true;
   }

   if (fwdsp_write_frame(sp->fw_stdin, data, len)) {
      return false;
   }
   /* A full child input socket must never stall the host. Retire the unhealthy process; the normal codec/endpoint owner will recreate it. */
   fwdsp_destroy(sp);

   return true;
}

bool fwdsp_write_channel_samples(const char codec_id[5], bool is_tx, const char *channel_uuid, const void *data, size_t len) {
   if (!codec_id || strlen(codec_id) != 4 || !channel_uuid || !*channel_uuid || !data || len == 0 || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec_id, is_tx, channel_uuid);

   if (!sp || sp->process.pid <= 0 || sp->fw_stdin < 0) {
      return false;
   }

   if (fwdsp_write_frame(sp->fw_stdin, data, len)) {
      return true;
   }
   fwdsp_destroy(sp);

   return false;
}

bool fwdsp_codec_set_pcm_callback(const char codec_id[5], const char *channel_uuid, fwdsp_processor_output_cb output_cb, void *user_data) {
   if (!codec_id || strlen(codec_id) != 4 || !output_cb) {
      return false;
   }
   struct fwdsp_subproc *sp = channel_uuid && *channel_uuid ? fwdsp_find_channel_instance(codec_id, false, channel_uuid) : fwdsp_find_instance(codec_id, false);

   if (!sp || sp->role != FWDSP_ROLE_CODEC || sp->process.pid <= 0) {
      return false;
   }
   sp->processor_output = output_cb;
   sp->processor_output_data = user_data;

   return true;
}

static bool fwdsp_send_control(struct fwdsp_subproc *sp, uint8_t type, uint8_t value) {
   if (!sp || sp->fw_control <= 0) {
      return true;
   }

   struct fwdsp_control_msg msg = {
      .magic = FWDSP_CTRL_MAGIC,
      .type = type,
      .value = value
   };

   return send(sp->fw_control, &msg, sizeof(msg), MSG_NOSIGNAL) == (ssize_t) sizeof(msg) ? false : true;
}

static int fwdsp_encoder_hangtime(void) {
   const char *hangtime_s = cfg_get_exp("fwdsp:hangtime");
   int hangtime = hangtime_s ? atoi(hangtime_s) : 60;

   free((char *)hangtime_s);

   return hangtime < 0 ? 0 : hangtime;
}

static void fwdsp_idle_pipeline(struct fwdsp_subproc *sp) {
   if (!sp || sp->process.pid <= 0) {
      return;
   }

   sp->refcount = 0;

   // is_tx means fwdsp consumes raw sound-card PCM and emits encoded media:
   // it is an encoder. Keep encoders warm for fwdsp:hangtime so PTT/codec
   // reuse does not pay process/GStreamer startup latency. Decoders are cheap
   // to recreate and can otherwise accumulate one process per old codec.
   if (sp->is_tx) {
      int hangtime = fwdsp_encoder_hangtime();

      if (hangtime > 0) {
         // A warm encoder must not keep producing frames for the channel after
         // a codec switch. Pause the GStreamer pipeline but keep the process.
         fwdsp_send_control(sp, FWDSP_CTRL_PAUSE, 0);
         sp->idle_since = time(NULL);
         sp->cleanup_deadline = sp->idle_since + hangtime;

         return;
      }
   }

   fwdsp_destroy(sp);
}

void fwdsp_sweep_expired(void) {
   time_t sweep_now = time(NULL);

   if (!active_slots) {
      return;
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (!fwdsp_slot_active(sp) || sp->role != FWDSP_ROLE_CODEC || sp->process.pid <= 0) {
         continue;
      }

      // Client-owned instances have explicit lifetimes, including independently
      // named RX decoders. Server subscriber sweeps must not retire them.
      if (sp->is_video || sp->local_lifetime || sp->channel_uuid[0] == '\0') {
         if (sp->refcount == 0 && sp->cleanup_deadline > 0 && sweep_now >= sp->cleanup_deadline) {
            fwdsp_destroy(sp);
         }
         continue;
      }

      struct rr_mediachan *channel = media_chan_find_uuid(sp->channel_uuid);
      u_int32_t chan_id = channel ? (u_int32_t) (channel - media_channels) + 1 : 0;
      int users = 0;

      // A lingering encoder still points at the same channel UUID, but it is
      // no longer active after that channel switches codec. Do not let its
      // subscribers resurrect its refcount during the sweep.
      bool active_codec = channel && channel->codec[0] != '\0' && strncmp(channel->codec, sp->pl_id, 4) == 0;
      rrconn_t *cur = http_client_list;

      while (active_codec && cur) {
         if (cur->is_ws && cur->authenticated &&
            ((channel->direction == RR_BINFRAME_DIR_RX &&
            chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id)) ||
            (channel->direction == RR_BINFRAME_DIR_TX && chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id)))) {
            users++;
         }
         cur = cur->next;
      }

      // Linked subscribers are represented by the server's media-demand
      // callback, rather than entries in the local HTTP client list.
      if (active_codec && users == 0 && ws_media_channel_has_subscribers(channel)) {
         users = 1;
      }

      if (users > 0) {
         if (sp->is_tx && sp->refcount == 0 && sp->cleanup_deadline > 0) {
            sp->replay_headers = true;
            fwdsp_send_control(sp, FWDSP_CTRL_RESUME, 0);
         }
         sp->refcount = users;
         sp->cleanup_deadline = 0;
         sp->idle_since = 0;
      } else if (sp->refcount > 0) {
         fwdsp_idle_pipeline(sp);
         continue;
      }

      if (sp->process.pid > 0 && sp->refcount == 0 && sp->cleanup_deadline > 0 && sweep_now >= sp->cleanup_deadline) {
         time_t idle_seconds = sp->idle_since > 0 && sweep_now >= sp->idle_since ? sweep_now - sp->idle_since : 0;
         Log(LOG_INFO, "fwdsp", "Cleaning up idle encoder %s.%s after %ld seconds idle", sp->pl_id, (sp->is_tx ? "tx" : "rx"), (long)idle_seconds);
         fwdsp_destroy(sp);
      }
   }
}

// Allocate an independently named codec whose caller owns its lifetime.
// No UI policy lives here: the caller explicitly stops it when no longer needed.
int fwdsp_codec_start_local(const char codec_id[5], bool is_tx, const char *identity) {
   if (!codec_id || strlen(codec_id) != 4 || !identity || !*identity) {
      return -1;
   }
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec_id, is_tx, identity);

   if (!sp) {
      sp = fwdsp_create(codec_id, FW_IO_STDIO, is_tx, identity, false, false);
   }

   if (!sp) {
      return -1;
   }
   // Set ownership before spawning so local RX always emits caller-routed PCM.
   sp->local_lifetime = true;

   return fwdsp_codec_start(codec_id, is_tx, identity);
}

// Start (or ref up) a pipeline; return its channel id, or -1 on failure.
int fwdsp_codec_start(const char codec_id[5], bool is_tx, const char *channel_uuid) {
   if (!codec_id || codec_id[0] == '\0') {
      return -1;
   }

   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec_id, is_tx, channel_uuid);

   if (!sp && (!channel_uuid || !*channel_uuid)) {
      sp = fwdsp_find_or_create(codec_id, FW_IO_STDIO, is_tx);
   }

   if (!sp && channel_uuid) {
      sp = fwdsp_create(codec_id, FW_IO_STDIO, is_tx, channel_uuid, false, false);
   }

   if (!sp) {
      Log(LOG_CRIT, "fwdsp", "Failed to start fwdsp for %s.%s", codec_id, (is_tx ? "tx" : "rx"));

      return -1;
   }

   if (sp->process.pid <= 0) {
      if (!fwdsp_spawn(sp)) {
         Log(LOG_CRIT, "fwdsp", "Failed to spawn fwdsp for %s.%s", codec_id, (is_tx ? "tx" : "rx"));

         return -1;
      }
   } else if (sp->is_tx && sp->refcount == 0 && sp->cleanup_deadline > 0) {
      // Reuse a warm encoder retained by fwdsp:hangtime.
      sp->replay_headers = true;
      fwdsp_send_control(sp, FWDSP_CTRL_RESUME, 0);
   }
   sp->cleanup_deadline = 0;
   sp->idle_since = 0;
   sp->refcount++;

   return sp->chan_id;
}


// Drop a reference on a codec pipeline. When the last user goes away, either
// destroy the subproc outright or set the hangtime cleanup deadline.
int fwdsp_codec_stop_channel(const char *codec, bool is_tx, const char *channel_uuid) {
   if (!codec || codec[0] == '\0') {
      return -1;
   }

   struct fwdsp_subproc *sp = NULL;

   if (channel_uuid && *channel_uuid) {
      sp = fwdsp_find_channel_instance(codec, is_tx, channel_uuid);

      /* Older pipelines were created before channel UUIDs were attached. Retire an otherwise unbound legacy instance during renegotiation so it cannot keep
       * emitting packets with no media destination. */
      if (!sp && fwdsp_subprocs) {
         for (int i = 0 ; i < max_subprocs ; i++) {
            struct fwdsp_subproc *candidate = &fwdsp_subprocs[i];

            if (candidate->pl_id[0] && candidate->is_tx == is_tx && !candidate->channel_uuid[0] && !strncmp(candidate->pl_id, codec, 4)) {
               sp = candidate;
               break;
            }
         }
      }
   } else {
      sp = fwdsp_find_instance(codec, is_tx);
   }

   if (!sp) {
      Log(LOG_DEBUG, "fwdsp", "fwdsp_codec_stop: no instance for %s.%s channel %s", codec, (is_tx ? "tx" : "rx"), (channel_uuid ? channel_uuid : "-"));

      return -1;
   }

   if (sp->refcount > 0) {
      sp->refcount--;
   }

   if (sp->refcount == 0) {
      fwdsp_idle_pipeline(sp);
   }

   return 0;
}

int fwdsp_codec_stop(const char *codec, bool is_tx) {
   return fwdsp_codec_stop_channel(codec, is_tx, NULL);
}

int fwdsp_codec_switch(const char *old_codec, const char *new_codec, bool is_tx, const char *channel_uuid) {
   if (!new_codec || strlen(new_codec) != 4) {
      return -1;
   }

   if (old_codec && strlen(old_codec) == 4 && strncmp(old_codec, new_codec, 4) == 0) {
      struct fwdsp_subproc *sp = channel_uuid && *channel_uuid ?
         fwdsp_find_channel_instance(new_codec, is_tx, channel_uuid) : fwdsp_find_instance(new_codec, is_tx);

      if (sp && sp->process.pid > 0) {
         if (sp->refcount > 0) {
            return sp->chan_id;
         }

         return fwdsp_codec_start(new_codec, is_tx, channel_uuid);
      }
   }

   int chan_id = fwdsp_codec_start(new_codec, is_tx, channel_uuid);

   if (chan_id < 0) {
      return -1;
   }

   // A channel can switch back before the subscriber sweep has paused its
   // old encoder. Its new decoder still needs the cached initialization data.
   struct fwdsp_subproc *replacement = channel_uuid && *channel_uuid ?
      fwdsp_find_channel_instance(new_codec, is_tx, channel_uuid) : fwdsp_find_instance(new_codec, is_tx);

   if (replacement && replacement->stream_headers_len) {
      replacement->replay_headers = true;
   }

   // Start the replacement first so switching never creates an avoidable
   // media gap. Then release the old process. Encoders linger; decoders die.
   if (old_codec && strlen(old_codec) == 4 && strncmp(old_codec, new_codec, 4) != 0) {
      fwdsp_codec_stop_channel(old_codec, is_tx, channel_uuid);
   }

   return chan_id;
}

int fwdsp_codec_stop_channel_immediate(const char *codec, bool is_tx, const char *channel_uuid) {
   if (!codec || codec[0] == '\0') {
      return -1;
   }

   struct fwdsp_subproc *sp = NULL;

   if (channel_uuid && *channel_uuid) {
      sp = fwdsp_find_channel_instance(codec, is_tx, channel_uuid);
   } else {
      sp = fwdsp_find_instance(codec, is_tx);
   }

   if (!sp) {
      Log(LOG_DEBUG, "fwdsp", "fwdsp_codec_stop_immediate: no instance for %s.%s channel %s", codec, is_tx ? "tx" : "rx", channel_uuid ? channel_uuid : "-");

      return -1;
   }

   fwdsp_destroy(sp);

   return 0;
}

int fwdsp_codec_stop_immediate(const char *codec, bool is_tx) {
   return fwdsp_codec_stop_channel_immediate(codec, is_tx, NULL);
}
