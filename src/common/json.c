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
#include "common/cat.h"
#include "common/logger.h"
#include "common/posix.h"
#include "common/json.h"
#include "rrserver/codec.h"
#include "rrserver/eeprom.h"
#include "rrserver/i2c.h"
#include "rrserver/state.h"
#include "rrserver/ws.h"

/*
 * Here we parse json into a dict
 */

static const char *skip_ws(const char *s) {
   while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') {
      s++;
   }

   return s;
}

static const char *json_parse_str(const char *s, char *out, size_t outlen) {
   if (*s != '"') {
      return NULL;
   }
   s++;
   size_t i = 0;

   while (*s && *s != '"') {
      if (*s == '\\') {
         s++; // skip escaped char
      }

      if (i < outlen - 1) {
         out[i++] = *s++;
      }
      else return NULL;
   }
   out[i] = 0;
   return ((*s == '"') ? s + 1 : NULL);
}

static const char *json_parse_value(const char *s, char *path, dict *dptr);

static const char *json_parse_obj(const char *s, char *path, dict *dptr) {
   if (*s != '{') {
      return NULL;
   }

   s = skip_ws(s + 1);

   while (*s && *s != '}') {
      char key[128], subpath[256];
      s = skip_ws(s);

      s = json_parse_str(s, key, sizeof key);
      if (!s) {
         return NULL;
      }

      s = skip_ws(s);
      if (*s++ != ':') {
         return NULL;
      }
      snprintf(subpath, sizeof subpath, "%s.%s", path, key);

      s = skip_ws(s);
      s = json_parse_value(s, subpath, dptr);
      if (!s) {
         return NULL;
      }

      s = skip_ws(s);
      if (*s == ',') {
         s = skip_ws(s + 1);
      }
   }
   return (*s == '}') ? s + 1 : NULL;
}

static const char *json_parse_array(const char *s, char *path, dict *dptr) {
   if (*s != '[') {
      return NULL;
   }

   s = skip_ws(s + 1);
   int idx = 0;
   while (*s && *s != ']') {
      char subpath[256];
      snprintf(subpath, sizeof subpath, "%s[%d]", path, idx++);
      s = json_parse_value(s, subpath, dptr);
      if (!s) {
         return NULL;
      }

      s = skip_ws(s);
      if (*s == ',') {
         s = skip_ws(s + 1);
      }
   }
   return (*s == ']') ? s + 1 : NULL;
}

static const char *json_parse_primitive(const char *s, char *out, size_t outlen) {
   size_t i = 0;
   while (*s && !strchr(",}] \t\r\n", *s)) {
      if (i < outlen - 1) {
         out[i++] = *s++;
      } else {
         return NULL;
      }
   }
   out[i] = 0;
   return s;
}

static const char *json_parse_value(const char *s, char *path, dict *dptr) {
   s = skip_ws(s);
   if (*s == '"') {
      char val[256];
      s = json_parse_str(s, val, sizeof val);
      if (!s) {
         return NULL;
      }
      dict_add(dptr, path, val);
      return s;
   } else if (*s == '{') {
      return json_parse_obj(s, path, dptr);
   } else if (*s == '[') {
      return json_parse_array(s, path, dptr);
   } else {
      char val[256];
      s = json_parse_primitive(s, val, sizeof val);
      if (!s) {
         return NULL;
      }
      dict_add(dptr, path, val);
      return s;
   }
}

void json_parse_and_flatten(const char *json, dict *dptr) {
   char path[4] = "$";
   json_parse_value(json, path, dptr);
}

/////////////////////////////////

// escape JSON string (returns malloc'd buffer with quotes included)
char *json_escape(const char *s) {
   if (!s) {
      return strdup("\"\"");
   }

   size_t len = strlen(s);
   // worst case every char becomes \uXXXX (6 bytes) + quotes
   char *out = malloc(len * 6 + 3);

   if (!out) {
      fprintf(stderr, "OOM in json_escape\n");
      return NULL;
   }
   char *p = out;
   *p++ = '"';

   for (size_t i = 0; i < len; i++) {
      unsigned char c = (unsigned char)s[i];
      switch (c) {
         case '\"': {
            *p++='\\';
            *p++='\"';
            break;
         }
         case '\\': {
            *p++='\\';
            *p++='\\';
            break;
         }
         case '\b': {
            *p++='\\';
            *p++='b';
            break;
         }
         case '\f': {
            *p++='\\';
            *p++='f';
            break;
         }
         case '\n': {
            *p++='\\';
            *p++='n';
            break;
         }
         case '\r': {
            *p++='\\';
            *p++='r'; 
            break;
         }
         case '\t': {
            *p++='\\';
            *p++='t';
            break;
         }
         default:
            if (c < 0x20) {
               p += sprintf(p, "\\u%04x", c);
            } else {
               *p++ = c;
            }
      }
   }
   *p++ = '"';
   *p = '\0';
   return out;
}

static json_node *json_make_node(const char *key) {
   json_node *n = calloc(1, sizeof(*n));
   if (!n) {
      fprintf(stderr, "OOM in json_make_node\n");
      return NULL;
   }
   n->key = strdup(key);
   if (!n->key) {
      fprintf(stderr, "OOM in json_make_node strdup\n");
      free(n);
      return NULL;
   }
   return n;
}

static json_node *find_child(json_node *parent, const char *key) {
   for (json_node *c = parent->child; c; c = c->next) {
      if (strcmp(c->key, key) == 0) {
         return c;
      }
   }
   json_node *n = json_make_node(key);
   if (!n) {
      return NULL;
   }
   n->next = parent->child;
   parent->child = n;
   return n;
}

