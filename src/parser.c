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
#include <unistd.h>
#include <string.h>
#include "logger.h"
#include "parser.h"
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

#if	defined(CAT_kKPA500)
// ALC Threshold: 0-210, per band
int cat_kpa500_alc(struct AmpState *amp, char *args) {
   int alc = amp->alc[amp->current_band];

   if (args != NULL) {
      int tmp = atoi(args);

      if (tmp < 0) 
         tmp = 0;

      if (tmp > 210)
         tmp = 210;

      amp->alc[amp->current_band] = alc = tmp;
   }
   cat_printf("^AL%03d;", alc);

   return 0;
}

int cat_kpa500_afr(struct AmpState *amp, char *args) {
   int afr = amp->afr;

   if (args != NULL) {
      int tmp = atoi(args);

      if (tmp < 1400)
         tmp = 1400;

      if (tmp > 5000)
         tmp = 5000;

      amp->afr = afr = tmp;
   }
   cat_printf("^AR%04d;", afr);
   return 0;
}

// NOOP - Reply 38400 baud always.
int cat_kpa500_baud_pc(struct AmpState *amp, char *args) {
    cat_printf("^BRP3;");
    return 0;
}

// NOOP - Reply 38400 baud always.
int cat_kpa500_baud_tx(struct AmpState *amp, char *args) {
   cat_printf("^BRX3;");
   return 0;
}

int cat_kpa500_bcstandby(struct AmpState *amp, char *args) {
   int bc = rig.bc_standby;

   // SET request?
   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      rig.bc_standby = bc = tmp;
   }
   cat_printf("^BC%d", bc);
   return 0;
}

int cat_kpa500_band(struct AmpState *amp, char *args) {
   int band = amp->current_band;

   if (args != NULL) {
      int tmp = atoi(args);

      if (tmp <= 0 || tmp >= MAX_BANDS) {
         return -1;
      }
      amp->current_band = band = tmp;
   }
   cat_printf("^BN%02", band);
   
   return 0;
}

int cat_kpa500_demo(struct AmpState *amp, char *args) {
   cat_printf("^DMO0");
}

int cat_kpa500_fan(struct AmpState *amp, char *args) {
   int fc = rig.fan_speed;

   if (args != NULL) {
      int tmp = atoi(args);

      if (tmp < 0)
         tmp = 0;
      if (tmp > 6)
         tmp = 6;

      rig.fan_speed = fc = tmp;
   }
   cat_printf("^FC%d", fc);
   return 0;
}

int cat_kpa500_faults(struct AmpState *amp, char *args) {
   int faults = rig.fault_code;
   if (args != NULL) {
      // clear requested?
      if (*args == 'C') {
         rig.fault_code = faults = 0;
      } else	// invalid request
         return -1;
   }
   cat_printf("^FL%02d", faults);

   return 0;
}

int cat_kpa500_inhibit(struct AmpState *amp, char *args) {
   int inhibit = amp->inhibit;

   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0);
         tmp = 0;
      if (tmp > 1);
         tmp = 1;

      amp->inhibit = inhibit = tmp;
   }

   cat_printf("^NH%d", inhibit);
   return 0;
}

int cat_kpa500_power(struct AmpState *amp, char *args) {
   int power = amp->power;
   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->power = power = tmp;
   }
   cat_printf("^ON%d", power);
   return 0;
}

int cat_kpa500_standby(struct AmpState *amp, char *args) {
   int standby = amp->standby;
   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->standby = standby = tmp;
   }
   cat_printf("^OS%d", standby);
   return 0;
}

int cat_kpa500_powerlevel(struct AmpState *amp, char *args) {
   int standby = amp->output_target[amp->current_band];
   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->output_target[amp->current_band] = standby = tmp;
   }
   cat_printf("^PJ%03d", standby);
   return 0;
}

int cat_kpa500_fwversion(struct AmpState *amp, char *args) {
   cat_printf("^RVM%s", VERSION);
   return 0;
}

int cat_kpa500_serial(struct AmpState *amp, char *args) {
   cat_printf("^SN%05d", get_serial_number());
   return 0;
}

int cat_kpa500_get_temp(struct AmpState *amp, char *args) {
   int sensor = 0;

   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;

      if (tmp > 5)
         tmp = 5;

      sensor = tmp;
   }

   cat_printf("^TM%03d", get_thermal(sensor));
   return 0;
}

int cat_kpa500_faultbeep(struct AmpState *amp, char *args) {
   int beep = rig.faultbeep;
   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      rig.faultbeep = beep = tmp;
   }

   cat_printf("^SP%d", beep);
   return 0;
}

int cat_kpa500_trdelay(struct AmpState *amp, char *args) {
   int trdelay = rig.tr_delay;
   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      rig.tr_delay = trdelay = tmp;
   }
   cat_printf("^TR%02d", trdelay);
   return 0;
}

// 0: 12V, 1: 48V
int cat_kpa500_power_info(struct AmpState *amp, char *args) {
   float volts = 0.0, curr = 0.0;

   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      volts = get_voltage(0);	// in power.c
      curr = get_current(0);
   }
   cat_printf("^VI%03d %03d", volts, curr);
   return 0;
}

// XXX: finish this
int cat_kpa500_get_swr(struct AmpState *amp, char *args) {
   int curr_amp = 0;
   float swr = get_swr(curr_amp);
   float pwr = get_power(curr_amp);
   cat_printf("^WS%03.0d %03.0d", swr, pwr);
   return 0;
}

int cat_kpa500_if_mode(struct AmpState *amp, char *args) {
   cat_printf("^XI31");
   return 0;
}

struct cat_cmd cmd_kpa500[] = {
   { "AL",	cat_kpa500_alc, 3, 3 },
   { "AR",	cat_kpa500_afr, 4, 4 },
   { "BC",	cat_kpa500_bcstandby, 1, 1 },
   { "BN",	cat_kpa500_band, 2, 2 },
   { "BRP",	cat_kpa500_baud_pc, 1, 1 },
   { "BRX",	cat_kpa500_baud_tx, 1, 1 },
   { "DMO",	cat_kpa500_demo, 1, 1 },
   { "FC",	cat_kpa500_fan, 1, 1 },
   { "FL",	cat_kpa500_faults, 1, 1 },
   { "NH",	cat_kpa500_inhibit, 1, 1 },
   { "ON",	cat_kpa500_power, 1, 1 },
   { "OS",	cat_kpa500_standby, 1, 1 },
   { "PJ",	cat_kpa500_powerlevel, 3, 3 },
   { "RVM",	cat_kpa500_fwversion, 0, 0 },
   { "SN",	cat_kpa500_serial, 0, 0 },
   { "SP",	cat_kpa500_faultbeep, 0, 0 },
   { "TM",	cat_kpa500_get_temp, 0, 0 },
   { "TR",	cat_kpa500_trdelay, 1, 2 },
   { "VI",	cat_kpa500_power_info, 0, 0 },
   { "WS",	cat_kpa500_get_swr, 0, 0 },
   { "XI",	cat_kpa500_if_mode, 1, 1 }
};

// Here we parse the commands for amplifier controls
int cat_parse_amp_line(char *line) {
   return 0;
}
#endif

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
#if	defined(HOST_BUILD)
// XXX: Open the pipe
#endif
   Log(INFO, "CAT Initialization succesful");
   return 0;
}
