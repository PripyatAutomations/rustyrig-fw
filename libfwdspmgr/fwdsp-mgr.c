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
#include <sys/wait.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <librustyaxe/core.h>
#include <librustyaxe/util.file.h>
#include <librrprotocol/rrprotocol.h>
#include <libfwdspmgr/fwdsp-mgr.h>
#include <fwdsp/default-pipelines.h>
#include <libfwdspmgr/fwdsp-ctl.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#define	FWDSP_MAX_SUBPROCS 100

defconfig_t defcfg_fwdsp[] = {
   { "codecs.allowed", FWDSP_DEFAULT_CODECS, "Preferred codecs" },
#ifdef _WIN32
   { "fwdsp.path", "bin/fwdsp.exe", "Path to fwdsp binary" },
#else
   { "fwdsp.path", "bin/fwdsp", "Path to fwdsp binary" },
#endif
   { "fwdsp.recording.path", "./recordings", "Path to audio recordings" },
   { "recording.codec", "flac", "Recording container/codec: flac or ogg" },
   { "recording.codec.modem", "flac", "Recording codec for modem recordings: flac or ogg" },
   { "fwdsp.recording.rx", "false", "Record received audio" },
   { "fwdsp.recording.tx", "false", "Record transmitted audio" },
   { "fwdsp.subproc.max", "16", "Maximum allowed de/encoder processes" },
   { "fwdsp.hangtime", "60", "How long to keep unused encoders alive after last use; decoders stop immediately" },
   { "fwdsp.subproc.debug", "false", "Show extra debug messages" },
   { NULL, NULL, NULL }
};

const char *fwdsp_path = NULL;
bool fwdsp_mgr_ready = false;
static int active_slots = 0;
static int max_subprocs = FWDSP_MAX_SUBPROCS;
static struct fwdsp_subproc *fwdsp_subprocs;
static int next_channel_id = 1;
extern const char *config_file;          // librustyaxe/config.c
#ifdef USE_MONGOOSE
extern struct mg_mgr mg_mgr;             // rrserver/main.c defines it; rrclient links through librrprotocol's `mgr` alias
#pragma weak mg_mgr
extern struct mg_mgr mgr;
#pragma weak mgr
#endif
static fwdsp_exit_cb_t on_fwdsp_exit = NULL;
extern time_t now;

static struct mg_mgr *fwdsp_mg_manager(void) {
   return (&mg_mgr != NULL) ? &mg_mgr : &mgr;
}

static bool fwdsp_send_control(struct fwdsp_subproc *sp, uint8_t type, uint8_t value);
static bool fwdsp_destroy(struct fwdsp_subproc *sp);

