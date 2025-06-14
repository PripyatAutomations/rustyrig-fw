//
// common/json.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "build_config.h"
#include "common/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "../../ext/libmongoose/mongoose.h"
#include "common/dict.h"
#include "rustyrig/cat.h"
#include "rustyrig/codec.h"
#include "rustyrig/eeprom.h"
#include "rustyrig/i2c.h"
#include "common/logger.h"
#include "common/posix.h"
#include "rustyrig/state.h"
#include "rustyrig/ws.h"

/*
 * Here we parse json into a dict
 */

static const char *skip_ws(const char *s) {
   while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') s++;
   return s;
}

static const char *parse_string(const char *s, char *out, size_t outlen) {
   if (*s != '"') return NULL;
   s++;
   size_t i = 0;
   while (*s && *s != '"') {
      if (*s == '\\') s++; // skip escaped char
      if (i < outlen - 1) out[i++] = *s++;
      else return NULL;
   }
   out[i] = 0;
   return (*s == '"') ? s + 1 : NULL;
}

static const char *parse_value(const char *s, char *path, dict *dptr);

static const char *parse_object(const char *s, char *path, dict *dptr) {
   if (*s != '{') return NULL;
   s = skip_ws(s + 1);
   while (*s && *s != '}') {
      char key[128], subpath[256];
      s = skip_ws(s);
      s = parse_string(s, key, sizeof key);
      if (!s) return NULL;
      s = skip_ws(s);
      if (*s++ != ':') return NULL;
      snprintf(subpath, sizeof subpath, "%s.%s", path, key);
      s = skip_ws(s);
      s = parse_value(s, subpath, dptr);
      if (!s) return NULL;
      s = skip_ws(s);
      if (*s == ',') s = skip_ws(s + 1);
   }
   return (*s == '}') ? s + 1 : NULL;
}

static const char *parse_array(const char *s, char *path, dict *dptr) {
   if (*s != '[') return NULL;
   s = skip_ws(s + 1);
   int idx = 0;
   while (*s && *s != ']') {
      char subpath[256];
      snprintf(subpath, sizeof subpath, "%s[%d]", path, idx++);
      s = parse_value(s, subpath, dptr);
      if (!s) return NULL;
      s = skip_ws(s);
      if (*s == ',') s = skip_ws(s + 1);
   }
   return (*s == ']') ? s + 1 : NULL;
}

static const char *parse_primitive(const char *s, char *out, size_t outlen) {
   size_t i = 0;
   while (*s && !strchr(",}] \t\r\n", *s)) {
      if (i < outlen - 1) out[i++] = *s++;
      else return NULL;
   }
   out[i] = 0;
   return s;
}

static const char *parse_value(const char *s, char *path, dict *dptr) {
   s = skip_ws(s);
   if (*s == '"') {
      char val[256];
      s = parse_string(s, val, sizeof val);
      if (!s) return NULL;
      dict_add(dptr, path, val);
      return s;
   } else if (*s == '{') {
      return parse_object(s, path, dptr);
   } else if (*s == '[') {
      return parse_array(s, path, dptr);
   } else {
      char val[256];
      s = parse_primitive(s, val, sizeof val);
      if (!s) return NULL;
      dict_add(dptr, path, val);
      return s;
   }
}

void parse_and_flatten(const char *json, dict *dptr) {
   char path[4] = "$";
   parse_value(json, path, dptr);
}
