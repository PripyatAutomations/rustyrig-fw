//
// fwp-manager.c: Deal with starting and stopping fwdsp instances as needed
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
#include <librrprotocol/rrprotocol.h>
#include <libfwdspmgr/fwdsp-mgr.h>

#define	FWDSP_MAX_SUBPROCS 100

defconfig_t defcfg_fwdsp[] = {
   {
      "codecs.allowed", "pc16 mu16 mu08", "Preferred codecs"
   },
#ifdef _WIN32
   {
      "path.fwdsp", "bin/fwdsp.exe", "Path to fwdsp binary"
   },
#else
   {
      "path.fwdsp", "bin/fwdsp", "Path to fwdsp binary"
   },
#endif
   {
      "path.fwdsp.config", "config/fwdsp.cfg", "Path to fwdsp configuration"
   },
   {
      "fwdsp.subproc.max", "16", "Maximum allowed de/encoder processes"
   },
   {
      "fwdsp.hangtime", "60", "How long to keep unused (en|de)coders alive after last use"
   },
   {
      "subproc.debug", "false", "Show extra debug messages"
   },
   //// XXX: We can put default pipelines here using the syntax pipeline:id.tx
   // and pipeline:id.rx where id is 4 char id
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
extern const char *config_file;          // librustyaxe/config.c
#ifdef USE_MONGOOSE
extern struct mg_mgr mg_mgr;             // rrserver/main.c defines it; rrclient links through librrprotocol's `mgr` alias
#pragma weak mg_mgr
extern struct mg_mgr mgr;
#pragma weak mgr
#endif
static fwdsp_exit_cb_t on_fwdsp_exit = NULL;

static struct mg_mgr *fwdsp_mg_manager(void) {
   return (&mg_mgr != NULL) ? &mg_mgr : &mgr;
}

static void fwdsp_subproc_exit_cb(struct fwdsp_subproc *sp, int status) {
   Log(LOG_INFO, "fwdsp", "Pipeline %s.%s at pid %d exited (status=%d)", sp->pl_id, (sp->is_tx ? "tx" : "rx"), sp->pid,
      status);
   // Optionally notify websocket clients, or log, etc.
}

static void fwdsp_set_exit_cb(fwdsp_exit_cb_t cb) {
   on_fwdsp_exit = cb;
}

static volatile sig_atomic_t fwdsp_sigchld_pending = 0;

static void fwdsp_sigchld(int sig) {
   // Async-signal-safe: only set a flag. waitpid(), Log() and touching the
   // subprocs array happen in fwdsp_reap_children() on the main loop.
   (void) sig;
   fwdsp_sigchld_pending = 1;
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
            memset( sp, 0, sizeof(struct fwdsp_subproc) );
            active_slots--;
            break;
         }
      }
   }
}

static void fwdsp_read_cb(struct mg_connection *c, int ev, void *ev_data) {
   struct fwdsp_io_conn *ctx = c->fn_data;

   if (!ctx) {
      return;
   }

   if (ev == MG_EV_READ) {
      size_t len = ev_data ? *(size_t *)ev_data : c->recv.len;

      if (len > c->recv.len) {
         len = c->recv.len;
      }

      if (ctx->is_stderr) {
         char message[1024];
         size_t message_len = len < sizeof(message) - 1 ? len : sizeof(message) - 1;
         memcpy(message, c->recv.buf, message_len);
         message[message_len] = '\0';
         Log(LOG_DEBUG, "fwdsp", "[stderr %s]", message);
      } else {
         struct rr_mediachan *channel = ctx->sp->channel_uuid[0] != '\0' ?
            media_chan_find_uuid(ctx->sp->channel_uuid) : media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
            ctx->sp->is_tx ? RR_BINFRAME_DIR_RX : RR_BINFRAME_DIR_TX, 0, 0);

         if (channel) {
            ws_media_broadcast_subscribed(channel, (const uint8_t *)c->recv.buf, len,
               ctx->sp->pl_id);
         } else {
            Log(LOG_WARN, "fwdsp", "No media channel for %s.%s output",
               ctx->sp->pl_id, ctx->sp->is_tx ? "tx" : "rx");
         }
      }
      mg_iobuf_del(&c->recv, 0, len);
   } else if (ev == MG_EV_CLOSE) {
      // Free only the wrapper we allocated in fwdsp_spawn(). ctx->sp points
      // into the shared fwdsp_subprocs array which is managed by the slot
      // allocator and must never be freed here.
      free(ctx);
   }
}