static void fwdsp_subproc_exit_cb(struct fwdsp_subproc *sp, int status) {
   Log(LOG_INFO, "fwdsp", "Pipeline %s.%s at pid %d exited (status=%d)", sp->pl_id, (sp->is_tx ? "tx" : "rx"), sp->pid, status);
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

static void frame_length_encode(uint8_t header[FWDSP_FRAME_HEADER_SIZE], uint32_t len) {
   header[0] = (uint8_t)(len >> 24);
   header[1] = (uint8_t)(len >> 16);
   header[2] = (uint8_t)(len >> 8);
   header[3] = (uint8_t)len;
}

static uint32_t frame_length_decode(const uint8_t header[FWDSP_FRAME_HEADER_SIZE]) {
   return ((uint32_t)header[0] << 24) |
          ((uint32_t)header[1] << 16) |
          ((uint32_t)header[2] << 8) |
          (uint32_t)header[3];
}

static bool fwdsp_write_all(int fd, const uint8_t *data, size_t len) {
   while (len > 0) {
      ssize_t written = write(fd, data, len);

      if (written > 0) {
         data += written;
         len -= (size_t)written;
      } else if (written < 0 && errno == EINTR) {
         continue;
      } else {
         return false;
      }
   }

   return true;
}

static bool fwdsp_write_frame(int fd, const uint8_t *data, size_t len) {
   uint8_t header[FWDSP_FRAME_HEADER_SIZE];

   if (!data || len == 0 || len > UINT32_MAX || len > FWDSP_MAX_FRAME_SIZE) {
      return false;
   }

   frame_length_encode(header, (uint32_t)len);

   if (!fwdsp_write_all(fd, header, sizeof(header))) {
      return false;
   }

   return fwdsp_write_all(fd, data, len);
}

// Called from the main event loop; reaps dead fwdsp children safely.
void fwdsp_reap_children(void) {
   if (!fwdsp_sigchld_pending) {
      return;
   }
   fwdsp_sigchld_pending = 0;

   int status;
   pid_t pid;
   while ( (pid = waitpid(-1, &status, WNOHANG) ) > 0) {
      for (int i = 0 ; i < max_subprocs ; i++) {
         struct fwdsp_subproc *sp = &fwdsp_subprocs[i];
         if (sp->pid == pid) {
            if (on_fwdsp_exit) {
               on_fwdsp_exit(sp, status);
            }
            sp->pid = 0;   // already reaped: only release I/O and the slot
            fwdsp_destroy(sp);
            break;
         }
      }
   }
}

// Header packets keep their existing encoded payload on the network. Only
// child IPC carries the marker used to cache them for late subscribers.
static void fwdsp_replay_stream_headers(struct fwdsp_subproc *sp,
   struct rr_mediachan *channel, rrconn_t *cptr) {
   for (size_t pos = 0; pos + 4 <= sp->stream_headers_len;) {
      uint32_t len = frame_length_decode(sp->stream_headers + pos);
      pos += 4;
      if (len > sp->stream_headers_len - pos) break;
      ws_media_send_frame(channel, cptr, sp->stream_headers + pos, len, sp->pl_id);
      pos += len;
   }
}

void fwdsp_send_stream_headers(const char *uuid, rrconn_t *cptr) {
   struct rr_mediachan *channel = media_chan_find_uuid(uuid);
   if (!channel || channel->direction != RR_BINFRAME_DIR_RX) return;
   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(channel->codec, true, uuid);
   // A resumed encoder will replay to all subscribers before its next packet.
   if (sp && !sp->replay_headers) fwdsp_replay_stream_headers(sp, channel, cptr);
}

static void fwdsp_read_cb(struct mg_connection *c, int ev, void *ev_data) {
   struct fwdsp_io_conn *ctx = c->fn_data;

   (void)ev_data;

   if (!ctx) {
      return;
   }

   if (ev == MG_EV_READ && ctx->sp) {
      if (ctx->is_stderr) {
         for (;;) {
            uint8_t *newline = memchr(c->recv.buf, '\n', c->recv.len);

            if (!newline) {
               break;
            }

            size_t line_len = (size_t)(newline - (uint8_t *)c->recv.buf);
            size_t consumed = line_len + 1;

            if (line_len > 0 && c->recv.buf[line_len - 1] == '\r') {
               line_len--;
            }

            char *message = malloc(line_len + 1);
            if (!message) {
               Log(LOG_CRIT, "fwdsp", "OOM buffering fwdsp stderr");
               mg_iobuf_del(&c->recv, 0, consumed);
               continue;
            }

            memcpy(message, c->recv.buf, line_len);
            message[line_len] = '\0';
            Log(LOG_DEBUG, "fwdsp", "[stderr %s]", message);
            free(message);

            mg_iobuf_del(&c->recv, 0, consumed);
         }

         if (c->recv.len > FWDSP_MAX_FRAME_SIZE) {
            Log(LOG_WARN, "fwdsp", "Discarding oversized unterminated stderr data");
            mg_iobuf_del(&c->recv, 0, c->recv.len);
         }
      } else {
         while (c->recv.len >= FWDSP_FRAME_HEADER_SIZE) {
            uint32_t frame_len = frame_length_decode((const uint8_t *)c->recv.buf);
            bool is_header = (frame_len & FWDSP_FRAME_STREAM_HEADER) != 0;
            frame_len &= ~FWDSP_FRAME_STREAM_HEADER;

            if (frame_len == 0 || frame_len > FWDSP_MAX_FRAME_SIZE) {
               Log(LOG_CRIT, "fwdsp", "Invalid frame length %u from %s.%s",
                  frame_len, ctx->sp->pl_id, ctx->sp->is_tx ? "tx" : "rx");
               mg_iobuf_del(&c->recv, 0, c->recv.len);
               break;
            }

            size_t total_len = FWDSP_FRAME_HEADER_SIZE + (size_t)frame_len;
            if (c->recv.len < total_len) {
               break;
            }

            if (is_header) {
               struct fwdsp_subproc *sp = ctx->sp;
               // Bound retained codec setup data, independent of media payload size.
               if (total_len <= 256 * 1024 - sp->stream_headers_len) {
                  uint8_t *headers = realloc(sp->stream_headers, sp->stream_headers_len + total_len);
                  if (headers) {
                     sp->stream_headers = headers;
                     frame_length_encode(headers + sp->stream_headers_len, frame_len);
                     memcpy(headers + sp->stream_headers_len + 4, c->recv.buf + 4, frame_len);
                     sp->stream_headers_len += total_len;
                  }
               } else {
                  Log(LOG_WARN, "fwdsp", "Codec initialization data exceeds cache limit");
               }
            }

            struct rr_mediachan *channel = ctx->sp->channel_uuid[0] != '\0' ?
               media_chan_find_uuid(ctx->sp->channel_uuid) :
               media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
                  ctx->sp->is_tx ? RR_BINFRAME_DIR_RX : RR_BINFRAME_DIR_TX,
                  0, 0);

            if (channel && ctx->sp->refcount > 0 &&
                strncmp(channel->codec, ctx->sp->pl_id, 4) == 0) {
               if (ctx->sp->replay_headers) {
                  fwdsp_replay_stream_headers(ctx->sp, channel, NULL);
                  ctx->sp->replay_headers = false;
               }
               ws_media_broadcast_subscribed(channel,
                  (const uint8_t *)c->recv.buf + FWDSP_FRAME_HEADER_SIZE,
                  frame_len, ctx->sp->pl_id);
            } else if (!channel) {
               /* A pipeline can produce packets briefly while its media
                * channel is being replaced or unsubscribed.  Do not emit a
                * warning for every packet in that transient state. */
               if (ctx->sp->last_no_channel_warn == 0 ||
                   now < ctx->sp->last_no_channel_warn ||
                   now - ctx->sp->last_no_channel_warn >= 5) {
                  Log(LOG_WARN, "fwdsp", "No media channel for %s.%s output",
                     ctx->sp->pl_id, ctx->sp->is_tx ? "tx" : "rx");
                  ctx->sp->last_no_channel_warn = now;
               }
            }

            mg_iobuf_del(&c->recv, 0, total_len);
         }
      }
   } else if (ev == MG_EV_CLOSE) {
      // Mongoose owns these descriptors and has closed them before this event.
      // Clear live slot pointers too, so teardown cannot close a reused fd.
      if (ctx->sp) {
         if (ctx->is_stderr) {
            ctx->sp->mg_stderr_conn = NULL;
            ctx->sp->fw_stderr = -1;
         } else {
            ctx->sp->mg_stdout_conn = NULL;
            ctx->sp->fw_stdout = -1;
         }
      }
      if (ctx->is_stderr && c->recv.len > 0) {
         char *message = malloc(c->recv.len + 1);

         if (message) {
            memcpy(message, c->recv.buf, c->recv.len);
            message[c->recv.len] = '\0';
            Log(LOG_DEBUG, "fwdsp", "[stderr %s]", message);
            free(message);
         }
      }

      // Free only the wrapper we allocated in fwdsp_spawn(). ctx->sp points
      // into the shared fwdsp_subprocs array which is managed by the slot
      // allocator and must never be freed here.
      free(ctx);
   }
}

