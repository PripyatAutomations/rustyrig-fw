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
#include "rustyrig/state.h"
#include "common/logger.h"
#include "rustyrig/fwdsp-mgr.h"

// Some defaults
defconfig_t defcfg_fwdsp[] = {
   { "codecs.allowed", 	"pc16 mu16 mu08",	"Preferred codecs" },
   { "fwdsp.path",	NULL,			"Path to fwdsp binary" },
   { "subproc.max",	"16",			"Maximum allowed de/encoder processes" },
   { "subproc.debug",	"false",		"Show extra debug messages" },
   //// XXX: We can put default pipelines here using the syntax pipeline:id.tx and pipeline:id.rx where id is 4 char id
   { NULL,		NULL,			NULL }
};

////////////

dict *fwdsp_cfg = NULL;
bool fwdsp_mgr_ready = false;
static int active_slots = 0;
static int max_subprocs = 0;
struct fwdsp_subproc	*fwdsp_subprocs;

static void fwdsp_subproc_exit_cb(struct fwdsp_subproc *sp, int status) {
   Log(LOG_INFO, "fwdsp", "Pipeline %.*s exited (status=%d)", 4, sp->pl_id, status);
   // Optionally notify websocket clients, or log, etc.
}

static fwdsp_exit_cb_t on_fwdsp_exit = NULL;
void fwdsp_set_exit_cb(fwdsp_exit_cb_t cb) {
   on_fwdsp_exit = cb;
}