bool fwdsp_init(void) {
   if (fwdsp_mgr_ready) {
      return true;
   }
   const char *max_subprocs_s = cfg_get_exp("fwdsp.subproc.max");

   if (max_subprocs_s) {
      max_subprocs = atoi(max_subprocs_s);
      Log(LOG_DEBUG, "fwdsp-mgr", "fwdsp initializing with %d slots available", max_subprocs);
      free( (char *)max_subprocs_s );
   } else {
      Log(LOG_CRIT, "config", "fwdsp.subproc.max must be set in config for fwdsp manager to work!");

      return true;
   }

   // Sanity check as some hams are crazy? ;)
   if (max_subprocs <= 0 || max_subprocs > FWDSP_MAX_SUBPROCS) {
      Log(LOG_CRIT, "config", "fwdsp.subproc.max <%d> is invalid: range=0-%d", max_subprocs, FWDSP_MAX_SUBPROCS);

      return true;
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
   fwdsp_path = cfg_get_exp("path.fwdsp");

   if (!fwdsp_path) {
      Log(LOG_CRIT, "fwdsp", "You must set path.fwdsp to point at fwdsp binary");

      return NULL;
   }
   fwdsp_mgr_ready = true;

   return false;
}

bool fwdsp_fini(void) {
   free( (char *)fwdsp_path );
   fwdsp_path = NULL;

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

static struct fwdsp_subproc *fwdsp_find_channel_instance(const char *id, bool is_tx,
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

static struct fwdsp_subproc *fwdsp_find_instance(const char *id, bool is_tx) {
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

   // Find the desired pipeline
   // Find an unused slot
   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (sp && (sp->pl_id[0] == '\0') &&
          (sp->pl_id[1] == '\0') &&
          (sp->pl_id[2] == '\0') &&
          (sp->pl_id[3] == '\0') ) {
         Log( LOG_CRIT, "fwdsp", "Assigning fwdsp slot %d to new codec %s.%s", i, id, (is_tx ? "tx" : "rx") );
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
         fwdsp_spawn(sp);

         return sp;
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
         Log( LOG_DEBUG, "fwdsp", "Spawned new instance of fwdsp at %x for codec %s.%s", sp, id,
            (is_tx ? "tx" : "rx") );
      }
   } else {
      Log( LOG_DEBUG, "fwdsp", "Using existing fwdsp instance %x for codec %s.%s", sp, id, (is_tx ? "tx" : "rx") );
   }

   return sp;
}

static bool fwdsp_destroy(struct fwdsp_subproc *sp) {
   if (!sp || sp->pl_id[0] == '\0') {
      return true;
   }
   // Disconnect stdout/stderr from event loop
   // XXX: this was crashing! should be ok now
#if     defined(USE_MONGOOSE)

   if (sp->mg_stdout_conn) {
      mg_close_conn(sp->mg_stdout_conn);
      sp->mg_stdout_conn = NULL;
   }
#endif

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
   }
   // Clear struct
   memset( sp, 0, sizeof(*sp) );

   if (active_slots > 0) {
      active_slots--;
   }

   return true;
}

bool fwdsp_spawn(struct fwdsp_subproc *sp) {
   if (!sp) {
      return false;
   }
   int in_pipe[2], out_pipe[2], err_pipe[2];
   int sock_pair[2];
   struct mg_mgr *manager = fwdsp_mg_manager();

   if (sp->io_type == FW_IO_STDIO) {
      if (!manager) {
         return false;
      }
#ifndef _WIN32
      if (socketpair(AF_UNIX, SOCK_STREAM, 0, in_pipe) ||
          socketpair(AF_UNIX, SOCK_STREAM, 0, out_pipe) ||
          socketpair(AF_UNIX, SOCK_STREAM, 0, err_pipe)) {
#else
      if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) {
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
   const char *fwdsp_path = cfg_get_exp("path.fwdsp");
   const char *fwdsp_config = cfg_get_exp("path.fwdsp.config");

   if (!fwdsp_path || fwdsp_path[0] == '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set path.fwdsp to point at fwdsp bin");

      return false;
   }
   if (!fwdsp_config || fwdsp_config[0] == '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set path.fwdsp.config to point at fwdsp config");
      free( (char *)fwdsp_path );
      return false;
   }

   if (pid == 0) {
      // --- Child ---
      if (sp->io_type == FW_IO_STDIO) {
         dup2(in_pipe[0], 0);
         dup2(out_pipe[1], 1);
         dup2(err_pipe[1], 2);
         close(in_pipe[1]);
         close(out_pipe[0]);
         close(err_pipe[0]);
      }

      if (sp->is_tx) {
         execl(fwdsp_path, fwdsp_path, "-f", fwdsp_config, "-c", sp->pl_id, "-t", NULL);
      } else if (sp->is_video) {
         // video pipelines: -v makes fwdsp announce FW_MEDIA_VIDEO and treat
         // the pipeline as a video (not audio) stream
         execl(fwdsp_path, fwdsp_path, "-f", fwdsp_config, "-c", sp->pl_id, "-v", "-t", NULL);
      } else {
         execl(fwdsp_path, fwdsp_path, "-f", fwdsp_config, "-c", sp->pl_id, NULL);
      }
      perror("execl");
      _exit(127);
   }
    // --- Parent ---
   sp->pid = pid;
   // cfg_get_exp() returns a malloc'd string we own
   free( (char *)fwdsp_path );
   fwdsp_path = NULL;
   free( (char *)fwdsp_config );
   fwdsp_config = NULL;

   if (sp->io_type == FW_IO_STDIO) {
      close(in_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[1]);
      sp->fw_stdin = in_pipe[1];
      sp->fw_stdout = out_pipe[0];
      sp->fw_stderr = err_pipe[0];

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

      if (sp->fw_stdin) {
         sp->mg_stdin_conn = mg_wrapfd(manager, sp->fw_stdin, NULL, sp);
      }

      if (!sp->mg_stdout_conn || !sp->mg_stderr_conn || !sp->mg_stdin_conn) {
         Log(LOG_CRIT, "fwdsp", "Failed to attach fds to event loop for codec %s.%s", sp->pl_id,
            sp->is_tx ? "tx" : "rx");

         return false;
      }
   }
   Log(LOG_DEBUG, "fwdsp", "Spawned codec %s.%s at pid %d", sp->pl_id, sp->is_tx ? "tx" : "rx", sp->pid);

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
               Log( LOG_CRIT, "fwdsp", "Failed to spawn fwdsp for codec %s.%s", token, (tx_mode ? "tx" : "rx") );

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
   Log( LOG_CRIT, "fwdsp", "No usable codecs found in list: %s for %s", codec_list, (tx_mode ? "tx" : "rx") );

   return NULL;
}

int fwdsp_get_chan_id(const char *magic, bool is_tx) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(magic, is_tx);

   return (sp) ? sp->chan_id : -1;
}

bool fwdsp_write_samples(const char codec_id[5], bool is_tx, const void *data, size_t len) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, is_tx);
   const uint8_t *bytes = data;

   if (!sp || sp->fw_stdin <= 0 || !bytes || len == 0) {
      return true;
   }
   while (len > 0) {
      ssize_t written = write(sp->fw_stdin, bytes, len);
      if (written > 0) {
         bytes += written;
         len -= (size_t)written;
      } else if (written < 0 && errno == EINTR) {
         continue;
      } else {
         return true;
      }
   }
   return false;
}