bool fwdsp_init(void) {
   if (fwdsp_mgr_ready) {
      return false;
   }

   const char *max_subprocs_s = cfg_get_exp("fwdsp.subproc.max");
   if (max_subprocs_s) {
      max_subprocs = atoi(max_subprocs_s);
      Log(LOG_DEBUG, "fwdsp-mgr", "fwdspmgr initializing with %d slots available", max_subprocs);
      free((char *)max_subprocs_s);
   } else {
      Log(LOG_CRIT, "config", "fwdsp.subproc.max must be set in config for fwdsp manager to work!");
      return true;
   }

   // Sanity check as some hams are crazy? ;)
   if (max_subprocs <= 0 || max_subprocs > FWDSP_MAX_SUBPROCS) {
      Log(LOG_CRIT, "config", "fwdsp.subproc.max <%d> is invalid: range=0-%d", max_subprocs, FWDSP_MAX_SUBPROCS);
      return true;
   }

   const char *record_dir = cfg_get_exp("fwdsp.recording.path");
   bool record_dir_owned = record_dir != NULL;
   if (!record_dir || !*record_dir) {
      free((char *)record_dir);
      record_dir = "./recordings";
      record_dir_owned = false;
   }
   if (!mkdir_p(record_dir)) {
      Log(LOG_CRIT, "fwdsp-mgr", "Unable to create recording directory %s: %s",
         record_dir, strerror(errno));
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
      fwdsp_subprocs = calloc( max_subprocs + 1, sizeof(struct fwdsp_subproc) );
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
   fwdsp_set_exit_cb(fwdsp_subproc_exit_cb);

   // Find the fwdsp path
   fwdsp_path = cfg_get_exp("fwdsp.path");
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
    * Tear down every remaining subprocess, including encoders retained by
    * fwdsp.hangtime. fwdsp_destroy() also closes IPC and releases any cached
    * stream headers associated with the slot.
    */
   if (fwdsp_subprocs) {
      for (int i = 0 ; i < max_subprocs ; i++) {
         if (fwdsp_subprocs[i].pl_id[0] != '\0') {
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
      if (fwdsp_subprocs[i].pl_id[0] == '\0') {
         continue;
      }

      if (strncmp(id, fwdsp_subprocs[i].pl_id, 4) == 0 && fwdsp_subprocs[i].is_tx == is_tx) {
         return i;
      }
   }
   return -1;
}

struct fwdsp_subproc *fwdsp_find_channel_instance(const char *id, bool is_tx,
   const char *channel_uuid) {
   if (!id || !channel_uuid || !*channel_uuid || !fwdsp_subprocs) {
      return NULL;
   }
   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];
      if (sp->pl_id[0] != '\0' && sp->is_tx == is_tx &&
          strncmp(sp->pl_id, id, 4) == 0 &&
          strncmp(sp->channel_uuid, channel_uuid, sizeof(sp->channel_uuid)) == 0) {
         return sp;
      }
   }
   return NULL;
}

struct fwdsp_subproc *fwdsp_find_instance(const char *id, bool is_tx) {
   int i = fwdsp_find_offset(id, is_tx);
   return (i >= 0) ? &fwdsp_subprocs[i] : NULL;
}

static struct fwdsp_subproc *fwdsp_create(const char *id, enum fwdsp_io_type io_type, bool is_tx,
   const char *channel_uuid) {
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

      if (sp && (sp->pl_id[0] == '\0') &&
          (sp->pl_id[1] == '\0') &&
          (sp->pl_id[2] == '\0') &&
          (sp->pl_id[3] == '\0') ) {
         Log(LOG_CRIT, "fwdsp", "Assigning fwdsp slot %d to new codec %s.%s", i, id, (is_tx ? "tx" : "rx"));
         // Clear the memory for reuse
         memset( sp, 0, sizeof(struct fwdsp_subproc) );
         // Fill the struct
         memcpy(sp->pl_id, id, 4);
         if (channel_uuid) {
            snprintf(sp->channel_uuid, sizeof(sp->channel_uuid), "%s", channel_uuid);
         }

         sp->is_tx = is_tx;
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
      sp = fwdsp_create(id, io_type, is_tx, NULL);
      if (!sp) {
         Log(LOG_CRIT, "fwdsp", "Failure in fwdsp_create call");
      } else {
         Log(LOG_DEBUG, "fwdsp", "Spawned new instance of fwdsp at %p for codec %s.%s", (void *)sp, id,
            (is_tx ? "tx" : "rx"));
      }
   } else {
      Log(LOG_DEBUG, "fwdsp", "Using existing fwdsp instance %p for codec %s.%s", (void *)sp, id, (is_tx ? "tx" : "rx"));
   }
   return sp;
}

static bool fwdsp_destroy(struct fwdsp_subproc *sp) {
   if (!sp || sp->pl_id[0] == '\0' || sp->destroying) {
      return true;
   }
   // Mark the slot before touching any child or Mongoose state. Teardown can
   // be reached from expiry, SIGCHLD handling, codec switching and shutdown;
   // a late callback must never free the same slot-owned buffers twice.
   sp->destroying = true;
   // Let Mongoose close both wrapped sockets on its next poll. Detach their
   // contexts before the slot is reused; never free a connection mid-poll or
   // close its descriptor behind the event loop's back.
#ifdef USE_MONGOOSE
   if (sp->mg_stdout_conn) {
      ((struct fwdsp_io_conn *)sp->mg_stdout_conn->fn_data)->sp = NULL;
      sp->mg_stdout_conn->is_closing = 1;
      sp->mg_stdout_conn = NULL;
      sp->fw_stdout = -1;
   }
   if (sp->mg_stderr_conn) {
      ((struct fwdsp_io_conn *)sp->mg_stderr_conn->fn_data)->sp = NULL;
      sp->mg_stderr_conn->is_closing = 1;
      sp->mg_stderr_conn = NULL;
      sp->fw_stderr = -1;
   }
#endif // USE_MONGOOSE

   // Kill subprocess
   if (sp->pid > 0) {
      kill(sp->pid, SIGTERM);
      nanosleep(&(struct timespec) { .tv_sec = 1 }, NULL);

      if (waitpid(sp->pid, NULL, WNOHANG) == 0) {
         kill(sp->pid, SIGKILL);
         waitpid(sp->pid, NULL, 0);
      } else {
         waitpid(sp->pid, NULL, 0);
      }
      sp->pid = -1;
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
   memset( sp, 0, sizeof(*sp) );

   if (active_slots > 0) {
      active_slots--;
   }
   return true;
}

static bool fwdsp_child_dup_fd(int oldfd, int newfd) {
   if (oldfd == newfd) {
      return true;
   }

   if (dup2(oldfd, newfd) < 0) {
      return false;
   }
   return true;
}

//
// Holy shite batman, there's some scary in here lol
//
bool fwdsp_spawn(struct fwdsp_subproc *sp) {
   if (!sp) {
      return false;
   }

   int in_pipe[2], out_pipe[2], err_pipe[2], control_pipe[2];
   int sock_pair[2];
   struct mg_mgr *manager = fwdsp_mg_manager();

   if (sp->io_type == FW_IO_STDIO) {
      if (!manager) {
         return false;
      }
#ifndef _WIN32
      if (socketpair(AF_UNIX, SOCK_STREAM, 0, in_pipe) ||
          socketpair(AF_UNIX, SOCK_STREAM, 0, out_pipe) ||
          socketpair(AF_UNIX, SOCK_STREAM, 0, err_pipe) ||
          socketpair(AF_UNIX, SOCK_STREAM, 0, control_pipe)) {
#else
      if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe) || pipe(control_pipe)) {
#endif
         perror("pipe");
         return false;
      }
   }

   pid_t pid = fork();
   if (pid < 0) {
      perror("fork");
      return false;
   }

   const char *fwdsp_path = cfg_get_exp("fwdsp.path");
   const char *fwdsp_config = config_file;
   char pipeline_key[64];
   snprintf(pipeline_key, sizeof(pipeline_key), "pipeline:%s.%s", sp->pl_id,
      sp->is_tx ? "tx" : "rx");
   // Parent defaults differ: client capture versus server test noise. Pass the
   // resolved pipeline so the child does not substitute its standalone defaults.
   const char *child_pipeline = cfg_get(pipeline_key);
   if (!fwdsp_path || fwdsp_path[0] == '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set [fwdsp] path to point at fwdsp bin");

      return false;
   }

   if (pid == 0) {
      // --- Child ---
      if (sp->io_type == FW_IO_STDIO) {
         /*
          * Move the child ends to their fixed descriptors.
          *
          * stdin   = media input
          * stdout  = media output
          * stderr  = logging
          * fd 3    = control
          */
         if (!fwdsp_child_dup_fd(in_pipe[0], STDIN_FILENO) ||
             !fwdsp_child_dup_fd(out_pipe[1], STDOUT_FILENO) ||
             !fwdsp_child_dup_fd(err_pipe[1], STDERR_FILENO) ||
             !fwdsp_child_dup_fd(control_pipe[0], 3)) {
            _exit(126);
         }

         /*
          * Close every socketpair descriptor except descriptors which are now
          * one of our fixed child descriptors.
          */
         int child_fds[] = {
            in_pipe[0],
            in_pipe[1],
            out_pipe[0],
            out_pipe[1],
            err_pipe[0],
            err_pipe[1],
            control_pipe[0],
            control_pipe[1]
         };

         for (size_t i = 0 ;
              i < sizeof(child_fds) / sizeof(child_fds[0]) ;
              i++) {
            int fd = child_fds[i];

            if (fd != STDIN_FILENO &&
                fd != STDOUT_FILENO &&
                fd != STDERR_FILENO &&
                fd != 3) {
               close(fd);
            }
         }
      }

      if (sp->is_tx) {
         execl(fwdsp_path, fwdsp_path,
            "-f", fwdsp_config,
            "-c", sp->pl_id,
            "-C", "3",
            "-p", child_pipeline ? child_pipeline : "",
            "-t",
            NULL);
      } else if (sp->is_video) {
         execl(fwdsp_path, fwdsp_path,
            "-f", fwdsp_config,
            "-c", sp->pl_id,
            "-C", "3",
            "-p", child_pipeline ? child_pipeline : "",
            "-v",
            "-t",
            NULL);
      } else {
         execl(fwdsp_path, fwdsp_path,
            "-f", fwdsp_config,
            "-c", sp->pl_id,
            "-C", "3",
            "-p", child_pipeline ? child_pipeline : "",
            NULL);
      }

      perror("execl");
      _exit(127);
   }
   // --- Parent ---
   sp->pid = pid;

   // cfg_get_exp() returns a malloc'd string we own
   free( (char *)fwdsp_path );
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

#ifdef	USE_MONGOOSE
      // Hook up stdout/stderr to Mongoose immediately. The fn_data must be a
      // heap-allocated struct fwdsp_io_conn: fwdsp_read_cb() frees it on
      // MG_EV_CLOSE. Passing `sp` itself (which lives inside the shared
      // fwdsp_subprocs array) made the callback free() the whole array --
      // corrupting the heap and later crashing mg_iobuf_free when a client
      // connected ("double free or corruption").
      if (sp->fw_stdout) {
         struct fwdsp_io_conn *ctx = calloc(1, sizeof(*ctx) );

         if (ctx) {
            ctx->sp = sp;
            ctx->is_stderr = false;
            sp->mg_stdout_conn = mg_wrapfd(manager, sp->fw_stdout, fwdsp_read_cb, ctx);
         } else {
            sp->mg_stdout_conn = NULL;
         }
      }
      if (sp->fw_stderr) {
         struct fwdsp_io_conn *ctx = calloc(1, sizeof(*ctx) );
         if (ctx) {
            ctx->sp = sp;
            ctx->is_stderr = true;
            sp->mg_stderr_conn = mg_wrapfd(manager, sp->fw_stderr, fwdsp_read_cb, ctx);
         } else {
            sp->mg_stderr_conn = NULL;
         }
      }

      if (!sp->mg_stdout_conn || !sp->mg_stderr_conn) {
         Log(LOG_CRIT, "fwdsp",
            "Failed to attach fds to event loop for codec %s.%s",
            sp->pl_id, sp->is_tx ? "tx" : "rx");
         return false;
      }
#endif	// USE_MONGOOSE
   }
   Log(LOG_DEBUG, "fwdsp", "Spawned codec %s.%s at pid %d", sp->pl_id, sp->is_tx ? "tx" : "rx", sp->pid);

   // The application starts recording with its user and radio direction.

   return true;
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

         if (!sp || !sp->pid) {
            if (!sp || !fwdsp_spawn(sp) ) {
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

bool fwdsp_write_samples(const char codec_id[5], bool is_tx,
   const void *data, size_t len) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, is_tx);
   const uint8_t *bytes = data;

   if (!sp) {
      Log(LOG_WARN, "fwdsp",
         "write_samples: no fwdsp instance for %s.%s",
         codec_id, is_tx ? "tx" : "rx");
      return true;
   }

   if (sp->fw_stdin <= 0) {
      Log(LOG_WARN, "fwdsp",
         "write_samples: %s.%s has invalid stdin fd %d pid %d",
         codec_id, is_tx ? "tx" : "rx",
         sp->fw_stdin, sp->pid);
      return true;
   }

   if (!bytes || len == 0) {
      Log(LOG_WARN, "fwdsp",
         "write_samples: %s.%s got empty frame",
         codec_id, is_tx ? "tx" : "rx");
      return true;
   }

   if (len > UINT32_MAX) {
      Log(LOG_WARN, "fwdsp",
         "write_samples: %s.%s frame too large: %zu bytes",
         codec_id, is_tx ? "tx" : "rx", len);
      return true;
   }

   /*
    * fwdsp stdin is SOCK_STREAM, so preserve the media packet boundary:
    *
    *    4-byte big-endian payload length
    *    payload
    */
   uint32_t frame_len = htonl((uint32_t)len);
   const uint8_t *header = (const uint8_t *)&frame_len;
   size_t remaining = sizeof(frame_len);

   while (remaining > 0) {
      /* The child may exit before SIGCHLD is reaped; return EPIPE safely. */
      ssize_t written = send(sp->fw_stdin, header, remaining, MSG_NOSIGNAL);

      if (written > 0) {
         header += written;
         remaining -= (size_t)written;
         continue;
      }

      if (written < 0 && errno == EINTR) {
         continue;
      }

      int saved_errno = errno;

      Log(LOG_WARN, "fwdsp",
         "write_samples: header write failed for %s.%s "
         "fd %d pid %d: errno=%d (%s)",
         codec_id, is_tx ? "tx" : "rx",
         sp->fw_stdin, sp->pid,
         saved_errno, strerror(saved_errno));

      if (saved_errno == EPIPE) {
         /*
          * The child no longer has its stdin open. Arrange for the
          * normal child-reaping path to check it rather than repeatedly
          * trying to feed a dead subprocess.
          */
         fwdsp_sigchld_pending = 1;
      }

      return true;
   }

   remaining = len;

   while (remaining > 0) {
      ssize_t written = send(sp->fw_stdin, bytes, remaining, MSG_NOSIGNAL);

      if (written > 0) {
         bytes += written;
         remaining -= (size_t)written;
         continue;
      }

      if (written < 0 && errno == EINTR) {
         continue;
      }

      int saved_errno = errno;

      Log(LOG_WARN, "fwdsp",
         "write_samples: payload write failed for %s.%s "
         "fd %d pid %d: errno=%d (%s)",
         codec_id, is_tx ? "tx" : "rx",
         sp->fw_stdin, sp->pid,
         saved_errno, strerror(saved_errno));

      if (saved_errno == EPIPE) {
         fwdsp_sigchld_pending = 1;
      }

      return true;
   }

   return false;
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

   return send(sp->fw_control, &msg, sizeof(msg), MSG_NOSIGNAL) == (ssize_t)sizeof(msg) ? false : true;
}

static int fwdsp_encoder_hangtime(void) {
   const char *hangtime_s = cfg_get_exp("fwdsp.hangtime");
   int hangtime = hangtime_s ? atoi(hangtime_s) : 60;

   free((char *)hangtime_s);
   return hangtime < 0 ? 0 : hangtime;
}

static void fwdsp_idle_pipeline(struct fwdsp_subproc *sp) {
   if (!sp || sp->pid <= 0) {
      return;
   }

   sp->refcount = 0;

   // is_tx means fwdsp consumes raw sound-card PCM and emits encoded media:
   // it is an encoder. Keep encoders warm for fwdsp.hangtime so PTT/codec
   // reuse does not pay process/GStreamer startup latency. Decoders are cheap
   // to recreate and can otherwise accumulate one process per old codec.
   if (sp->is_tx) {
      int hangtime = fwdsp_encoder_hangtime();

      if (hangtime > 0) {
         // A warm encoder must not keep producing frames for the channel after
         // a codec switch. Pause the GStreamer pipeline but keep the process.
         fwdsp_send_control(sp, FWDSP_CTRL_PAUSE, 0);
         sp->cleanup_deadline = time(NULL) + hangtime;
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

      if (!sp || sp->pl_id[0] == '\0' || sp->pid <= 0) {
         continue;
      }

      // Channel-less instances are used only by rrclient. Their lifetime is driven
      // explicitly by audio_switch_codec()/fwdsp_codec_stop(), not by the
      // server subscription table.
      if (sp->channel_uuid[0] == '\0') {
         if (sp->refcount == 0 && sp->cleanup_deadline > 0 &&
             sweep_now >= sp->cleanup_deadline) {
            fwdsp_destroy(sp);
         }
         continue;
      }

      struct rr_mediachan *channel = media_chan_find_uuid(sp->channel_uuid);
      u_int32_t chan_id = channel ? (u_int32_t)(channel - media_channels) + 1 : 0;
      int users = 0;

      // A lingering encoder still points at the same channel UUID, but it is
      // no longer active after that channel switches codec. Do not let its
      // subscribers resurrect its refcount during the sweep.
      bool active_codec = channel && channel->codec[0] != '\0' &&
         strncmp(channel->codec, sp->pl_id, 4) == 0;
      rrconn_t *cur = http_client_list;

      while (active_codec && cur) {
         if (cur->is_ws && cur->authenticated &&
             ((channel->direction == RR_BINFRAME_DIR_RX &&
               chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id)) ||
              (channel->direction == RR_BINFRAME_DIR_TX &&
               chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id)))) {
            users++;
         }
         cur = cur->next;
      }

      if (users > 0) {
         if (sp->is_tx && sp->refcount == 0 && sp->cleanup_deadline > 0) {
            sp->replay_headers = true;
            fwdsp_send_control(sp, FWDSP_CTRL_RESUME, 0);
         }
         sp->refcount = users;
         sp->cleanup_deadline = 0;
      } else if (sp->refcount > 0) {
         fwdsp_idle_pipeline(sp);
         continue;
      }

      if (sp->pid > 0 && sp->refcount == 0 && sp->cleanup_deadline > 0 &&
          sweep_now >= sp->cleanup_deadline) {
         Log(LOG_INFO, "fwdsp", "Cleaning up idle encoder %s.%s",
            sp->pl_id, (sp->is_tx ? "tx" : "rx"));
         fwdsp_destroy(sp);
      }
   }
}

