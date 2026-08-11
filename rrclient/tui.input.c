//
// rrclient/commands.c: Chat stuff that isn't GUI dependent
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

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
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/command.h>
#include <rrclient/ui.h>
#include <ev.h>

extern bool dying;
extern time_t now;

#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
extern bool ws_connected;
extern bool rrclient_send_chat(const char *data);
#endif

extern client_cmd_t client_cmds[];       // from commands.c

bool tui_input_cb(const char *input) {
   if (client_cmds[0].cmd == NULL || !input || !*input) {
      return true;
   }
   // Make a mutable copy
   char buf[TUI_INPUTLEN];
   strlcpy(buf, input, sizeof(buf));
   buf[sizeof(buf) - 1] = '\0';

   // Tokenize into argc/args
   int argc = 0;
   char *args[64];    // max 64 tokens
   char *tok = strtok(buf, " \t");
   while (tok && argc < (int)(sizeof(args) / sizeof(args[0]) ) ) {
      args[argc++] = tok;
      tok = strtok(NULL, " \t");
   }

   if (argc == 0) {
      return true;
   }

   if (args[0][0] == '/') {
      for (client_cmd_t *c = client_cmds ; c->cmd && c->cb ; c++) {
         if (strcasecmp(c->cmd, args[0]) == 0) {
            if (c->cb) {
               c->cb(argc, args);

               return false;
            }
         }
      }

      ui_print(NULL,
         "{red}*** {bright-red}Huh?! What you say?! I dont understand '%s' {red}***{reset}.",
         args[0]);

      return true;
   }
   // Send to active window target
#if defined(USE_MONGOOSE)

   if (!ws_connected) {
      ui_print(NULL, "{red}*** Not connected to server ***{reset}");

      return false;
   }
#endif
   char fullmsg[502];
   memset( fullmsg, 0, sizeof(fullmsg) );
   size_t pos = 0;

   for (int i = 0 ; i < argc ; i++) {
      int n = snprintf(fullmsg + pos, sizeof(fullmsg) - pos, "%s%s", (i > 0 ? " " : ""),
         args[i] ? args[i] : "");

      if (n < 0 || (size_t)n >= sizeof(fullmsg) - pos) {
         break;
      }
      pos += n;
   }

   rrclient_send_chat(fullmsg);

   return false;
}
