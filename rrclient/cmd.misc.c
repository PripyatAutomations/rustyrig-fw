//
// rrclient/cmd.misc.c: unsorted commands
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/audio.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>

extern bool dying;
extern time_t now;
extern bool syslog_clear(void);
extern const char *server_name; // remove this (connman.c)
extern rrconn_t *ws_conn;
extern const char *config_file;
extern dict *cfg;
extern dict *default_cfg;
extern defconfig_t defcfg[];
extern bool ui_confirm_quit(void);
void rrclient_print_callsign_line(const char *line);

static const char *cmd_set_type_name(defconfig_type_t type) {
   switch (type) {
   case DEFCONFIG_BOOL: return "bool";
   case DEFCONFIG_INT: return "int";
   case DEFCONFIG_UINT: return "uint";
   case DEFCONFIG_FLOAT: return "float";
   case DEFCONFIG_PATH: return "path";
   case DEFCONFIG_PASSWORD: return "password";
   case DEFCONFIG_ENUM: return "enum";
   case DEFCONFIG_STRING:
   default: return "string";
   }
}

static bool cmd_set_matches(const char *pattern, const char *key) {
   if (!pattern || !*pattern) return true;
   return (strchr(pattern, '*') || strchr(pattern, '?'))
      ? fnmatch(pattern, key, 0) == 0
      : strcmp(pattern, key) == 0;
}

static bool cmd_set_has_key(char **keys, size_t count, const char *key) {
   for (size_t i = 0; i < count; i++) {
      if (strcmp(keys[i], key) == 0) return true;
   }
   return false;
}

static int cmd_set_key_cmp(const void *a, const void *b) {
   const char *const *ka = a;
   const char *const *kb = b;
   return strcmp(*ka, *kb);
}

static void cmd_set_print_key(const char *key) {
   const defconfig_t *def = cfg_defconfig_find(key);
   const char *value = cfg ? dict_get(cfg, key, NULL) : NULL;
   if (!value && default_cfg) value = dict_get(default_cfg, key, NULL);
   if (!value) value = "(unset)";

   ui_print(ui_active_window_name(), "%s%s = %s {bright-black}[%s%s]{reset}",
      def ? "" : "* ", key, value,
      def ? cmd_set_type_name(def->type) : "custom",
      def && def->help ? "; " : "",
      def && def->help ? def->help : "");
}

static bool cmd_set_list(const char *pattern) {
   char **keys = NULL;
   size_t count = 0;
   size_t capacity = 0;
   const char *key = NULL;
   char *value = NULL;
   int rank = 0;

   /* cfg contains user-defined keys, including keys with no defconfig entry. */
   while (cfg && (rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!cmd_set_matches(pattern, key) || cmd_set_has_key(keys, count, key)) continue;
      if (count == capacity) {
         size_t next = capacity ? capacity * 2 : 64;
         char **grown = realloc(keys, next * sizeof(*keys));
         if (!grown) { free(keys); return false; }
         keys = grown;
         capacity = next;
      }
      keys[count++] = (char *)key;
   }

   /* Include effective defaults not copied into cfg by the loader. */
   for (size_t i = 0; defcfg[i].key; i++) {
      const char *defkey = defcfg[i].key;
      const char *defvalue = default_cfg ? dict_get(default_cfg, defkey, NULL) : NULL;
      if (!defvalue || !cmd_set_matches(pattern, defkey) || cmd_set_has_key(keys, count, defkey)) continue;
      if (count == capacity) {
         size_t next = capacity ? capacity * 2 : 64;
         char **grown = realloc(keys, next * sizeof(*keys));
         if (!grown) { free(keys); return false; }
         keys = grown;
         capacity = next;
      }
      keys[count++] = (char *)defkey;
   }

   qsort(keys, count, sizeof(*keys), cmd_set_key_cmp);
   for (size_t i = 0; i < count; i++) cmd_set_print_key(keys[i]);
   if (!count && pattern && *pattern && !strchr(pattern, '*') && !strchr(pattern, '?') &&
       cfg_defconfig_find(pattern)) {
      /* A known key may intentionally have no value (for example an optional
       * site setting).  /set key should still explain that key and show it as
       * unset rather than reporting it as unknown. */
      cmd_set_print_key(pattern);
      count = 1;
   }
   if (!count) {
      ui_print(ui_active_window_name(), "{yellow}No configuration keys match '%s'{reset}",
         (pattern && *pattern) ? pattern : "*");
   }
   free(keys);
   return true;
}