void fwdsp_sweep_expired(void) {
   time_t now = time(NULL);

   if (!active_slots) {
      return;
   }

   for (int i = 0 ; i < max_subprocs ; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (!sp) {
         continue;
      }

      struct rr_mediachan *channel = sp->channel_uuid[0] != '\0' ?
         media_chan_find_uuid(sp->channel_uuid) : media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
         sp->is_tx ? RR_BINFRAME_DIR_RX : RR_BINFRAME_DIR_TX, 0, 0);
      u_int32_t chan_id = channel ? (u_int32_t)(channel - media_channels) + 1 : 0;
      int users = 0;
      rrconn_t *cur = http_client_list;

      while (channel && cur) {
         if (cur->is_ws && cur->authenticated &&
             ((channel->direction == RR_BINFRAME_DIR_RX &&
               chan_id_in_array(cur->rx_channels, MAX_RX_CHANNELS, chan_id)) ||
              (channel->direction == RR_BINFRAME_DIR_TX &&
               chan_id_in_array(cur->tx_channels, MAX_TX_CHANNELS, chan_id)))) {
            users++;
         }
         cur = cur->next;
      }

      if (sp->pid > 0 && users > 0) {
         sp->refcount = users;
         sp->cleanup_deadline = 0;
      } else if (sp->pid > 0 && sp->refcount > 0) {
         sp->refcount = 0;
         sp->cleanup_deadline = now + 5;
      }

      if (sp->pid > 0 && sp->refcount == 0 && sp->cleanup_deadline > 0 &&
          now >= sp->cleanup_deadline) {
         Log( LOG_INFO, "fwdsp", "Cleaning up idle pipeline %s.%s", sp->pl_id, (sp->is_tx ? "tx" : "rx") );
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
      Log( LOG_CRIT, "fwdsp", "Failed to start fwdsp for %s.%s", codec_id, (is_tx ? "tx" : "rx") );

      return -1;
   }

   if (!sp->pid) {
      if (!fwdsp_spawn(sp) ) {
         Log( LOG_CRIT, "fwdsp", "Failed to spawn fwdsp for %s.%s", codec_id, (is_tx ? "tx" : "rx") );

         return -1;
      }
   }
   sp->refcount++;

   return sp->chan_id;
}

