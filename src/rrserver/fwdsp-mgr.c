//
// fwp-manager.c: Deal with starting and stopping fwdsp instances as needed
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
#include "../ext/libmongoose/mongoose.h"
#include "rustyrig/state.h"
#include "common/logger.h"
#include "rustyrig/fwdsp-mgr.h"
defconfig_t defcfg_fwdsp[] = {
   { "codecs.allowed", 	"pc16 mu16 mu08",	"Preferred codecs" },
   { "fwdsp.path",	NULL,			"Path to fwdsp binary" },// /usr/local/bin/fwdsp
   { "subproc.max",	"16",			"Maximum allowed de/encoder processes" },
   { "subproc.debug",	"false",		"Show extra debug messages" },
   { NULL,		NULL,			NULL }
};


////////////

dict *fwdsp_cfg = NULL;
bool fwdsp_mgr_ready = false;
static int active_slots = 0;
static int max_subprocs = 0;
struct fwdsp_subproc	*fwdsp_subprocs;

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

struct fwdsp_subproc *fwdsp_create(const char *id, const char *path, const char *pipeline) {
   return NULL;
}

bool fwdsp_destroy(struct fwdsp_subproc *instance) {
   if (!instance) {
      return true;
   }

   int offset = fwdsp_find_offset(instance->pl_id);
   struct fwdsp_subproc *sp = &fwdsp_subprocs[offset];
   memset(sp, 0, sizeof(struct fwdsp_subproc));
   return false;
}
