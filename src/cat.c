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
#include "state.h"
#include "thermal.h"
#include "power.h"
#include "eeprom.h"
#include "vfo.h"
#include "cat.h"
#include "cat_kpa500.h"
#include "cat_yaesu.h"
extern struct GlobalState rig;  // Global state
extern bool dying;		// in main.c

int32_t cat_printf(char *str, ...) {
   va_list ap;
   va_start(ap, str);

   // XXX: Print it to a buffer and send to serial...

   va_end(ap);
   return 0;
}

// Here we parse commands for the main rig
int32_t cat_parse_line_real(char *line) {
   return 0;
}

// Here we decide which parser to use
int32_t cat_parse_line(char *line) {
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
int32_t cat_init(void) {
#if	defined(HOST_POSIX)
// XXX: Open the pipe(s)
// KPA500 amplifier control
// Yaesu-style rig control
#if	defined(CAT_YAESU)
   cat_yaesu_init();
#endif
#if	defined(CAT_KPA500)
   cat_kpa500_init();
#endif
#endif
   Log(LOG_INFO, "CAT Initialization succesful");
   return 0;
}
