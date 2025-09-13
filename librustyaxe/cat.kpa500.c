//
// cat.kpa500.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/cat.h"

#if	defined(CAT_KPA500)
// ALC Threshold: 0-210, per band
static int32_t rr_cat_kpa500_alc(struct AmpState *amp, char *args) {
   uint32_t alc = amp->alc[amp->current_band];

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 210) {
         tmp = 210;
      }

      amp->alc[amp->current_band] = alc = tmp;
   }
   rr_cat_printf("^AL%03d;", alc);

   return 0;
}

static int32_t rr_cat_kpa500_afr(struct AmpState *amp, char *args) {
   uint32_t afr = amp->afr;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 1400) {
         tmp = 1400;
      }

      if (tmp > 5000) {
         tmp = 5000;
      }

      amp->afr = afr = tmp;
   }
   rr_cat_printf("^AR%04d;", afr);
   return 0;
}

// NOOP - Reply 38400 baud always.
static int32_t rr_cat_kpa500_baud_pc(struct AmpState *amp, char *args) {
    rr_cat_printf("^BRP3;");
    return 0;
}

// NOOP - Reply 38400 baud always.
static int32_t rr_cat_kpa500_baud_tx(struct AmpState *amp, char *args) {
   rr_cat_printf("^BRX3;");
   return 0;
}

static int32_t rr_cat_kpa500_bcstandby(struct AmpState *amp, char *args) {
   uint32_t bc = rig.bc_standby;

   // SET request?
   if (args != NULL) {
      uint32_t tmp = atoi(args);
      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      rig.bc_standby = tmp;
      bc = tmp;
   }
   rr_cat_printf("^BC%d", bc);
   return 0;
}

static int32_t rr_cat_kpa500_band(struct AmpState *amp, char *args) {
   uint32_t band = amp->current_band;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp <= 0 || tmp >= MAX_BANDS) {
         return -1;
      }
      amp->current_band = band = tmp;
   }
   rr_cat_printf("^BN%02d", band);

   return 0;
}

static int32_t rr_cat_kpa500_demo(struct AmpState *amp, char *args) {
   rr_cat_printf("^DMO0");
   return 0;
}

static int32_t rr_cat_kpa500_fan(struct AmpState *amp, char *args) {
   uint32_t fc = rig.fan_speed;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 6) {
         tmp = 6;
      }

      rig.fan_speed = fc = tmp;
   }
   rr_cat_printf("^FC%d", fc);
   return 0;
}

static int32_t rr_cat_kpa500_faults(struct AmpState *amp, char *args) {
   uint32_t faults = rig.fault_code;
   if (args != NULL) {
      // clear requested?
      if (*args == 'C') {
         rig.fault_code = faults = 0;
      } else {	// invalid request
         return -1;
      }
   }
   rr_cat_printf("^FL%02d", faults);

   return 0;
}

static int32_t rr_cat_kpa500_inhibit(struct AmpState *amp, char *args) {
   bool inhibit = amp->inhibit;

   if (args != NULL) {
      int tmp = atoi(args);
      if (tmp <= 0) {
         inhibit = false;
      }

      if (tmp >= 1) {
         inhibit = true;
      }

      amp->inhibit = inhibit;
   }

   rr_cat_printf("^NH%d", inhibit);
   return 0;
}

static int32_t rr_cat_kpa500_power(struct AmpState *amp, char *args) {
   uint32_t power = amp->power;
   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      amp->power = power = tmp;
   }
   rr_cat_printf("^ON%d", power);
   return 0;
}

static int32_t rr_cat_kpa500_standby(struct AmpState *amp, char *args) {
   uint32_t standby = amp->standby;
   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      amp->standby = standby = tmp;
   }
   rr_cat_printf("^OS%d", standby);
   return 0;
}

static int32_t rr_cat_kpa500_powerlevel(struct AmpState *amp, char *args) {
   uint32_t standby = amp->output_target[amp->current_band];
   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      amp->output_target[amp->current_band] = standby = tmp;
   }
   rr_cat_printf("^PJ%03d", standby);
   return 0;
}

static int32_t rr_cat_kpa500_fwversion(struct AmpState *amp, char *args) {
   rr_cat_printf("^RVM%s", VERSION);
   return 0;
}