static void fwdsp_sigchld(int sig) {
   (void)sig;
   int status;
   pid_t pid;
   while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
      for (int i = 0; i < max_subprocs; i++) {
         struct fwdsp_subproc *sp = &fwdsp_subprocs[i];
         if (sp->pid == pid) {
            Log(LOG_DEBUG, "fwdsp", "Subproc %.*s (pid %d) exited", 4, sp->pl_id, pid);
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

bool fwdsp_init(void) {
   if (fwdsp_mgr_ready) {
      return true;
   }

   const char *max_subprocs_s = dict_get(fwdsp_cfg, "subproc.max", "4");
   max_subprocs = 0;
   if (max_subprocs_s) {
      max_subprocs = atoi(max_subprocs_s);
   } else {
      Log(LOG_CRIT, "config",  "fwdsp:subproc.max must be set in config for fwdsp manager to work!");
      return true;
   }

   // Sanity check, 100 is excessivve but some hams are crazy? ;)
   if (max_subprocs <= 0 || max_subprocs > 100) {
      Log(LOG_CRIT, "config", "fwdsp:subproc.max <%d> is invalid: range=0-100", max_subprocs);
      return true;
   }

   fwdsp_subprocs = calloc(max_subprocs + 1, sizeof(struct fwdsp_subproc));
   if (fwdsp_subprocs != NULL) {
      // OOM:
      fprintf(stderr, "OOM in fwdsp_init\n");
      exit(1);
   }

   struct sigaction sa = {
      .sa_handler = fwdsp_sigchld,
      .sa_flags = SA_RESTART | SA_NOCLDSTOP
   };
   sigemptyset(&sa.sa_mask);
   sigaction(SIGCHLD, &sa, NULL);
   fwdsp_set_exit_cb(fwdsp_subproc_exit_cb);  // registers your callback

   fwdsp_mgr_ready = true;
   return false;
}

int fwdsp_find_offset(const char *id) {
   if (id == NULL) {
      return -1;
   }

   if (max_subprocs <= 0) {
      Log(LOG_CRIT, "fwdsp", "You need to set fwdsp:subproc.max!");
      return -1;
    }
   
   for (int i = 0; i < max_subprocs; i++) {
      if ((fwdsp_subprocs[i].pl_id[0] == id[0]) &&
          (fwdsp_subprocs[i].pl_id[1] == id[1]) &&
          (fwdsp_subprocs[i].pl_id[2] == id[2]) &&
          (fwdsp_subprocs[i].pl_id[3] == id[3])) {

         Log(LOG_DEBUG, "fwdsp", "fwdsp_find_instance: Matched offset %d with pipeline ID %.*s", 4, i, fwdsp_subprocs[i].pl_id);
         return i;
       }
   }
   return -1;
}
struct fwdsp_subproc *fwdsp_find_instance(const char *id) {
   if (id == NULL) {
      return NULL;
   }

   if (max_subprocs <= 0) {
      Log(LOG_CRIT, "fwdsp", "You need to set fwdsp:subproc.max!");
      return NULL;
    }
   
   int i = fwdsp_find_offset(id);

   if (i < 0) {
      Log(LOG_CRIT, "fwdsp", "Couldn't find match for pipeline id %.*s", 4, id);
      return NULL;
   }

   if ((fwdsp_subprocs[i].pl_id[0] == id[0]) &&
       (fwdsp_subprocs[i].pl_id[1] == id[1]) &&
       (fwdsp_subprocs[i].pl_id[2] == id[2]) &&
       (fwdsp_subprocs[i].pl_id[3] == id[3])) {
      Log(LOG_DEBUG, "fwdsp", "fwdsp_find_instance: Matched %.*s", 4, fwdsp_subprocs[i].pl_id);
      return &fwdsp_subprocs[i];
    }
   return NULL;
}

struct fwdsp_subproc *fwdsp_create(const char *id, enum fwdsp_io_type io_type, bool is_tx) {
   if (id == NULL) { 
      Log(LOG_CRIT, "fwdsp", "create: Invalid parameters: id:<%x>", id);
      return NULL;
   }

   if (active_slots >= max_subprocs) {
      Log(LOG_CRIT, "fwdsp", "We're out of fwdsp slots. %d of %d used", active_slots, max_subprocs);
      return NULL;
   }

   // Find the fwdsp path
   const char *fwdsp_path = dict_get(fwdsp_cfg, "fwdsp.path", NULL);
   if (!fwdsp_path || fwdsp_path[0] != '\0') {
      Log(LOG_CRIT, "fwdsp", "You must set fwdsp:fwdsp.path to point at fwdsp bin");
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
         Log(LOG_CRIT, "fwdsp", "Assigning fwdsp slot %d to new codec %.*s", i, 4, sp->pl_id);
         // Clear the memory for reuse
         memset(sp, 0, sizeof(struct fwdsp_subproc));
         // Fill the struct
         memcpy(sp->pl_id, id, 4);

#if	0
         // Find the appropriate pipeline and copy it in
         if (pipeline && strlen(pipeline) > 0) {
            snprintf(sp->pipeline, sizeof(sp->pipeline),"%s", pipeline);
         }
#endif
         sp->io_type = io_type;
         active_slots++;

         // Connect IO
         switch(io_type) {
            case FW_IO_STDIO:
               break;
            case FW_IO_AF_UNIX:
               break;
            default:
            case FW_IO_NONE:
               break;
         }
         return sp;
      }
   }
   Log(LOG_CRIT, "fwdsp", "Out of subproc slots?! %d > %d", active_slots, max_subprocs);
   return NULL;
}

struct fwdsp_subproc *fwdsp_find_or_create(const char *id, enum fwdsp_io_type io_type, bool is_tx) {
   struct fwdsp_subproc *sp = fwdsp_find_instance(id);

   if (!sp) {
      sp = fwdsp_create(id, io_type, is_tx);
   }
   return sp;
}

bool fwdsp_destroy(struct fwdsp_subproc *sp) {
   if (!sp || sp->pl_id[0] == '\0') {
      return true;
   }

   // Kill the subprocess
   if (sp->pid > 0) {
      kill(sp->pid, SIGTERM);
      usleep(100000); // give it a moment to die
      if (waitpid(sp->pid, NULL, WNOHANG) == 0) {
         kill(sp->pid, SIGKILL); // force kill if still alive
         waitpid(sp->pid, NULL, 0); // reap
      } else {
         waitpid(sp->pid, NULL, 0); // normal reap
      }
   }

   // Close open fds
   if (sp->io_type == FW_IO_STDIO) {
      if (sp->fw_stdin  > 0) close(sp->fw_stdin);
      if (sp->fw_stdout > 0) close(sp->fw_stdout);
      if (sp->fw_stderr > 0) close(sp->fw_stderr);
   } else if (sp->io_type == FW_IO_AF_UNIX) {
      if (sp->unix_sock > 0) close(sp->unix_sock);
   }

   // Clear the struct
   memset(sp, 0, sizeof(struct fwdsp_subproc));
   active_slots--;
   return true;
}

bool fwdsp_spawn(struct fwdsp_subproc *sp, const char *path) {
   if (!sp || !path || !*path) return false;

   int in_pipe[2], out_pipe[2], err_pipe[2];
   int sock_pair[2];

   if (sp->io_type == FW_IO_STDIO) {
      if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) {
         perror("pipe");
         return false;
      }
   } else if (sp->io_type == FW_IO_AF_UNIX) {
      if (socketpair(AF_UNIX, SOCK_STREAM, 0, sock_pair) < 0) {
         perror("socketpair");
         return false;
      }
   }

   pid_t pid = fork();
   if (pid < 0) {
      perror("fork");
      return false;
   }

   if (pid == 0) {
      // Child
      if (sp->io_type == FW_IO_STDIO) {
         dup2(in_pipe[0], 0);
         dup2(out_pipe[1], 1);
         dup2(err_pipe[1], 2);
         close(in_pipe[1]);
         close(out_pipe[0]);
         close(err_pipe[0]);
      } else if (sp->io_type == FW_IO_AF_UNIX) {
         dup2(sock_pair[1], 0);
         dup2(sock_pair[1], 1);
         dup2(sock_pair[1], 2);
         close(sock_pair[0]);
      }
      execl(path, path, sp->pipeline, NULL);
      perror("execl");
      _exit(127);
   }

   // Parent
   sp->pid = pid;
   if (sp->io_type == FW_IO_STDIO) {
      close(in_pipe[0]);
      close(out_pipe[1]);
      close(err_pipe[1]);
      sp->fw_stdin = in_pipe[1];
      sp->fw_stdout = out_pipe[0];
      sp->fw_stderr = err_pipe[0];
   } else if (sp->io_type == FW_IO_AF_UNIX) {
      close(sock_pair[1]);
      sp->unix_sock = sock_pair[0];
   }

   return true;
}
