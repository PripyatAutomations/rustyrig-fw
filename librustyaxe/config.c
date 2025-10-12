#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/config.h>
#include <librustyaxe/logger.h>
#include <librustyaxe/util.file.h>
#include <librustyaxe/posix.h>

extern defconfig_t defcfg[];

const char *config_file = NULL;
dict *cfg = NULL;			// User configuration values from config file / ui
dict *default_cfg = NULL;		// Hard-coded defaults (defcfg.c)
dict *servers = NULL;			// Holds a list of servers where applicable (client and fwdsp)
cfg_cb_list_t *cfg_callbacks = NULL;

int dict_merge(dict *dst, dict *src) {
   if (!dst || !src) {
      return -1;
   }

   int rank = 0;
   const char *key;
   char *val;
   while ((rank = dict_enumerate(src, rank, &key, &val)) >= 0) {
      if (dict_add(dst, key, val) != 0) {
         continue;
      }
   }
   return 0;
}

dict *dict_merge_new(dict *a, dict *b) {
   // XXX: Should this return whichever of a or b exists? I feel NULL makes it more clear the merge failed...
   if (!a || !b) {
      Log(LOG_WARN, "dict", "dict_merge_new called with NULL a <%p> or NULL b <%p>", a, b);
      return NULL;
   }

   dict *merged = dict_new();
   if (!merged) {
      fprintf(stderr, "OOM in dict_merge_new?!\n");
      return NULL;
   }

   const char *key;
   char *val;
   int rank = 0;

   // Copy from a
   while ((rank = dict_enumerate(a, rank, &key, &val)) >= 0) {
      if (dict_add(merged, key, val) != 0) {
         Log(LOG_WARN, "dict", "dict_merge_new: Failed merging A for a:<%p>, b:<%p>", a, b);
         dict_free(merged);
         return NULL;
      }
   }

   rank = 0;
   // Copy from b (overwriting a’s entries if necessary)
   while ((rank = dict_enumerate(b, rank, &key, &val)) >= 0) {
      if (dict_add(merged, key, val) != 0) {
         Log(LOG_WARN, "dict", "dict_merge_new: Failed merging B for a:<%p>, b:<%p>", a, b);
         dict_free(merged);
         return NULL;
      }
   }

   return merged;
}

bool cfg_set_default(dict *d, char *key, char *val) {
   if (!key || !d) {
      Log(LOG_WARN, "config", "cfg_set_default: dict:<%p> key:<%p> is not valid", d, key);
      return true;
   }

//   Log(LOG_CRAZY, "config", "Setting default for dict:<%p>/%s to '%s'", d, key, val);

   if (dict_add(d, key, val) != 0) {
      Log(LOG_WARN, "config", "defcfg dict:<%p> failed to set key |%s| to val |%s| at <%p>", d, key, val, val);
      return true;
   }

   return false;
}

bool cfg_set_defaults(dict *d, defconfig_t *defaults) {
   if (!d) {
      Log(LOG_WARN, "config", "cfg_set_defaults: NULL dict");
      return true;
   }

   if (!defaults) {
      Log(LOG_WARN, "config", "cfg_set_defaults: NULL input");
      return true;
   }

   Log(LOG_CRAZY, "config", "cfg_set_defaults: Loading defaults from <%p>", defaults);

   int i = 0;
   int warnings = 0;
   while (defaults[i].key) {
      if (!defaults[i].val) {
         Log(LOG_CRAZY, "config", "cfg_set_defaults: Skipping key |%s| as its empty", defaults[i].key);
         i++;
         continue;
      }

//      Log(LOG_CRAZY, "config", "cfg_set_defaults: |%s| => |%s|", defaults[i].key, defaults[i].val);

      if (cfg_set_default(d, defaults[i].key, defaults[i].val)) {
         Log(LOG_WARN, "config", "cfg_set_defaults: Failed to set key: |%s|", defaults[i].key);
         warnings++;
      }

      i++;
   }

   Log(LOG_INFO, "config", "Imported %d default settings with %d warnings", i, warnings);
   return true;
}