bool cmd_set(int argc, char **args) {
   if (argc < 2 || !args[1] || !*args[1]) {
      return cmd_set_list(NULL) ? false : true;
   }
   if (argc < 3 || !args[2]) {
      return cmd_set_list(args[1]) ? false : true;
   }
   char value[512] = "";
   for (int i = 2; i < argc; i++) {
      if (i > 2) strlcat(value, " ", sizeof(value));
      strlcat(value, args[i] ? args[i] : "", sizeof(value));
   }
   const defconfig_t *def = cfg_defconfig_find(args[1]);
   if (!def) {
      ui_print(ui_active_window_name(), "{red}Unknown configuration key: %s{reset}", args[1]);
      return false;
   }
   if (!cfg_set_value(args[1], value)) {
      ui_print(ui_active_window_name(), "{red}Invalid value for %s{reset}", args[1]);
      return false;
   }
   ui_print(ui_active_window_name(), "{green}Set %s = %s{reset}", args[1], cfg_get(args[1]));
   return false;
}

/* Run the local callsign helper without invoking a shell.  Callsign and grid
 * input is user supplied, so constructing a command string for popen() would
 * turn an otherwise harmless lookup into command injection. */
static bool run_local_lookup(const char *program, const char *config,
   const char *query, bool grid, bool no_cache) {
   if (!program || !*program || !config || !*config || !query || !*query) {
      return false;
   }

   int output_pipe[2];
   if (pipe(output_pipe) != 0) {
      return false;
   }

   pid_t child = fork();
   if (child < 0) {
      close(output_pipe[0]);
      close(output_pipe[1]);
      return false;
   }

   if (child == 0) {
      close(output_pipe[0]);
      if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
          dup2(output_pipe[1], STDERR_FILENO) < 0) {
         _exit(126);
      }
      close(output_pipe[1]);
      // Keep the configured absolute/relative path behavior, while also
      // preserving the old popen() behavior for a bare helper name by using
      // PATH lookup when no directory component was configured.
      bool has_dir = strchr(program, '/') != NULL;
      if (grid) {
         if (has_dir) {
            execl(program, program, "-q", "-f", config, "-g", query, (char *)NULL);
         } else {
            execlp(program, program, "-q", "-f", config, "-g", query, (char *)NULL);
         }
      } else {
         if (has_dir) {
            if (no_cache) execl(program, program, "-q", "-f", config, "-n", query, (char *)NULL);
            else execl(program, program, "-q", "-f", config, query, (char *)NULL);
         } else {
            if (no_cache) execlp(program, program, "-q", "-f", config, "-n", query, (char *)NULL);
            else execlp(program, program, "-q", "-f", config, query, (char *)NULL);
         }
      }
      dprintf(STDERR_FILENO, "callsign lookup exec failed for %s: %s\n",
         program, strerror(errno));
      _exit(127);
   }

   close(output_pipe[1]);
   FILE *output = fdopen(output_pipe[0], "r");
   if (!output) {
      close(output_pipe[0]);
      (void)waitpid(child, NULL, 0);
      return false;
   }

   char line[1024];
   while (fgets(line, sizeof(line), output)) {
      line[strcspn(line, "\r\n")] = '\0';
      if (*line && strncmp(line, "+NOTICE ", 8) != 0 &&
          strncmp(line, "+OK ", 4) != 0 &&
          strncmp(line, "+PROTO ", 7) != 0 &&
          strncmp(line, "+GOODBYE", 8) != 0 &&
          strcmp(line, "+EOR") != 0 && line[0] != '[' && line[0] != '<' &&
          strncmp(line, "==", 2) != 0) {
         rrclient_print_callsign_line(line);
      }
   }
   fclose(output);

   int status = 0;
   if (waitpid(child, &status, 0) < 0) {
      return false;
   }
   return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool cmd_qrz(int argc, char **args) {
   bool no_cache = argc == 3 && args[2] && strcasecmp(args[2], "nocache") == 0;
   if ((argc != 2 && !no_cache) || (argc == 3 && !no_cache) || !args[1] || !args[1][0]) {
      ui_print(ui_active_window_name(), "Usage: /qrz CALLSIGN [NOCACHE]");
      return true;
   }

   for (const unsigned char *p = (const unsigned char *)args[1]; *p; p++) {
      if (!isalnum(*p) && *p != '-' && *p != '/' && *p != '.') {
         ui_print(ui_active_window_name(), "Invalid callsign: %s", args[1]);
         return true;
      }
   }

   char *program = cfg_get_path("callsign-lookup:path");
   const char *qrz_user = cfg_get("callsign-lookup:qrz-username");
   const char *qrz_pass = cfg_get("callsign-lookup:qrz-password");
   if (!qrz_user || !*qrz_user || !qrz_pass || !*qrz_pass) {
      if (!ws_conn) {
         ui_print(ui_active_window_name(), "Callsign lookup is not configured locally and the server is disconnected");
         free(program);
         return true;
      }
      dict *request = dict_new();
      dict_add(request, "msg.type", "talk");
      dict_add(request, "talk.cmd", "qrz");
      char request_data[256];
      snprintf(request_data, sizeof(request_data), "%s%s", args[1], no_cache ? " NOCACHE" : "");
      dict_add(request, "talk.data", request_data);
      ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
      dict_free(request);
      ui_print(ui_active_window_name(), "Asking the server to look up %s", args[1]);
      free(program);
      return false;
   }
   if (!program || !*program || !config_file || !*config_file) {
      ui_print(ui_active_window_name(), "Callsign lookup is not configured locally");
      free(program);
      return true;
   }

   bool lookup_ok = run_local_lookup(program, config_file, args[1], false, no_cache);
   free(program);
   if (!lookup_ok) {
      ui_print(ui_active_window_name(), "Callsign lookup failed for %s", args[1]);
      return true;
   }
   return false;
}

