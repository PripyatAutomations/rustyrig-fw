//
// fwp-manager.c: Deal with starting and stopping fwdsp instances as needed
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// codec negotiation should call fwdsp_create 
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include "../ext/libmongoose/mongoose.h"
#include "rrserver/state.h"
#include "common/logger.h"
#include "common/codecneg.h"
#include "rrserver/fwdsp-mgr.h"

#define	FWDSP_MAX_SUBPROCS	100

defconfig_t defcfg_fwdsp[] = {
   { "codecs.allowed", 	"pc16 mu16 mu08",	"Preferred codecs" },
#ifdef _WIN32
   { "fwdsp.path",	"fwdsp.exe",		"Path to fwdsp binary" },
#else
   { "fwdsp.path",	"fwdsp",		"Path to fwdsp binary" },
#endif
   { "subproc.max",	"16",			"Maximum allowed de/encoder processes" },
   { "subproc.debug",	"false",		"Show extra debug messages" },
   //// XXX: We can put default pipelines here using the syntax pipeline:id.tx and pipeline:id.rx where id is 4 char id
   { NULL,		NULL,			NULL }
};
const char *fwdsp_path = NULL;
bool fwdsp_mgr_ready = false;
static int active_slots = 0;
static int max_subprocs = 4;
static struct fwdsp_subproc *fwdsp_subprocs;
static int next_channel_id = 1;
extern char *config_file;		// main.c
extern struct mg_mgr mg_mgr;
static fwdsp_exit_cb_t on_fwdsp_exit = NULL;

static void fwdsp_subproc_exit_cb(struct fwdsp_subproc *sp, int status) {
   Log(LOG_INFO, "fwdsp", "Pipeline %s.%s at pid %d exited (status=%d)", sp->pl_id, (sp->is_tx ? "tx" : "rx"), sp->pid, status);
   // Optionally notify websocket clients, or log, etc.
}

static void fwdsp_set_exit_cb(fwdsp_exit_cb_t cb) {
   on_fwdsp_exit = cb;
}

static void fwdsp_sigchld(int sig) {
   int status;
   pid_t pid;

   while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      for (int i = 0; i < max_subprocs; i++) {
         struct fwdsp_subproc *sp = &fwdsp_subprocs[i];
         if (sp->pid == pid) {
            if (on_fwdsp_exit) {
               on_fwdsp_exit(sp, status);
            }
            memset(sp, 0, sizeof(struct fwdsp_subproc));
            active_slots--;
            break;
         }
      }
   }
}

static void fwdsp_read_cb(struct mg_connection *c, int ev, void *ev_data) {
   struct fwdsp_io_conn *ctx = c->fn_data;

   if (ev == MG_EV_READ) {
      struct mg_str *data = (struct mg_str *) ev_data;

      Log(LOG_DEBUG, "fwdsp", "[%s %.*s]",
          ctx->is_stderr ? "stderr" : "stdout",
          (int) data->len, data->buf);
   } else if (ev == MG_EV_CLOSE) {
      free(ctx);
   }
}

bool fwdsp_init(void) {
   if (fwdsp_mgr_ready) {
      return true;
   }

   const char *max_subprocs_s = cfg_get("fwdsp:subproc.max");;
   max_subprocs = 0;

   if (max_subprocs_s) {
      max_subprocs = atoi(max_subprocs_s);
   } else {
      Log(LOG_CRIT, "config",  "fwdsp:subproc.max must be set in config for fwdsp manager to work!");
      return true;
   }

   // Sanity check as some hams are crazy? ;)
   if (max_subprocs <= 0 || max_subprocs > FWDSP_MAX_SUBPROCS) {
      Log(LOG_CRIT, "config", "fwdsp:subproc.max <%d> is invalid: range=0-%d", max_subprocs, FWDSP_MAX_SUBPROCS);
      return true;
   }

   // if not allocated, try to allocate it
   if (!fwdsp_subprocs) {
      fwdsp_subprocs = calloc(max_subprocs + 1, sizeof(struct fwdsp_subproc));
   }

   // did we OOM?
   if (!fwdsp_subprocs) {
      fprintf(stderr, "OOM in fwdsp_init\n");
      exit(1);
   }

   // Setup signal handling for SIGCHLD
   struct sigaction sa = {
      .sa_handler = fwdsp_sigchld,
      .sa_flags = SA_RESTART | SA_NOCLDSTOP
   };
   sigemptyset(&sa.sa_mask);
   sigaction(SIGCHLD, &sa, NULL);
   fwdsp_set_exit_cb(fwdsp_subproc_exit_cb);

   // Find the fwdsp path
   fwdsp_path = cfg_get("fwdsp.path");
   if (!fwdsp_path || fwdsp_path[0] == '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set fwdsp.path to point at fwdsp binary");
      return NULL;
   }

   fwdsp_mgr_ready = true;
   return false;
}

