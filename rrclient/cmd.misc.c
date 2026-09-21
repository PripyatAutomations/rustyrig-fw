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
extern bool ui_confirm_quit(void);
void rrclient_print_callsign_line(const char *line);

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
      ui_print(NULL, "Usage: /qrz CALLSIGN [NOCACHE]");
      return true;
   }

   for (const unsigned char *p = (const unsigned char *)args[1]; *p; p++) {
      if (!isalnum(*p) && *p != '-' && *p != '/' && *p != '.') {
         ui_print(NULL, "Invalid callsign: %s", args[1]);
         return true;
      }
   }

   char *program = cfg_get_path("callsign-lookup:path");
   const char *qrz_user = cfg_get("callsign-lookup:qrz-username");
   const char *qrz_pass = cfg_get("callsign-lookup:qrz-password");
   if (!qrz_user || !*qrz_user || !qrz_pass || !*qrz_pass) {
      if (!ws_conn) {
         ui_print(NULL, "Callsign lookup is not configured locally and the server is disconnected");
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
      ui_print(NULL, "Asking the server to look up %s", args[1]);
      free(program);
      return false;
   }
   if (!program || !*program || !config_file || !*config_file) {
      ui_print(NULL, "Callsign lookup is not configured locally");
      free(program);
      return true;
   }

   bool lookup_ok = run_local_lookup(program, config_file, args[1], false, no_cache);
   free(program);
   if (!lookup_ok) {
      ui_print(NULL, "Callsign lookup failed for %s", args[1]);
      return true;
   }
   return false;
}

bool cmd_grid(int argc, char **args) {
   if (argc != 2 || !args[1] || !args[1][0]) {
      ui_print(NULL, "Usage: /grid GRID|LAT,LON");
      return true;
   }
   for (const unsigned char *p = (const unsigned char *)args[1]; *p; p++) {
      if (!isalnum(*p) && *p != '-' && *p != '.' && *p != ',' &&
          *p != '+' && *p != ' ') {
         ui_print(NULL, "Invalid grid or coordinates: %s", args[1]);
         return true;
      }
   }

   char *program = cfg_get_path("callsign-lookup:path");
   if (!program || !*program || !config_file || !*config_file) {
      if (!ws_conn) {
      ui_print(NULL, "Callsign lookup is not configured locally and the server is disconnected");
         free(program);
         return true;
      }
      dict *request = dict_new();
      dict_add(request, "msg.type", "talk");
      dict_add(request, "talk.cmd", "grid");
      dict_add(request, "talk.data", args[1]);
      ws_send_dict(NULL, ws_conn, request, WEBSOCKET_OP_TEXT);
      dict_free(request);
      ui_print(NULL, "Asking the server for grid information for %s", args[1]);
      free(program);
      return false;
   }

   bool lookup_ok = run_local_lookup(program, config_file, args[1], true, false);
   free(program);
   if (!lookup_ok) {
      ui_print(NULL, "Grid lookup failed for %s", args[1]);
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
      ui_print(NULL, "{bright-green}%s{reset}", line);
      return;
   }

   const char *colon = strchr(line, ':');
   if (colon && colon != line) {
      int label_len = (int)(colon - line);
      ui_print(NULL, "  {bright-cyan}%.*s:{reset}%s", label_len, line, colon + 1);
      return;
   }

   ui_print(NULL, "%s", line);
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

   ui_print(NULL, "{bright-cyan}Seeya soon, have a great day!{reset}");

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
      ui_print(NULL, "* Usage: /rxvol <0-100>");
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
      ui_print(NULL, "* Set rx-vol to %d", val);
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
         ui_print(NULL, "%s * Changing server profile to %s", get_chat_ts(now), server);
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
         ui_print(NULL, "Try /server servername to connect");
         show_server_chooser();
      }

      return false;
   }