bool cmd_grid(int argc, char **args) {
   if (argc != 2 || !args[1] || !args[1][0]) {
      ui_print(ui_active_window_name(), "Usage: /grid GRID|LAT,LON");
      return true;
   }
   for (const unsigned char *p = (const unsigned char *)args[1]; *p; p++) {
      if (!isalnum(*p) && *p != '-' && *p != '.' && *p != ',' &&
          *p != '+' && *p != ' ') {
         ui_print(ui_active_window_name(), "Invalid grid or coordinates: %s", args[1]);
         return true;
      }
   }

   char *program = cfg_get_path("callsign-lookup:path");
   if (!program || !*program || !config_file || !*config_file) {
      if (!ws_conn) {
      ui_print(ui_active_window_name(), "Callsign lookup is not configured locally and the server is disconnected");
         free(program);
         return true;
      }
      dict *request = dict_new();
      dict_add(request, "msg.type", "talk");
      dict_add(request, "talk.cmd", "grid");
      dict_add(request, "talk.data", args[1]);
      ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
      dict_free(request);
      ui_print(ui_active_window_name(), "Asking the server for grid information for %s", args[1]);
      free(program);
      return false;
   }

   bool lookup_ok = run_local_lookup(program, config_file, args[1], true, false);
   free(program);
   if (!lookup_ok) {
      ui_print(ui_active_window_name(), "Grid lookup failed for %s", args[1]);
      return true;
   }
   return false;
}

void rrclient_print_callsign_line(const char *line) {
   if (!line || !*line) {
      return;
   }

   /* The helper emits a compact, line-oriented response. Keep the protocol
    * header recognizable, and align the field labels without parsing or
    * discarding any returned data. */
   if (strncmp(line, "200 OK ", 7) == 0) {
      ui_print(ui_active_window_name(), "{bright-green}%s{reset}", line);
      return;
   }

   const char *colon = strchr(line, ':');
   if (colon && colon != line) {
      int label_len = (int)(colon - line);
      ui_print(ui_active_window_name(), "  {bright-cyan}%.*s:{reset}%s", label_len, line, colon + 1);
      return;
   }

   ui_print(ui_active_window_name(), "%s", line);
}

