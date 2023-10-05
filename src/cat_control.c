/*
 * This is the CAT parser.
 *
 * Here we parse commands for the various functions of the radio.
 *
 * Since this module is designed to be used either with an existing rig or
 * eventually as a stand-alone transceiver, we support two protocols for control
 *
 * CAT_KPA500: Electraft KPA-500 amplifier control protocol
 * CAT_YAESU: Yaesu FT-891/991A rig control protocol
 * You can enable both protocols or just one.
 */
#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "cat.h"
#include "cat_control.h"
#include "state.h"
#include "thermal.h"
#include "power.h"
#include "eeprom.h"

extern struct GlobalState rig;		// in main.c

int cat_printf(char *str, ...) {
   va_list ap;
   va_start(ap, str);

   // XXX: Print it to a buffer and send to serial...

   va_end(ap);
   return 0;
}

// Here we parse commands for the main rig
int cat_parse_line_real(char *line) {
   return 0;
}

// Here we decide which parser to use
int cat_parse_line(char *line) {
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

      // is comand line complete? if not, return -2 to say "Not yet"
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
      return cat_parse_amp_line(line + 1);
   } else
#endif
   {
      // XXX: Validate the CAT command syntax
      return cat_parse_line_real(line);
   }

   return 0;
}

// Initialize CAT control
int cat_init(void) {
#if	defined(HOST_POSIX)
// XXX: Open the pipe
#endif
   Log(LOG_INFO, "CAT Initialization succesful");
   return 0;
}