static int32_t rr_cat_kpa500_serial(struct AmpState *amp, char *args) {
#if	defined(USE_EEPROM)
   rr_cat_printf("^SN%05d", get_serial_number());
#else
   const char *s = cfg_get("device.serial");
   if (s) {
      int serial = atoi(s);
      rr_cat_printf("^SN%05d", serial);
   }
#endif
   return 0;
}

static int32_t rr_cat_kpa500_get_temp(struct AmpState *amp, char *args) {
   uint32_t sensor = 0;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 5) {
         tmp = 5;
      }

      sensor = tmp;
   }

   rr_cat_printf("^TM%03d", get_thermal(sensor));
   return 0;
}

static int32_t rr_cat_kpa500_faultbeep(struct AmpState *amp, char *args) {
   uint32_t beep = rig.faultbeep;
   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      beep = tmp;
      rig.faultbeep = beep;
   }

   rr_cat_printf("^SP%d", beep);
   return 0;
}

static int32_t rr_cat_kpa500_trdelay(struct AmpState *amp, char *args) {
   uint32_t trdelay = rig.tr_delay;
   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      rig.tr_delay = trdelay = tmp;
   }
   rr_cat_printf("^TR%02d", trdelay);
   return 0;
}

// 0: 12V, 1: 48V
static int32_t rr_cat_kpa500_power_info(struct AmpState *amp, char *args) {
   float volts = 0.0, curr = 0.0;

   if (args != NULL) {
      uint32_t tmp = atoi(args);

      if (tmp < 0) {
         tmp = 0;
      }

      if (tmp > 1) {
         tmp = 1;
      }

      volts = get_voltage(0);	// in power.c
      curr = get_current(0);
   }
   rr_cat_printf("^VI%03d %03d", volts, curr);
   return 0;
}

// XXX: finish this
static int32_t rr_cat_kpa500_get_swr(struct AmpState *amp, char *args) {
   uint32_t curr_amp = 0;
   float swr = get_swr(curr_amp);
   float pwr = get_power(curr_amp);
   rr_cat_printf("^WS%03.0d %03.0d", swr, pwr);
   return 0;
}

static int32_t rr_cat_kpa500_if_mode(struct AmpState *amp, char *args) {
   rr_cat_printf("^XI31");
   return 0;
}

struct rr_cat_cmd cmd_kpa500[] = {
   { "AL",	rr_cat_kpa500_alc, 3, 3 },
   { "AR",	rr_cat_kpa500_afr, 4, 4 },
   { "BC",	rr_cat_kpa500_bcstandby, 1, 1 },
   { "BN",	rr_cat_kpa500_band, 2, 2 },
   { "BRP",	rr_cat_kpa500_baud_pc, 1, 1 },
   { "BRX",	rr_cat_kpa500_baud_tx, 1, 1 },
   { "DMO",	rr_cat_kpa500_demo, 1, 1 },
   { "FC",	rr_cat_kpa500_fan, 1, 1 },
   { "FL",	rr_cat_kpa500_faults, 1, 1 },
   { "NH",	rr_cat_kpa500_inhibit, 1, 1 },
   { "ON",	rr_cat_kpa500_power, 1, 1 },
   { "OS",	rr_cat_kpa500_standby, 1, 1 },
   { "PJ",	rr_cat_kpa500_powerlevel, 3, 3 },
   { "RVM",	rr_cat_kpa500_fwversion, 0, 0 },
   { "SN",	rr_cat_kpa500_serial, 0, 0 },
   { "SP",	rr_cat_kpa500_faultbeep, 0, 0 },
   { "TM",	rr_cat_kpa500_get_temp, 0, 0 },
   { "TR",	rr_cat_kpa500_trdelay, 1, 2 },
   { "VI",	rr_cat_kpa500_power_info, 0, 0 },
   { "WS",	rr_cat_kpa500_get_swr, 0, 0 },
   { "XI",	rr_cat_kpa500_if_mode, 1, 1 }
};

// Here we parse the commands for amplifier controls
int32_t rr_cat_parse_amp_line(char *line) {
   return 0;
}

bool rr_cat_kpa500_init(void) {
   Log(LOG_INFO, "kpa500", "Amplifier CAT control (KPA500 emu) initialized");
   return false;
}

#endif