bool cmd_clear(int argc, char **args) {
   if (ui_mode == UI_MODE_TUI) {
      tui_clear_scrollback( tui_active_window() );
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      gtk_text_buffer_set_text(text_buffer, "", -1);
#endif
   }

   return false;
}

bool cmd_clearlog(int argc, char **args) {
#ifdef	USE_GTK
   syslog_clear();
#endif
   return false;
}

bool cmd_disconnect(int argc, char **args) {
   disconnect_server(server_name);
   return false;
}


bool cmd_quit(int argc, char **args) {
   const char *quitmsg = "no reason given";

   // -y/-yes/y/yes anywhere in the args quits without confirmation
   bool confirmed = false;
   int first_arg = 1;

   while (first_arg < argc && args[first_arg] &&
          (strcasecmp(args[first_arg], "-y") == 0 || strcasecmp(args[first_arg], "-yes") == 0 ||
           strcasecmp(args[first_arg], "y") == 0 || strcasecmp(args[first_arg], "yes") == 0) ) {
      confirmed = true;
      first_arg++;
   }

   if (args && first_arg < argc && args[first_arg] && args[first_arg][0] != '\0') {
      quitmsg = args[first_arg];
   }

#ifdef	USE_GTK
   // Confirm before quitting in GTK mode
   if (ui_mode == UI_MODE_GTK && !confirmed && !ui_confirm_quit() ) {
      return false;
   }
#endif	// USE_GTK

   ui_print(ui_active_window_name(), "{bright-cyan}Seeya soon, have a great day!{reset}");

   dict *d = dict_new();
   dict_add(d, "msg.type", "auth");
   dict_add(d, "auth.cmd", "quit");
   dict_add(d, "auth.msg", quitmsg);
   if (d && ws_conn) {
      ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
   }
   dict_free(d);

   // Set the dying flag so main loop with cleanly exit
   dying = true;

   return false;
}

bool cmd_rxvol(int argc, char **args) {
   if (argc < 2 || !args[1]) {
      ui_print(ui_active_window_name(), "* Usage: /rxvol <0-100>");
      return true;
   }
   int val = atoi(args[1]);
   if (val < 0) val = 0;
   if (val > 100) val = 100;
   audio_set_rx_volume(val);

   if (ui_mode == UI_MODE_TUI) {
      // do stuff
   } else if (ui_mode == UI_MODE_GTK) {
#ifdef	USE_GTK
      gtk_range_set_value(GTK_RANGE(rx_vol_slider), val);
#endif
      ui_print(ui_active_window_name(), "* Set rx-vol to %d", val);
   }

   return false;
}

bool cmd_server(int argc, char **args) {
   // With no argument, show the server picker rather than silently
   // reconnecting to the current profile.
   if (argc < 2 || !args || !args[1] || args[1][0] == '\0') {
      show_server_chooser();

      return true;
   }

       const char *server = args[1];

       // Trim trailing whitespace; tab completion adds a space after the name
       char trimmed[64];
       snprintf(trimmed, sizeof(trimmed), "%s", server ? server : "");
       for (char *tp = trimmed + strlen(trimmed); tp > trimmed && isspace( (unsigned char)tp[-1] ); tp--) {
          tp[-1] = '\0';
       }
       server = trimmed;

       if (server && server[0] != '\0') {
         ui_print(ui_active_window_name(), "%s * Changing server profile to %s", get_chat_ts(now), server);
         disconnect_server(server);

         // Set the profile name unconditionally, server_name may be NULL on a
         // fresh start when nothing has connected yet
         free( (char *)server_name );
         server_name = strdup(server);

         if (!server_name) {
            fprintf(stderr, "OOM in parse_chat_input /server\n");

            return true;
         }
         Log(LOG_DEBUG, "gtk.core", "Set server profile to %s by console cmd", server);
         connect_server(server);
      } else {
         ui_print(ui_active_window_name(), "Try /server servername to connect");
         show_server_chooser();
      }

      return false;
   }
