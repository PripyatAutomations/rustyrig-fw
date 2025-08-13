//
// cat.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
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
 * Amplifier and rig control are split up into two CAT interfaces.
 *   CAT_KPA500: Electraft KPA-500 amplifier control protocol
 *   CAT_YAESU: Yaesu FT-891/991A rig control protocol
 * You can enable both protocols or just one, depending on your build
 *
 * Since the KPA500 commands have a prefix character, we can be flexible about
 * how it is connected. A single pipe/serial port/socket can be used, for CAT,
 * if desired.
 *
 * We have two entry points here
 *	- rr_cat_parse_line(): Parses a line from io (sock|net|pipe)
 *	- rr_cat_parse_ws(): Parses a websocket message containing a CAT command
 *
 * We respond via rr_cat_reply() with enum rr_cat_req_type as first arg
 */
#include "build_config.h"
#include "common/config.h"
#include "../ext/libmongoose/mongoose.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "common/logger.h"
#include "common/cat.h"

// Initialize CAT control
int32_t rr_cat_init(void) {
   Log(LOG_INFO, "cat", "Initializing CAT interfaces");

#if	defined(HOST_POSIX)
// XXX: Open the pipe(s)

#if	defined(CAT_YAESU)		// Yaesu-style rig control
   rr_cat_yaesu_init();
#endif
#if	defined(CAT_KPA500)		// KPA500 amplifier control

   rr_cat_kpa500_init();
#endif
#endif
   Log(LOG_INFO, "cat", "CAT Initialization succesful");
   return 0;
}

int32_t rr_cat_printf(char *str, ...) {
   va_list ap;
   va_start(ap, str);

   // XXX: Print it to a buffer and send to serial...

   va_end(ap);
   return 0;
}

// Here we parse commands for the main rig
int32_t rr_cat_parse_line_real(char *line) {
   return 0;
}

// Here we decide which parser to use
int32_t rr_cat_parse_line(char *line) {
   size_t line_len = -1;
   char *endp = NULL;

   // If passed empty string, stop immediately and let the caller know...
   if (line == NULL || (line_len = strlen(line) <= 0)) {
      return -1;
   } else {
      char *p = endp = line + line_len;

      while (p < line) {
         // Scrub out line endings
         if (*p == '\r' || *p == '\n') {
            *p = '\0';
         }
         p--;
      }

      // validate the pointers, just in case...
      if (endp <= line || (endp > (line + line_len))) {
         // Line is invalid, stop touching it and let the caller know
         return -1;
      }

      // is command line complete? if not, return -2 to say "Not yet"
      if (*endp == ';') {
         *endp = '\0';
         endp--;
      } else {
         return -2;
      }
   }

#if	defined(CAT_KPA500)
   // is command for amp?
   if (*line == '^') {
      return rr_cat_parse_amp_line(line + 1);
   } else
#endif
#if	defined(CAT_YAESU)
   {
      // XXX: Validate the CAT command syntax
      return rr_cat_parse_line_real(line);
   }
#endif
   return 0;
}

#if	defined(FEATURE_HTTP)
bool rr_cat_parse_ws(rr_cat_req_type reqtype, struct mg_ws_message *msg) {
    if (reqtype != REQ_WS || msg == NULL) {
        return false;
    }

    Log(LOG_CRAZY, "cat.ws", "parsing %d bytes from ws: |%.*s|", msg->data.len, msg->data.len, msg->data.buf);

    // Extract "cmd" and "val" from JSON
#if	0
    const char *cmd_str = mg_json_get_str(msg->data, "$.cat.cmd");
    const char *val_str = mg_json_get_str(msg->data, "$.cat.val");
    Log(LOG_DEBUG, "cat.ws", "cmd: %s, val: %s", cmd_str, val_str);

    if (cmd_str && val_str) {
        // Copy cmd to a fixed-size buffer and null-terminate
        char cmd[16] = {0};
        strncpy(cmd, cmd_str, sizeof(cmd) - 1);

        // Convert val to an integer
        int val = atoi(val_str);
        Log(LOG_DEBUG, "cat.ws", "got cmd: %s", cmd);
    }

cleanup:
    free((void *)cmd_str);
    free((void *)val_str);
#endif	// 0
    return false;
}

#endif