static int fwdsp_find_offset(const char *id, bool is_tx) {
   if (!id || max_subprocs <= 0) {
      return -1;
   }

   for (int i = 0; i < max_subprocs; i++) {
      if (strncmp(id, fwdsp_subprocs[i].pl_id, 4) == 0 && fwdsp_subprocs[i].is_tx == is_tx) {
         return i;
      }
   }
   return -1;
}

static struct fwdsp_subproc *fwdsp_find_instance(const char *id, bool is_tx) {
   int i = fwdsp_find_offset(id, is_tx);
   return (i >= 0) ? &fwdsp_subprocs[i] : NULL;
}

static struct fwdsp_subproc *fwdsp_create(const char *id, enum fwdsp_io_type io_type, bool is_tx) {
   if (id == NULL) { 
      Log(LOG_CRIT, "fwdsp", "create: Invalid parameters: id == NULL");
      return NULL;
   }

   if (active_slots >= max_subprocs) {
      Log(LOG_CRIT, "fwdsp", "We're out of fwdsp slots. %d of %d used", active_slots, max_subprocs);
      return NULL;
   }

   // Find the desired pipeline
   // Find an unused slot
   for (int i = 0; i < max_subprocs; i++) {
      if ((fwdsp_subprocs[i].pl_id[0] == '\0') &&
          (fwdsp_subprocs[i].pl_id[1] == '\0') &&
          (fwdsp_subprocs[i].pl_id[2] == '\0') &&
          (fwdsp_subprocs[i].pl_id[3] == '\0')) {
         struct fwdsp_subproc *sp = &fwdsp_subprocs[i];
         Log(LOG_CRIT, "fwdsp", "Assigning fwdsp slot %d to new codec %s.%s", i, id, (is_tx ? "tx" : "rx"));
         // Clear the memory for reuse
         memset(sp, 0, sizeof(struct fwdsp_subproc));
         // Fill the struct
         memcpy(sp->pl_id, id, 4);

         // We need to INVERT the TX flag here, because the client is telling us which codec it wants for the direction, we must match
         sp->is_tx = !is_tx;
         sp->chan_id = next_channel_id++;
         sp->io_type = io_type;
         active_slots++;

         // Connect IO
         switch(io_type) {
            case FW_IO_STDIO:
               break;
            default:
            case FW_IO_NONE:
               break;
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
      sp = fwdsp_create(id, io_type, is_tx);

      if (!sp) {
         Log(LOG_CRIT, "fwdsp", "Failure in fwdsp_create call");
      } else {
         Log(LOG_DEBUG, "fwdsp", "Spawned new instance of fwdsp at %x for codec %s.%s", sp, id, (is_tx ? "tx" : "rx"));
      }
   } else {
      Log(LOG_DEBUG, "fwdsp", "Using existing fwdsp instance %x for codec %s.%s", sp, id, (is_tx ? "tx" : "rx"));
   }
   return sp;
}

static bool fwdsp_destroy(struct fwdsp_subproc *sp) {
   if (!sp || sp->pl_id[0] == '\0') {
      return true;
   }

#if	0
   // Disconnect stdout/stderr from event loop
   if (sp->mg_stdout_conn) {
      mg_mgr_disconnect(sp->mg_stdout_conn);
      sp->mg_stdout_conn = NULL;
   }

   if (sp->mg_stderr_conn) {
      mg_mgr_disconnect(sp->mg_stderr_conn);
      sp->mg_stderr_conn = NULL;
   }
#endif

   // Kill subprocess
   if (sp->pid > 0) {
      kill(sp->pid, SIGTERM);
      nanosleep(&(struct timespec){ .tv_sec = 1 }, NULL);

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
      if (sp->fw_stdin > 0)  {
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
   memset(sp, 0, sizeof(*sp));

   if (active_slots > 0)
      active_slots--;

   return true;
}

static struct mg_connection *fwdsp_attach_fd_to_mgr(struct mg_mgr *mgr,
                                                    struct fwdsp_subproc *sp,
                                                    int fd,
                                                    bool is_stderr) {

   struct fwdsp_io_conn *ctx = calloc(1, sizeof(struct fwdsp_io_conn));

   if (!ctx) {
      return NULL;
   }

   ctx->sp = sp;
   ctx->is_stderr = is_stderr;

   struct mg_connection *c = mg_wrapfd(mgr, fd, fwdsp_read_cb, ctx);
   if (!c) {
      Log(LOG_CRIT, "fwdsp", "Failed to wrap fd %d for %s", fd,
          is_stderr ? "stderr" : "stdout");
      free(ctx);
      return NULL;
   }

   return c;
}

bool fwdsp_spawn(struct fwdsp_subproc *sp) {
   if (!sp) {
      return false;
   }

   int in_pipe[2], out_pipe[2], err_pipe[2];
   int sock_pair[2];

   if (sp->io_type == FW_IO_STDIO) {
      if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) {
         perror("pipe");
         return false;
      }
   }

   pid_t pid = fork();
   if (pid < 0) {
      perror("fork");
      return false;
   }

   const char *fwdsp_path = cfg_get("fwdsp.path");
   if (!fwdsp_path || fwdsp_path[0] == '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set fwdsp.path to point at fwdsp bin");
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
         execl(fwdsp_path, fwdsp_path, "-f", config_file, "-c", sp->pl_id, "-s", "-t", NULL);
      } else {
         execl(fwdsp_path, fwdsp_path, "-f", config_file, "-c", sp->pl_id, "-s", NULL);
      }

      perror("execl");
      _exit(127);
   }

   // --- Parent ---
   sp->pid = pid;

   if (sp->io_type == FW_IO_STDIO) {
      close(in_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[1]);
      sp->fw_stdin  = in_pipe[1];
      sp->fw_stdout = out_pipe[0];
      sp->fw_stderr = err_pipe[0];

      // Hook up stdout/stderr to Mongoose immediately
//      sp->mg_stdout_conn = mg_wrapfd(&mg_mgr, sp->fw_stdout, fwdsp_read_cb, sp);
//      sp->mg_stderr_conn = mg_wrapfd(&mg_mgr, sp->fw_stderr, fwdsp_read_cb, sp);

      if (!sp->mg_stdout_conn || !sp->mg_stderr_conn) {
         Log(LOG_CRIT, "fwdsp", "Failed to attach fds to event loop for codec %s.%s", sp->pl_id, sp->is_tx ? "tx" : "rx");
         return false;
      }
   }

   Log(LOG_DEBUG, "fwdsp", "Spawned codec %s.%s at pid %d", sp->pl_id, sp->is_tx ? "tx" : "rx", sp->pid);
   return true;
}

bool ws_send_capab(struct mg_connection *c) {
   char *capab_msg = codecneg_send_supported_codecs();

   if (capab_msg) {
      Log(LOG_DEBUG, "codecneg", "Sending capab msg: %s", capab_msg);
      mg_ws_send(c, capab_msg, strlen(capab_msg), WEBSOCKET_OP_TEXT);
      free(capab_msg);
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

#if	0
   while (token) {
      au_codec_mapping_t *c = au_codec_find_by_magic(token);

      if (c && c->magic) {
         struct fwdsp_subproc *sp = fwdsp_find_or_create(c->magic, FW_IO_STDIO, tx_mode);

         if (sp && !sp->pid) {
            if (!fwdsp_spawn(sp)) {
               Log(LOG_CRIT, "fwdsp", "Failed to spawn fwdsp for codec %s.%s", token, (tx_mode ? "tx" : "rx"));
               fwdsp_destroy(sp);
               sp = NULL;
            }
         }
         free(tmp);
         return sp;
      }
      token = strtok_r(NULL, " ", &saveptr);
   }
#endif
   free(tmp);
   Log(LOG_CRIT, "fwdsp", "No usable codecs found in list: %s for %s", codec_list, (tx_mode ? "tx" : "rx"));
   return NULL;
}

int fwdsp_get_chan_id(const char *magic, bool is_tx) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(magic, is_tx);
   return (sp) ? sp->chan_id : -1;
}

void fwdsp_sweep_expired(void) {
   time_t now = time(NULL);

   for (int i = 0; i < max_subprocs; i++) {
      struct fwdsp_subproc *sp = &fwdsp_subprocs[i];

      if (sp->pid > 0 && sp->refcount == 0 && sp->cleanup_deadline > 0 && now >= sp->cleanup_deadline) {
         Log(LOG_INFO, "fwdsp", "Cleaning up idle pipeline %s.%s", sp->pl_id, (sp->is_tx ? "tx" : "rx"));
         fwdsp_destroy(sp);
      }
   }
}

int fwdsp_codec_start(const char codec_id[5], bool is_tx) {
   if (codec_id[0] == '\0') {
      return -1;
   }
#if	0
   au_codec_mapping_t *c = au_codec_by_id(id);

   if (!c || !c->magic) {
      return -1;
   }

   if (c->refcount == 0) {
      struct fwdsp_subproc *sp = fwdsp_find_or_create(c->magic, FW_IO_STDIO, is_tx);

      if (!sp || !fwdsp_spawn(sp)) {
         Log(LOG_CRIT, "fwdsp", "Failed to start fwdsp for %s.%s", c->magic, (is_tx ? "tx" : "rx"));
         return -1;
      }
   }

   return au_codec_start(id, is_tx);
#endif
   return -1;
}

int fwdsp_codec_stop(const char *codec, bool is_tx) {
#if	0
   au_codec_mapping_t *c = au_codec_by_id(id);

   if (!c || !c->magic) {
      return -1;
   }

   int rc = au_codec_stop(id, is_tx);
   if (rc < 0) {
      return rc;
   }

   if (c->refcount == 0) {
      struct fwdsp_subproc *sp = fwdsp_find_instance(c->magic, is_tx);

      if (sp) {
         const char *hangtime_s = cfg_get("fwdsp.hangtime");
         int hangtime = hangtime_s ? atoi(hangtime_s) : 60;

         if (hangtime > 0) {
            sp->cleanup_deadline = time(NULL) + hangtime;
         } else {
            fwdsp_destroy(sp);
         }
      }
   }
#endif

   return 0;
}
