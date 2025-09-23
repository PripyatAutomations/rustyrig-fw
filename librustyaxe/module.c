//
// module.c: Loadable module support
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * support logging to a a few places
 *	Targets: syslog console flash (file)
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <librustyaxe/core.h>

//
// Here we deal with loading and unloading modules
//
#define	RUSTY_MODULE_API_VER	100

// Module functions
typedef struct rr_module_hook {
   char                 *name;		// name for calling
   enum {
      CB_MSG = 0,		// MeSsaGe with length (default)
      CB_ARGV,			// argument list & count
      CB_VOID,			// no arguments
   } callback_type;
   bool                (*callback)();	// Callback
   struct rr_module_hook	*next;			// next hook
} rr_module_hook_t;

rr_module_t *modules = NULL;

char *concat_path(const char *dir, const char *file, const char *suffix) {
   char *tmp = malloc(PATH_MAX + 1);
   memset(tmp, 0, PATH_MAX + 1);
   if (suffix) {
      snprintf(tmp, PATH_MAX, "%s/%s.%s", dir, file, suffix);
   } else {
      snprintf(tmp, PATH_MAX, "%s/%s", dir, file);
   }

   return tmp;
}
char *rr_find_module(const char *name) {
   // Try to find mod path and return it in an allocated string

   const char *cpath = cfg_get_exp("path.modules");
   char *tmp;
   if (cpath) {
      tmp = concat_path(cpath, name, NULL);

      if (file_exists(tmp)) {
         free((char *)cpath);
         return(tmp);
      }
      free((char *)cpath);
   }
   return NULL;
}

/////////////////////////
bool rr_load_module(const char *name) {
   bool rv = true;

   if (!name) {
      return true;
   }

   // Does the module exist?
   char *mod_path = rr_find_module(name);
   if (!mod_path) {
      // Display an error that the module wasn't found
      Log(LOG_CRIT, "module", "rr_load_module: Couldn't find module %s: File not found.", name);
      return true;
   }

   // Try to load the module
   void *dp = dlopen(mod_path, RTLD_NOW|RTLD_GLOBAL);
   if (!dp) {
      Log(LOG_CRIT, "module", "rr_load_module: Failed opening module %s: %d:%s", mod_path, errno, strerror(errno));
      rv = false;
      goto done;
   }
   Log(LOG_DEBUG, "module", "rr_load_module: Module %s opened from %s at <%x>", name, mod_path, dp);
   rr_module_t *mp = malloc(sizeof(rr_module_t));

   if (!mp) {
      fprintf(stderr, "OOM in rr_load_module!\n");
      return true;
   }
   memset(mp, 0, sizeof(rr_module_t));

   mp->dlptr = dp;
   mp->mod_path = strdup(mod_path);
   mp->mod_name = strdup(name);

   rr_module_event_t *ep = dlsym(mp->dlptr, "modinfo");

   // if no modinfo found, free memory and bail!
   if (!ep) {
      Log(LOG_CRIT, "module", "rr_load_module: Mod info not found loading %s", mod_path);
      dlclose(mp->dlptr);
      free(mp);
      goto done;
   }

   // Look for export list
   rr_module_event_t *mep = dlsym(mp->dlptr, "modexports");

   if (mep) {
      mp->mod_events = mep;
   } else {
      Log(LOG_WARN, "module", "rr_load_module: No exports found for module %s", mod_path);
   }

   // Add to module's list
   if (!modules) {
      modules = mp;
   } else {
      rr_module_t *lp = modules;
      int i = 1;
      while (lp) {
         // if this is the end of the list, exit leaving lp point at the end
         if (!lp->next) {
            Log(LOG_DEBUG, "module", "Adding module to list as %d entry", i);
            break;
         }
         lp = lp->next;
         i++;
      }
      lp->next = mp;
   }

   // XXX: Call initialization function for the new module

done:
   free(mod_path);
   return rv;
}

bool rr_unload_module(const char *name) {
   if (!name) {
      return true;
   }

   return false;
}