bool cfg_detect_and_load(const char *configs[], int num_configs) {
   const char *homedir = getenv("HOME");

   // Find and load the configuration file
   char *fullpath = find_file_by_list(configs, num_configs);

   if (!default_cfg) {
      default_cfg = dict_new();
   }

   if (fullpath) {
      config_file = strdup(fullpath);
      if (!(cfg = cfg_load(fullpath))) {
         Log(LOG_CRIT, "core", "Couldn't load config \"%s\", using defaults instead", fullpath);
      } else {
         Log(LOG_DEBUG, "config", "Loaded config from '%s'", fullpath);
      }
      free(fullpath);
   } else {
     // Use default settings and save it to default
     cfg = default_cfg;
     Log(LOG_WARN, "core", "No config file found, saving defaults");
   }
   return false;
}

bool cfg_add_callback(const char *path, const char *section, bool (*cb)()) {
   if (!section || !cb) {
      return true;
   }

   Log(LOG_DEBUG, "config", "add_cb: path=%s section=%s cb=<%p>", path, section, (void *)cb);

   cfg_cb_list_t *new_cb = malloc(sizeof(cfg_cb_list_t));
   memset(new_cb, 0, sizeof(cfg_cb_list_t));

   if (!new_cb) {
      fprintf(stderr, "OOM in cfg_add_callback\n");
      return true;
   }

   if (path) {
      new_cb->path = strdup(path);
   }

   if (section) {
      new_cb->section = strdup(section);
   }
   new_cb->callback = cb;

   Log(LOG_DEBUG, "config", "Stored config callback cb:<%p> for section:|%s| path:|%s|", cb, section, path);

   // store our new callback
   if (!cfg_callbacks) {
      cfg_callbacks = new_cb;
   } else {
      // Find the end of the list
      cfg_cb_list_t *cbp = cfg_callbacks;

      while (cbp) {
         if (!cbp->next) {
            cbp->next = new_cb;
            break;
         }
         cbp = cbp->next;
      }
   }

   return false;
}

static bool cfg_dispatch_callback(const char *path, int line, const char *section, const char *buf) {
   if (!path || !section || !buf) {
      return true;
   }

   cfg_cb_list_t *cbp = cfg_callbacks, *prev = NULL;
   if (!cbp) {
      return false;
   }

   Log(LOG_CRIT, "config", "cfg_dispatch_callback: starting cbp=%p", cbp);
   int i = 0;
   while (cbp && i < CONFIG_MAX_CALLBACKS) {
      if (cbp->section && fnmatch(cbp->section, section, 0) == 0) {
         if (!cbp->path || (fnmatch(cbp->path, path, 0) == 0)) {
            Log(LOG_DEBUG, "config", "cfg_dispatch_callback: Found callback at <%p> for section %s (%s) in path %s (%s)", cbp->callback, section, cbp->section, path, cbp->path);

            if (cbp->callback) {
               cbp->callback(path, line, section, buf);
            } else {
               Log(LOG_WARN, "config", "cfg_dispatch_callback: The callback at <%p> for section |%s| path |%s| doesn't have a valid function attached", cbp, section, path);
            }
         }
      }
      i++;
      prev = cbp;
      cbp = cbp->next;
      fprintf(stderr, "prev;<%p> cbp:<%p> list:<%p>\n", prev, cbp, cfg_callbacks);
   }
   if (i > 10) {
      Log(LOG_WARN, "config", "%s: made %d iterations for cbp:<%p>", __FUNCTION__, i, cfg_callbacks);
   }
   return false;
}

