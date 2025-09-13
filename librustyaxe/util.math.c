//
// util.math.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "librustyaxe/config.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include "librustyaxe/util.math.h"

float safe_atof(const char *s) {
    if (!s || !*s) {
       return NAN;
    }

    errno = 0;
    char *end;
    float val = strtof(s, &end);

    if (errno != 0) {
       return NAN;            // overflow/underflow
    }

    while (*end && isspace((unsigned char)*end)) {
       end++;
    }

    if (*end != '\0') {
       return NAN;          // junk at end
    }

    return val;
}

double safe_atod(const char *s) {
    if (!s || !*s) {
       return NAN;
    }
    errno = 0;
    char *end;
    double val = strtod(s, &end);

    if (errno != 0) {
       return NAN;
    }

    while (*end && isspace((unsigned char)*end)) {
       end++;
    }

    if (*end != '\0') {
       return NAN;
    }

    return val;
}

/* Return long, or 0 on error */
long safe_atol(const char *s) {
   if (!s) {
      return 0;
   }

   char *end;
   errno = 0;
   long v = strtol(s, &end, 10);

   if (end == s || errno != 0) {
      return 0;
   }

   return v;
}

/* Return long long, or 0 on error */
long long safe_atoll(const char *s) {
   if (!s) {
      return 0;
   }

   char *end;
   errno = 0;
   long long v = strtoll(s, &end, 10);

   if (end == s || errno != 0) {
      return 0;
   }
   return v;
}