static void json_insert(json_node *root, const char *fullkey, const char *val) {
   char *tmp = strdup(fullkey);
   if (!tmp) {
      fprintf(stderr, "OOM in json_insert\n");
       return;
   }

   char *tok = strtok(tmp, ".");
   json_node *cur = root;

   while (tok) {
      cur = find_child(cur, tok);
      tok = strtok(NULL, ".");
   }
   if (cur->value) {
      free(cur->value);
   }
   cur->value = strdup(val ? val : "UNDEF");

   if (!cur->value) {
      // XXX: deal with this fault
      fprintf(stderr, "OOM in json_insert strdup\n");
   }

   free(tmp);
}

// ---- string builder ----
typedef struct {
   char *buf;
   size_t len, cap;
} sbuf;

static void sbuf_init(sbuf *b) {
   b->cap = 256;
   b->len = 0;
   b->buf = malloc(b->cap);
   b->buf[0] = 0;
}

static void sbuf_putc(sbuf *b, char c) {
   if (b->len + 2 > b->cap) {
      b->cap *= 2;
      b->buf = realloc(b->buf, b->cap);
   }
   b->buf[b->len++] = c;
   b->buf[b->len] = 0;
}

static void sbuf_puts(sbuf *b, const char *s) {
   size_t slen = strlen(s);

   if (b->len + slen + 1 > b->cap) {
      while (b->len + slen + 1 > b->cap) {
         b->cap *= 2;
      }
      b->buf = realloc(b->buf, b->cap);
   }
   memcpy(b->buf + b->len, s, slen);
   b->len += slen;
   b->buf[b->len] = 0;
}

static void dump_json(json_node *n, sbuf *out) {
   sbuf_putc(out, '{');
   for (json_node *c = n->child; c; c = c->next) {
      sbuf_putc(out, '"');
      sbuf_puts(out, c->key);
      sbuf_puts(out, "\":");

      if (c->value && !c->child) {
         char *esc = json_escape(c->value);
         sbuf_puts(out, esc);
         free(esc);
      } else {
         dump_json(c, out);
      }

      if (c->next) {
         sbuf_putc(out, ',');
      }
   }
   sbuf_putc(out, '}');
}

static void free_json(json_node *n) {
   for (json_node *c = n->child; c;) {
      json_node *next = c->next;
      free_json(c);
      c = next;
   }
   free(n->key);
   free(n->value);
   free(n);
}

// === public ===
char *dict2json(dict *d) {
   const char *key;
   char *val;
   int rank = 0;
   json_node root = {0};

   while ((rank = dict_enumerate(d, rank, &key, &val)) >= 0) {
      json_insert(&root, key, val);
   }

   sbuf out;
   sbuf_init(&out);
   dump_json(&root, &out);
   free_json(root.child);

   return out.buf;
}

// worker that takes a va_list
void dict_import_va(dict *d, int first_type, va_list ap) {
   int type = first_type;

   while (type != VAL_END) {
      const char *key = va_arg(ap, const char *);

      switch (type) {
         case VAL_STR: {
            const char *val = va_arg(ap, const char *);
            dict_add(d, key, (char *)val);
            break;
         }
         case VAL_INT: {
            int val = va_arg(ap, int);
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", val);
            dict_add(d, key, buf);
            break;
         }
         case VAL_LONG: {
            long val = va_arg(ap, long);
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", val);
            dict_add(d, key, buf);
            break;
         }
         case VAL_FLOAT: {
            double v = va_arg(ap, double);  // floats promote to double
            float val = (float)v;
            char buf[32];
            snprintf(buf, sizeof(buf), "%f", val);
            dict_add(d, key, buf);
            break;
         }

         case VAL_DOUBLE: {
            double val = va_arg(ap, double);
            char buf[64];
            snprintf(buf, sizeof(buf), "%f", val);
            dict_add(d, key, buf);
            break;
         }
         case VAL_BOOL: {
            int val = va_arg(ap, int); // promoted
            dict_add(d, key, val ? "true" : "false");
            break;
         }
         case VAL_FLOATP: {
            double v = va_arg(ap, double);
            int prec = va_arg(ap, int); // next arg is precision
            char buf[64];
            snprintf(buf, sizeof(buf), "%.*f", prec, (float)v);
            dict_add(d, key, buf);
            break;
         }
         case VAL_DOUBLEP: {
            double val = va_arg(ap, double);
            int prec = va_arg(ap, int);   // precision follows the double
            char buf[64];
            snprintf(buf, sizeof(buf), "%.*f", prec, val);
            dict_add(d, key, buf);
            break;
         }         default:
            // eat an extra va_arg if we don't know the type to keep aligned
            (void)va_arg(ap, void *);
            break;
      }

      type = va_arg(ap, int);
   }
}

void dict_import(dict *d, int first_type, ...) {
   va_list ap;
   va_start(ap, first_type);
   dict_import_va(d, first_type, ap);
   va_end(ap);
}

dict *dict_new_ext(int first_type, ...) {
   dict *d = dict_new();
   va_list ap;
   va_start(ap, first_type);
   dict_import_va(d, first_type, ap);
   va_end(ap);
   return d;
}

// Higher-level: build a dict from varargs, send it, free it
const char *dict2json_mkstr(int first_type, ...) {
   dict *d = dict_new();

   va_list ap;
   va_start(ap, first_type);
   dict_import_va(d, first_type, ap);
   va_end(ap);

   char *jp = dict2json(d);
   dict_free(d);

   // You must free jp when done with it
   return jp;
}