dict *cfg_load(const char *path) {
   int line = 0, errors = 0;
   char buf[32768];
   char *end, *skip, *key, *val;
   char this_section[128];

   memset(this_section, 0, sizeof(this_section));

   if (!file_exists(path)) {
      fprintf(stderr, "Can't find config file %s\n", path);
      return NULL;
   }

   dict *newcfg = dict_new();
   if (!newcfg) {
      fprintf(stderr, "OOM in cfg_load?!\n");
      exit(1);
   }

   FILE *fp = fopen(path, "r");
   if (!fp) {
      free(newcfg);
      fprintf(stderr, "Failed to open config %s: %d:%s\n", path, errno, strerror(errno));
      return NULL;
   }

   fseek(fp, 0, SEEK_SET);
   servers = dict_new();

   bool in_comment = false;
   do {
      memset(buf, 0, sizeof(buf));
      if (!fgets(buf, sizeof(buf) - 1, fp)) {
         break;
      }
      line++;

      // skip leading spaces
      skip = buf;
      while (*skip == ' ') {
         skip++;
      }

      // trim trailing newlines and whitespace
      end = buf + strlen(buf) - 1;
      while (end >= buf && (*end == '\r' || *end == '\n' || *end == ' ' || *end == '\t')) {
         *end-- = '\0';
      }

      if ((end - skip) < 0) {
         continue;
      }

      // Handle line continuations
      while (1) {
         // Trim trailing newlines / carriage returns
         while (end >= buf && (*end == '\r' || *end == '\n')) {
            *end-- = '\0';
         }

         // Trim trailing spaces/tabs before checking for '\'
         while (end >= buf && (*end == ' ' || *end == '\t')) {
            *end-- = '\0';
         }

         if (end < buf || *end != '\\') {
            // No continuation
            break;
         }

         // Check if space before '\'
         bool space_before = (end > buf && *(end - 1) == ' ');

         // Remove the backslash
         *end = '\0';
         end--;

         // Also remove trailing spaces before backslash if any remain
         while (end >= buf && (*end == ' ' || *end == '\t')) {
            *end-- = '\0';
         }

         // Read continuation line
         char contbuf[sizeof(buf)];
         if (!fgets(contbuf, sizeof(contbuf), fp)) {
            break;  // EOF or error
         }

         line++;

         // Trim leading whitespace on continuation line
         char *cont = contbuf;
         while (*cont == ' ' || *cont == '\t') {
            cont++;
         }

         // Trim trailing whitespace/newlines on continuation line
         char *e2 = cont + strlen(cont) - 1;
         while (e2 >= cont && (*e2 == '\r' || *e2 == '\n' || *e2 == ' ' || *e2 == '\t')) {
            *e2-- = '\0';
         }

         // Append a space if needed
         if (space_before && strlen(buf) > 0) {
            strncat(buf, " ", sizeof(buf) - strlen(buf) - 1);
         }

         // Append continuation content
         strncat(buf, cont, sizeof(buf) - strlen(buf) - 1);

         // Update end pointer for next loop iteration
         end = buf + strlen(buf) - 1;
      }

      /////////////////////////////
      // parse the line contents //
      /////////////////////////////
      if (*skip == '*' && *(skip + 1) == '/') {
         in_comment = false;
         continue;
      } else if (*skip == '/' && *(skip + 1) == '*') {
         in_comment = true;
         continue;
      } else if (in_comment) {
         continue;
      } else if ((*skip == '/' && *(skip + 1) == '/') || *skip == '#' || *skip == ';') {
         continue;
      } else if (*skip == '[' && *end == ']') {
         size_t section_len = sizeof(this_section);
         size_t skip_len = strlen(skip);
         size_t copy_len = ((skip_len - 1) > section_len ? section_len : (skip_len - 1));
         memset(this_section, 0, section_len);
         snprintf(this_section, copy_len, "%s", skip + 1);
         continue;
      }

      if (this_section[0] == '\0') {
         fprintf(stderr, "[Debug] config %s has line outside section header at line %d: %s\n", path, line, buf);
         errors++;
         continue;
      }

// XXX: All sections except general below will eventually be moved to callbacks
      if (strncasecmp(this_section, "general", 7) == 0) {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');

         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;
            // trim leading whitespace off the value
            while (*val == ' ' || *val == '\t') {
               val++;
            }
         }

         if (!key) {
            continue;
         }

         // trim trailing whitespace off the key
         char *key_end = key + strlen(key) - 1;
         while (key_end >= key && (*key_end == ' ' || *key_end == '\t')) {
            *key_end-- = '\0';
         }

         if (!key && !val) {
            continue;
         }

         if (strncasecmp(this_section, "general", 7) != 0) {
            char keybuf[384];
            memset(keybuf, 0, sizeof(keybuf));
            snprintf(keybuf, sizeof(keybuf), "%s:%.s", this_section, key);
            dict_add(newcfg, keybuf, val);
         } else {
            dict_add(newcfg, key, val);
         }
      } else if (strncasecmp(this_section, "server:", 7) == 0) {
         key = NULL;
         val = NULL;
         char *eq = strchr(skip, '=');
         char fullkey[256];
         if (eq) {
            *eq = '\0';
            key = skip;
            val = eq + 1;

            // trim leading whitespace off the value
            while (*val == ' ' || *val == '\t') {
               val++;
            }
            memset(fullkey, 0, sizeof(fullkey));
            snprintf(fullkey, sizeof(fullkey), "%s.%s", this_section + 7, key);
            dict_add(servers, fullkey, val);
         } else {
            Log(LOG_WARN, "config", "Malformed line parsing |%s| at %s:%d", buf, path, line);
         }
      } else if (cfg_dispatch_callback(path, line, this_section, buf)) {
         Log(LOG_WARN, "config", "Unknown configuration section |%s| parsing |%s| at %s:%d", this_section, buf, path, line);
         errors++;
      }
   } while (!feof(fp));

   if (errors > 0) {
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with %d warnings/errors", line, path, errors);
   } else {
      Log(LOG_INFO, "config", "cfg loaded %d lines from %s with no errors", line, path);
   }

   return newcfg;
}

