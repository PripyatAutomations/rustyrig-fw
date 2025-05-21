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
