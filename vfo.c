//
// vfo.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/*
 * Deal with things related to the VFO, such as reconfiguring DDS(es)
 */
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "inc/logger.h"
#include "inc/state.h"
#include "inc/vfo.h"

rr_vfo_data_t vfos[MAX_VFOS];
rr_vfo_t active_vfo = VFO_A;

static const char vfo_mode_none[] = "NONE";
static const char vfo_mode_cw[] = "CW";
static const char vfo_mode_am[] = "AM";
static const char vfo_mode_lsb[] = "LSB";
static const char vfo_mode_usb[] = "USB";
static const char vfo_mode_dsb[] = "DSB";
static const char vfo_mode_fm[] = "FM";
static const char vfo_mode_dl[] = "D-L";
static const char vfo_mode_du[] = "D-U";
static const char vfo_mode_ft4[] = "FT4";
static const char vfo_mode_ft8[] = "FT8";

// This should be called by CAT to set the backend appropriately
bool set_vfo_frequency(rr_vfo_type_t vfo_type, uint32_t input, float freq) {
   Log(LOG_INFO, "vfo", "Setting VFO (type: %d) input #%d to %f", vfo_type, input, freq);
   // We should call into the backend here to set the frequency
   return true;
}

rr_vfo_t vfo_lookup(const char vfo) {
   rr_vfo_t c_vfo;

   switch(vfo) {
      case 'A':
         c_vfo = VFO_A;
         break;
      case 'B':
         c_vfo = VFO_B;
         break;
      case 'C':
         c_vfo = VFO_C;
         break;
      case 'D':
         c_vfo = VFO_D;
         break;
      case 'E':
         c_vfo = VFO_E;
         break;
      default:
         c_vfo = VFO_NONE;
         break;
   }

   return c_vfo;
}

const char vfo_name(rr_vfo_t vfo) {
   switch(vfo) {
      case VFO_A:
         return 'A';
         break;
      case VFO_B:
         return 'B';
         break;
      case VFO_C:
         return 'C';
         break;
      case VFO_D:
         return 'D';
         break;
      case VFO_E:
         return 'E';
         break;
      case VFO_NONE:
      default:
         return '*';
         break;
   }
   return '*';
}

rr_mode_t vfo_parse_mode(const char *mode) {
   if (strcasecmp(mode, vfo_mode_cw) == 0) {
      return MODE_CW;
   } else if (strcasecmp(mode, vfo_mode_am) == 0) {
      return MODE_AM;
   } else if (strcasecmp(mode, vfo_mode_lsb) == 0) {
      return MODE_LSB;
   } else if (strcasecmp(mode, vfo_mode_usb) == 0) {
      return MODE_USB;
   } else if (strcasecmp(mode, vfo_mode_dsb) == 0) {
      return MODE_DSB;
   } else if (strcasecmp(mode, vfo_mode_fm) == 0) {
      return MODE_FM;
   } else if (strcasecmp(mode, vfo_mode_dl) == 0 || strcasecmp(mode, "dl") == 0) {
      return MODE_DL;
   } else if (strcasecmp(mode, vfo_mode_du) == 0 || strcasecmp(mode, "du") == 0) {
      return MODE_DU;
   } else if (strcasecmp(mode, vfo_mode_ft4) == 0) {
      return MODE_FT4;
   } else if (strcasecmp(mode, vfo_mode_ft8) == 0) {
      return MODE_FT8;
   }
   return MODE_NONE;
}

const char *vfo_mode_name(rr_mode_t mode) {
   const char *rv = NULL;

   switch(mode) {
      case MODE_CW:
         rv = vfo_mode_cw;
         break;
      case MODE_AM:
         rv = vfo_mode_am;
         break;
      case MODE_LSB:
         rv = vfo_mode_lsb;
         break;
      case MODE_USB:
         rv = vfo_mode_usb;
         break;
      case MODE_DSB:
         rv = vfo_mode_dsb;
         break;
      case MODE_FM:
         rv = vfo_mode_fm;
         break;
      case MODE_DL:
         rv = vfo_mode_dl;
         break;
      case MODE_DU:
         rv = vfo_mode_du;
         break;
      case MODE_FT4:
         rv = vfo_mode_ft4;
         break;
      case MODE_FT8:
         rv = vfo_mode_ft8;
         break;
      case MODE_NONE:
      default:
         rv = vfo_mode_none;
         break;
   }

   Log(LOG_DEBUG, "vfo_mode_name", "%d => %s", mode, rv);
   return rv;
}

long parse_freq(const char *str) {
   while (isspace(*str)) str++;

   char *end = NULL;
   double val = strtod(str, &end);

   while (isspace(*end)) end++;

   // If no suffix provided, guess unit
   if (*end == '\0') {
      // Count digits before any decimal point
      const char *dot = strchr(str, '.');
      int digits = dot ? (dot - str) : strlen(str);

      if (digits >= 3 && digits <= 5) {
         return (long)(val * 1e3); // assume kHz
      } else {
         return (long)val;         // assume Hz
      }
   } else if (*end == 'k' || *end == 'K') {
      return (long)(val * 1e3);
   } else if (*end == 'm' || *end == 'M') {
      return (long)(val * 1e6);
   }

   return -1; // invalid format
}

const char *format_freq(long hz, char *buf, size_t len) {
   if (hz >= 1000000) {
      snprintf(buf, len, "%.3f MHz", hz / 1e6);
   } else if (hz >= 1000) {
      snprintf(buf, len, "%.1f kHz", hz / 1e3);
   } else {
      snprintf(buf, len, "%ld Hz", hz);
   }
   return buf;
}