const char *cfg_get(const char *key) {
   if (!key) {
      Log(LOG_WARN, "config", "got cfg_get with NULL key!");
      return NULL;
   }

   const char *p = dict_get(cfg, key, NULL);

   // nope! try default
   if (!p) {
      if (!default_cfg) {
         Log(LOG_DEBUG, "config", "defcfg not found looking for key |%s|", key);
         return NULL;
      }
      p = dict_get(default_cfg, key, NULL);
      Log(LOG_DEBUG, "config", "returning default value |%s| for key |%s|", p, key);
   } else {
      Log(LOG_CRAZY, "config", "returning user value |%s| for key |%s|", p, key);
   }
   return p;
}

bool cfg_get_bool(const char *key, bool def) {
   return dict_get_bool(cfg, key, def);
}

int cfg_get_int(const char *key, int def) {
   return dict_get_int(cfg, key, def);
}

unsigned int cfg_get_uint(const char *key, unsigned int def) {
   return dict_get_uint(cfg, key, def);
}

// You *MUST* free the return value
const char *cfg_get_exp(const char *key) {
   return dict_get_exp(cfg, key);
}

////////////////////////////

static void cfg_print_servers(dict *servers, FILE *fp) {
   if (!servers || !fp) {
      return;
   }

   const char *key;
   char *val;
   int rank = 0;
   dict *seen = dict_new();

   while ((rank = dict_enumerate(servers, rank, &key, &val)) >= 0) {
      // Find the prefix before the first '.'
      char *dot = strchr(key, '.');
      if (!dot) {
         continue;
      }

      size_t prefix_len = dot - key;
      char prefix[64];
      if (prefix_len >= sizeof(prefix)) {
         continue;
      }

      strncpy(prefix, key, prefix_len);
      prefix[prefix_len] = '\0';

      // Skip if we've already emitted this prefix
      if (dict_get(seen, prefix, NULL)) {
         continue;
      }
      dict_add(seen, prefix, "1");

      fprintf(fp, "[server:%s]\n", prefix);

      // Emit all keys with this prefix
      int inner_rank = 0;
      const char *inner_key;
      char *inner_val;
      while ((inner_rank = dict_enumerate(servers, inner_rank, &inner_key, &inner_val)) >= 0) {
         if (strncmp(inner_key, prefix, prefix_len) == 0 && inner_key[prefix_len] == '.') {
            fprintf(fp, "%s=%s\n", inner_key + prefix_len + 1, inner_val ? inner_val : "");
         }
      }

      fputc('\n', fp);
   }

   dict_free(seen);
}

bool cfg_save(dict *d, const char *path) {
   FILE *fp = fopen(path, "w");
   if (!fp) {
      Log(LOG_WARN, "config", "Failed to open save file: '%s': %d:%s", path, errno, strerror(errno));
      return true;
   }

   dict *merged = NULL;

   // Right-side argument overrides defaults
   merged = dict_merge_new(default_cfg, d);

   // Dump general settings
   fprintf(fp, "[general]\n");
   dict_dump(merged, fp);

   // Release the memory used
   dict_free(merged);

   // Print the server sections
   cfg_print_servers(servers, fp);

   fflush(fp);
   fclose(fp);

   return false;
}

