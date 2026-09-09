//
// librustyaxe/cat.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * This is the CAT parser. We use the io abstraction in io.c
 *
 * Here we parse commands for the various functions of the radio.
 *
 * Amplifier and rig control are split up into two CAT interfaces. CAT_KPA500:
 * Electraft KPA-500 amplifier control protocol CAT_YAESU: Yaesu FT-891/991A rig control
 * protocol You can enable both protocols or just one, depending on your build
 *
 * Since the KPA500 commands have a prefix character, we can be flexible about how it is
 * connected. A single pipe/serial port/socket can be used, for CAT, if desired.
 *
 * We have two entry points here
 * - rr_cat_parse_line(): Parses a line from io (sock|net|pipe)
 * - rr_cat_parse_ws(): Parses a websocket message containing a CAT command
 *
 * We respond via rr_cat_reply() with enum rr_cat_req_type as first arg
 */
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/cat.h>

static CATcmd *cat_commands = NULL;

static CATcmd *cat_find_or_create_cmd(const char *cmd) {
   CATcmd *c = cat_commands;

   while (c) {
      if (strcmp(c->cmd, cmd) == 0) {
         return c;
      }
      c = c->next;
   }
   CATcmd *newc = calloc( 1, sizeof(*newc) );

   if (!newc) {
      return NULL;
   }
   newc->cmd = strdup(cmd);
   newc->callbacks = NULL;
   newc->next = cat_commands;
   cat_commands = newc;

   return newc;
}

bool cat_register_callback(const char *cmd, CATCallback cb) {
   CATcmd *c = cat_find_or_create_cmd(cmd);

   if (!c) {
      return false;
   }
   CATCallbackNode *n = calloc( 1, sizeof(*n) );

   if (!n) {
      return false;
   }
   n->cb = cb;

   // append at end
   CATCallbackNode **p = &c->callbacks;
   while (*p) {
      p = &(*p)->next;
   }
   *p = n;

   return true;
}

bool cat_invoke_callbacks(const char *cmd, const char *args) {
   CATcmd *c = cat_commands;
   while (c) {
      if (strcmp(c->cmd, cmd) == 0) {
         CATCallbackNode *n = c->callbacks;
         while (n) {
            n->cb(args);
            n = n->next;
         }
         return true;
      }
      c = c->next;
   }
   return false;  // unknown command
}

bool cat_register_builtin_array(const CATBuiltin *arr) {
   if (!arr) {
      return false;
   }

   for (const CATBuiltin *p = arr ; p->cmd != NULL ; p++) {
      if (p->cb) {
         if ( !cat_register_callback(p->cmd, p->cb) ) {
            return false;
         }
      }
   }

   return true;
}


// Initialize CAT control
int32_t rr_cat_init(void) {
   Log(LOG_INFO, "cat", "Initializing CAT interfaces");

#if     defined(HOST_POSIX)
// PTY interface (./dev/ttyCAT0): external software (hamlib, rigctl, etc)
// opens the slave as a serial port and talks to our CAT parsers.
// No-op unless cat.pty.enable is true in the config.
   cat_pty_init();

// XXX: Open the pipe(s)

#if     defined(CAT_YAESU)              // Yaesu-style rig control
   rr_cat_yaesu_init();
#endif
#if     defined(CAT_KPA500)             // KPA500 amplifier control

   rr_cat_kpa500_init();
#endif
#endif
   Log(LOG_INFO, "cat", "CAT Initialization succesful");

   return 0;
}

int32_t rr_cat_printf(const char *str, ...) {
   va_list ap;
   va_start(ap, str);

   // Send the reply out the CAT PTY, if it's up
   if (cat_pty_active()) {
      char buf[512];
      int len = vsnprintf(buf, sizeof(buf), str, ap);
      if (len > 0 && (size_t)len < sizeof(buf)) {
         (void)!write(cat_pty_fd(), buf, len);
      }
   }

   va_end(ap);

   return 0;
}

// Here we parse commands for the main rig.
// The Yaesu protocol puts the 2-letter verb first, then any arguments
// (e.g. "FA004250000;" or "TX0;").  Lines may arrive with or without the
// terminating ';' depending on the input source.
int32_t rr_cat_parse_line_real(char *line) {
   if (!line || strlen(line) < 2) {
      return -1;
   }

   char verb[3] = { line[0], line[1], 0 };
   char *args = line + 2;

   while (*args == ' ') {
      args++;
   }

   // Registered dynamic callbacks first (they may override the built-ins)
   if (cat_invoke_callbacks(verb, args) ) {
      return 0;
   }

#if     defined(CAT_YAESU)
   for (CATcmdTable *p = rr_cat_yaesu_commands ; p->command != NULL ; p++) {
      if (strcmp(p->command, verb) == 0) {
         if (p->rr_cat_yaesu_r) {
            Log(LOG_DEBUG, "cat", "CAT cmd %s args: %s", verb, (args[0] ? args : "(none)") );
            p->rr_cat_yaesu_r(args);
         } else {
            // NB: an empty (NULL) handler that's a QUERY leaves the client
            // waiting for a response that never comes (WSJT-X hangs and
            // drops the connection). Implement the handler or answer here.
            Log(LOG_WARN, "cat", "Unimplemented CAT command: %s (args: %s) - no response sent!",
               verb, (args[0] ? args : "(none)") );
         }
         return 0;
      }
   }
#endif

   Log(LOG_WARN, "cat", "Unknown CAT command: %s (args: %s)", verb, (args[0] ? args : "(none)") );
   return -1;
}

// Here we decide which parser to use
int32_t rr_cat_parse_line(char *line) {
   if ( line == NULL || line[0] == '\0' ) {
      return -1;
   }

   // Scrub trailing line endings, the ';' terminator and any trailing spaces
   size_t len = strlen(line);

   while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' ||
                      line[len - 1] == ';' || line[len - 1] == ' ') ) {
      line[--len] = '\0';
   }

   if (len == 0) {
      return -1;
   }

#if     defined(CAT_KPA500)

   // is command for amp?
   if (*line == '^') {
      return rr_cat_parse_amp_line(line + 1);
   }
#endif
#if     defined(CAT_YAESU)
   return rr_cat_parse_line_real(line);
#endif
   return 0;
}