// Start (or ref up) a video pipeline (e.g. webcam capture) for a codec magic.
// Like fwdsp_codec_start, but the subprocess is spawned with -v so fwdsp
// treats it as a video stream. Returns the channel id, or -1 on failure.
int fwdsp_video_start(const char codec_id[5], bool is_tx) {
   if (!codec_id || codec_id[0] == '\0') {
      return -1;
   }
   if (fwdsp_init() ) {
      Log(LOG_CRIT, "fwdsp", "fwdsp_video_start: mgr init failed");
      return -1;
   }
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec_id, !is_tx);

   if (!sp) {
      // create + spawn, marking the instance as video before exec
      sp = fwdsp_find_or_create(codec_id, FW_IO_STDIO, is_tx);

      if (!sp) {
         Log(LOG_CRIT, "fwdsp", "fwdsp_video_start: failed to create %s", codec_id);
         return -1;
      }
      // The instance may already be running (audio use of the same id?);
      // only spawn here when it has no pid yet.
      if (!sp->pid) {
         sp->is_video = true;

         if (!fwdsp_spawn(sp) ) {
            Log(LOG_CRIT, "fwdsp", "fwdsp_video_start: spawn failed for %s", codec_id);
            return -1;
         }
      }
   }
   sp->refcount++;

   return sp->chan_id;
}

// Drop a reference on a codec pipeline. When the last user goes away, either
// destroy the subproc outright or set the hangtime cleanup deadline.
int fwdsp_codec_stop(const char *codec, bool is_tx) {
   if (!codec || codec[0] == '\0') {
      return -1;
   }
   struct fwdsp_subproc *sp = fwdsp_find_instance(codec, is_tx);

   if (!sp) {
      Log(LOG_WARN, "fwdsp", "fwdsp_codec_stop: no instance for %s.%s", codec, (is_tx ? "tx" : "rx") );

      return -1;
   }

   if (sp->refcount > 0) {
      sp->refcount--;
   }

   if (sp->refcount == 0) {
      const char *hangtime_s = cfg_get_exp("fwdsp.hangtime");
      int hangtime = hangtime_s ? atoi(hangtime_s) : 60;
      free( (char *)hangtime_s );

      if (hangtime > 0) {
         sp->cleanup_deadline = time(NULL) + hangtime;
      } else {
         fwdsp_destroy(sp);
      }
   }

   return 0;
}