// Start (or ref up) a pipeline for a codec magic (e.g. "mu08") in one
// direction. Returns the channel id, or -1 on failure.
int fwdsp_codec_start(const char codec_id[5], bool is_tx, const char *channel_uuid) {
   if (!codec_id || codec_id[0] == '\0') {
      return -1;
   }

   struct fwdsp_subproc *sp = fwdsp_find_channel_instance(codec_id, is_tx, channel_uuid);
   if (!sp && (!channel_uuid || !*channel_uuid)) {
      sp = fwdsp_find_or_create(codec_id, FW_IO_STDIO, is_tx);
   }

   if (!sp && channel_uuid) {
      sp = fwdsp_create(codec_id, FW_IO_STDIO, is_tx, channel_uuid);
   }

   if (!sp) {
      Log(LOG_CRIT, "fwdsp", "Failed to start fwdsp for %s.%s", codec_id, (is_tx ? "tx" : "rx"));
      return -1;
   }

   if (!sp->pid) {
      if (!fwdsp_spawn(sp) ) {
         Log(LOG_CRIT, "fwdsp", "Failed to spawn fwdsp for %s.%s", codec_id, (is_tx ? "tx" : "rx"));
         return -1;
      }
   } else if (sp->is_tx && sp->refcount == 0 && sp->cleanup_deadline > 0) {
      // Reuse a warm encoder retained by fwdsp.hangtime.
      sp->replay_headers = true;
      fwdsp_send_control(sp, FWDSP_CTRL_RESUME, 0);
   }
   sp->cleanup_deadline = 0;
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
   } else {
      sp = fwdsp_find_instance(codec, is_tx);
   }

   if (!sp) {
      Log(LOG_DEBUG, "fwdsp", "fwdsp_codec_stop: no instance for %s.%s channel %s",
         codec, (is_tx ? "tx" : "rx"), (channel_uuid ? channel_uuid : "-"));
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

int fwdsp_codec_switch(const char *old_codec, const char *new_codec, bool is_tx,
   const char *channel_uuid) {
   if (!new_codec || strlen(new_codec) != 4) {
      return -1;
   }

   if (old_codec && strlen(old_codec) == 4 &&
       strncmp(old_codec, new_codec, 4) == 0) {
      struct fwdsp_subproc *sp = channel_uuid && *channel_uuid ?
         fwdsp_find_channel_instance(new_codec, is_tx, channel_uuid) :
         fwdsp_find_instance(new_codec, is_tx);

      if (sp && sp->pid > 0) {
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
      fwdsp_find_channel_instance(new_codec, is_tx, channel_uuid) :
      fwdsp_find_instance(new_codec, is_tx);
   if (replacement && replacement->stream_headers_len) {
      replacement->replay_headers = true;
   }

   // Start the replacement first so switching never creates an avoidable
   // media gap. Then release the old process. Encoders linger; decoders die.
   if (old_codec && strlen(old_codec) == 4 &&
       strncmp(old_codec, new_codec, 4) != 0) {
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
      Log(LOG_DEBUG, "fwdsp",
         "fwdsp_codec_stop_immediate: no instance for %s.%s channel %s",
         codec, is_tx ? "tx" : "rx",
         channel_uuid ? channel_uuid : "-");
      return -1;
   }

   fwdsp_destroy(sp);
   return 0;
}

int fwdsp_codec_stop_immediate(const char *codec, bool is_tx) {
   return fwdsp_codec_stop_channel_immediate(codec, is_tx, NULL);
}