// XXX: This needs to compare changes and create a dict with the differences in it
bool cfg_apply_new(dict *oldcfg, dict *newcfg) {
   cfg = newcfg;
   return false;
}

// XXX: Make a function to return a dict of ONLY changes from a -> b
dict *dict_diff(dict *a, dict *b) {
   // XXX: Make this work
   return NULL;
}

// Config save stuff
#if	0	// XX: Not yet
   char pathbuf[PATH_MAX+1];
   memset(pathbuf, 0, sizeof(pathbuf));

   // If we don't couldnt find a config file, save the defaults to ~/.config/rrclient.cfg
   if (homedir && empty_config) {
#ifdef _WIN32
      snprintf(pathbuf, sizeof(pathbuf), "%%APPDATA%%\\rrclient\\rrclient.cfg");
#else
      snprintf(pathbuf, sizeof(pathbuf), "%s/.config/rrclient.cfg", homedir);
#endif
      if (!file_exists(pathbuf)) {
         Log(LOG_CRIT, "main", "Saving default config to %s since it doesn't exist", pathbuf);
         cfg_save(cfg, pathbuf);
         config_file = pathbuf;
      }
   }
#endif

///////////////////
// Reload Events //
///////////////////
// This facility allows us to notify modules when a configuration key is changed
reload_event_t *reload_events = NULL;

reload_event_t *reload_event_add(const char *key, bool (*callback)(), const char *note) {
   if (!callback || !key) {
      return NULL;
   }

   reload_event_t *r = malloc(sizeof(reload_event_t));
   if (!r) {
      fprintf(stderr, "OOM in reload_event_add!\n");
      return NULL;
   }

   memset(r, 0, sizeof(reload_event_t));
   r->key = strdup(key);
   r->callback = callback;

   if (note) {
      r->note = strdup(note);
   }

   // find end of the list and append it
   reload_event_t *ep = reload_events;
   while (ep) {
      if (!ep->next) {
         ep->next = r;
         break;
      }
      ep = ep->next;
   }
   return r;
}

bool reload_event_list(const char *key) {
   if (!key) {
      return true;
   }

   reload_event_t *r = reload_events;

   Log(LOG_DEBUG, "cfg.reload", "****** rel dump ******\n");
   r = reload_event_find(key, NULL);
   while (r) {
      Log(LOG_DEBUG, "cfg.reload", "* %s has callback at <%p>: %s\n", r->key, r->callback, r->note ? r->note : "*** No note ***");
      r = r->next;
   }
   Log(LOG_DEBUG, "cfg.reload", "**********************\n");
   return false;
}

// Find an event in the linked list                                                          
reload_event_t *reload_event_find(const char *key, bool (*callback)()) {
   reload_event_t *r = reload_events;

   // one or both must be passed
   if (!key && !callback) {
      return NULL;
   }

   while (r) {
      bool match_key = false, match_cb = false;

      if (strcasecmp(key, r->key) == 0) {
         match_key = true;
      }

      if (callback && r->callback  && callback == r->callback) {
         match_cb = true;
      }

      Log(LOG_CRAZY, "cfg.reload",
          "reload_event_find matched entry at <%p>, key: %s <%p>, callback:%s <%p>",
          r, (match_key ? "true" : "false"), r->key,
          (match_cb ? "true" : "false"), r->callback);

      // If (no key or key matches) and (no callback or callback matches), return the entry
      if ((!key || match_key) && (!callback || match_cb)) {
         return r;
      }
      r = r->next;
   }
   return NULL;
}

bool reload_event_run(const char *key) {
   if (!reload_events) {
      return false;
   }

   reload_event_t *rl = reload_event_find(key, NULL);
   if (rl) {
      Log(LOG_CRAZY, "cfg.reload", "reload: run callback at <%p> for key '%s'", rl->callback, key);
      rl->callback(key);
   }
   return false;
}

// Remove a reload event from the list
bool reload_event_remove(reload_event_t *evt) {
   if (!evt) {
      return true;
   }

   // Free resources
   free(evt);

   return false;
}
