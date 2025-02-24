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
#include "state.h"
#include "thermal.h"
#include "power.h"
#include "eeprom.h"
extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

#if	defined(CAT_KPA500)
// ALC Threshold: 0-210, per band
static int32_t cat_kpa500_alc(struct AmpState *amp, char *args) {
   uint32_t alc = amp->alc[amp->current_band];

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) 
         tmp = 0;

      if (tmp > 210)
         tmp = 210;

      amp->alc[amp->current_band] = alc = tmp;
   }
   cat_printf("^AL%03d;", alc);

   return 0;
}

static int32_t cat_kpa500_afr(struct AmpState *amp, char *args) {
   uint32_t afr = amp->afr;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

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
static int32_t cat_kpa500_baud_pc(struct AmpState *amp, char *args) {
    cat_printf("^BRP3;");
    return 0;
}

// NOOP - Reply 38400 baud always.
static int32_t cat_kpa500_baud_tx(struct AmpState *amp, char *args) {
   cat_printf("^BRX3;");
   return 0;
}

static int32_t cat_kpa500_bcstandby(struct AmpState *amp, char *args) {
   uint32_t bc = rig.bc_standby;

   // SET request?
   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      rig.bc_standby = tmp;
      bc = tmp;
   }
   cat_printf("^BC%d", bc);
   return 0;
}

static int32_t cat_kpa500_band(struct AmpState *amp, char *args) {
   uint32_t band = amp->current_band;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp <= 0 || tmp >= MAX_BANDS) {
         return -1;
      }
      amp->current_band = band = tmp;
   }
   cat_printf("^BN%02", band);
   
   return 0;
}

static int32_t cat_kpa500_demo(struct AmpState *amp, char *args) {
   cat_printf("^DMO0");
   return 0;
}

static int32_t cat_kpa500_fan(struct AmpState *amp, char *args) {
   uint32_t fc = rig.fan_speed;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0)
         tmp = 0;
      if (tmp > 6)
         tmp = 6;

      rig.fan_speed = fc = tmp;
   }
   cat_printf("^FC%d", fc);
   return 0;
}

static int32_t cat_kpa500_faults(struct AmpState *amp, char *args) {
   uint32_t faults = rig.fault_code;
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

static int32_t cat_kpa500_inhibit(struct AmpState *amp, char *args) {
   uint32_t inhibit = amp->inhibit;

   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->inhibit = inhibit = tmp;
   }

   cat_printf("^NH%d", inhibit);
   return 0;
}

static int32_t cat_kpa500_power(struct AmpState *amp, char *args) {
   uint32_t power = amp->power;
   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->power = power = tmp;
   }
   cat_printf("^ON%d", power);
   return 0;
}

static int32_t cat_kpa500_standby(struct AmpState *amp, char *args) {
   uint32_t standby = amp->standby;
   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->standby = standby = tmp;
   }
   cat_printf("^OS%d", standby);
   return 0;
}

static int32_t cat_kpa500_powerlevel(struct AmpState *amp, char *args) {
   uint32_t standby = amp->output_target[amp->current_band];
   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      amp->output_target[amp->current_band] = standby = tmp;
   }
   cat_printf("^PJ%03d", standby);
   return 0;
}

static int32_t cat_kpa500_fwversion(struct AmpState *amp, char *args) {
   cat_printf("^RVM%s", VERSION);
   return 0;
}

static int32_t cat_kpa500_serial(struct AmpState *amp, char *args) {
   cat_printf("^SN%05d", get_serial_number());
   return 0;
}

static int32_t cat_kpa500_get_temp(struct AmpState *amp, char *args) {
   uint32_t sensor = 0;

   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;

      if (tmp > 5)
         tmp = 5;

      sensor = tmp;
   }

   cat_printf("^TM%03d", get_thermal(sensor));
   return 0;
}

static int32_t cat_kpa500_faultbeep(struct AmpState *amp, char *args) {
   uint32_t beep = rig.faultbeep;
   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0)
         tmp = 0;
      if (tmp > 1)
         tmp = 1;

      beep = tmp;
      rig.faultbeep = beep;
   }

   cat_printf("^SP%d", beep);
   return 0;
}

static int32_t cat_kpa500_trdelay(struct AmpState *amp, char *args) {
   uint32_t trdelay = rig.tr_delay;
   if (args != NULL) {
      uint32_t tmp = atoi(args);
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
static int32_t cat_kpa500_power_info(struct AmpState *amp, char *args) {
   float volts = 0.0, curr = 0.0;

   if (args != NULL) {
      uint32_t tmp = atoi(args);
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
static int32_t cat_kpa500_get_swr(struct AmpState *amp, char *args) {
   uint32_t curr_amp = 0;
   float swr = get_swr(curr_amp);
   float pwr = get_power(curr_amp);
   cat_printf("^WS%03.0d %03.0d", swr, pwr);
   return 0;
}

static int32_t cat_kpa500_if_mode(struct AmpState *amp, char *args) {
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
int32_t cat_parse_amp_line(char *line) {
   return 0;
}

bool cat_kpa500_init(void) {
   Log(LOG_INFO, "Amplifier CAT control (KPA500 emu) initialized");
   return false;
}

#endif
