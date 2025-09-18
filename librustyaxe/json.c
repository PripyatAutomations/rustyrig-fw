//
// librustyaxe/json.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//#include "build_config.h"
#include "librustyaxe/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include "librustyaxe/dict.h"
#include "librustyaxe/cat.h"
#include "librustyaxe/logger.h"
#include "librustyaxe/posix.h"
#include "librustyaxe/json.h"

static const char *json_parse_value(const char *s, const char *path, dict *d);

// helper: append to path dynamically
static char *path_append(const char *base, const char *suffix) {
   size_t len = strlen(base) + strlen(suffix) + 2; // +1 for dot or brackets, +1 for \0
   char *newpath = malloc(len);
   if (!newpath) return NULL;

   if (suffix[0] == '[') {  // array index
      snprintf(newpath, len, "%s%s", base, suffix);
   } else if (base[0] != '\0') { // normal object key
      snprintf(newpath, len, "%s.%s", base, suffix);
   } else { // root key
      snprintf(newpath, len, "%s", suffix);
   }

   return newpath;
}

/////////////////////////////////

// helper: skip whitespace
static const char *skip_ws(const char *s) {
   while (*s && isspace((unsigned char)*s)) s++;
   return s;
}


// parse JSON string into a C string
static const char *json_parse_str(const char *s, char **out) {
   if (*s != '"') return NULL;
   s++;
   const char *start = s;
   size_t len = 0;

   while (*s && *s != '"') {
      if (*s == '\\') s++;  // skip escaped char
      s++; len++;
   }

   if (*s != '"') return NULL;

   *out = malloc(len + 1);
   if (!*out) return NULL;

   size_t j = 0;
   for (const char *p = start; p < s; p++, j++) {
      if (*p == '\\') p++;
      (*out)[j] = *p;
   }
   (*out)[j] = '\0';

   return s + 1;
}

// parse primitive (number, true, false, null)
static const char *json_parse_primitive(const char *s, char **out) {
   const char *start = s;
   while (*s && !strchr(",]} \t\r\n", *s)) s++;
   size_t len = s - start;
   *out = malloc(len + 1);
   if (!*out) return NULL;
   memcpy(*out, start, len);
   (*out)[len] = '\0';
   return s;
}

// parse JSON object
static const char *json_parse_obj(const char *s, const char *path, dict *d) {
   if (*s != '{') return NULL;
   s = skip_ws(s + 1);

   while (*s && *s != '}') {
      s = skip_ws(s);
      char *key = NULL;
      s = json_parse_str(s, &key);
      if (!s) return NULL;

      s = skip_ws(s);
      if (*s++ != ':') { free(key); return NULL; }

      char *newpath = path_append(path, key);
      free(key);
      if (!newpath) return NULL;

      s = skip_ws(s);
      s = json_parse_value(s, newpath, d);
      free(newpath);
      if (!s) return NULL;

      s = skip_ws(s);
      if (*s == ',') s++;
   }

   return (*s == '}') ? s + 1 : NULL;
}

// parse JSON array
static const char *json_parse_array(const char *s, const char *path, dict *d) {
   if (*s != '[') return NULL;
   s = skip_ws(s + 1);
   int idx = 0;

   while (*s && *s != ']') {
      char idxbuf[32];
      snprintf(idxbuf, sizeof(idxbuf), "[%d]", idx++);

      char *newpath = path_append(path, idxbuf);
      if (!newpath) return NULL;

      s = skip_ws(s);
      s = json_parse_value(s, newpath, d);
      free(newpath);
      if (!s) return NULL;

      s = skip_ws(s);
      if (*s == ',') s++;
   }

   return (*s == ']') ? s + 1 : NULL;
}


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

// unescape JSON string (expects surrounding quotes, returns malloc'd buffer)
char *json_unescape(const char *s) {
   if (!s) {
      return NULL;
   }

   size_t len = strlen(s);
   if (len < 2 || s[0] != '"' || s[len-1] != '"') {
      fprintf(stderr, "Invalid JSON string: %s\n", s);
      return NULL;
   }

   // worst case: input shrinks, so allocate len+1
   char *out = malloc(len);
   if (!out) {
      fprintf(stderr, "OOM in json_unescape\n");
      return NULL;
   }

   const char *p = s + 1;        // skip opening quote
   const char *end = s + len - 1; // before closing quote
   char *q = out;

   while (p < end) {
      if (*p == '\\') {
         p++;
         if (p >= end) {
            break;
         }

         switch (*p) {
            case '\"': *q++ = '\"'; break;
            case '\\': *q++ = '\\'; break;
            case '/':  *q++ = '/';  break;
            case 'b':  *q++ = '\b'; break;
            case 'f':  *q++ = '\f'; break;
            case 'n':  *q++ = '\n'; break;
            case 'r':  *q++ = '\r'; break;
            case 't':  *q++ = '\t'; break;
            case 'u': {
               if (end - p < 4) { // not enough chars
                  fprintf(stderr, "Invalid \\u escape\n");
                  free(out);
                  return NULL;
               }
               unsigned code = 0;
               for (int i = 0; i < 4; i++) {
                  p++;
                  if (p >= end) {
                     free(out);
                     return NULL;
                  }

                  char c = *p;
                  code <<= 4;
                  if (c >= '0' && c <= '9') {
                     code |= c - '0';
                  } else if (c >= 'a' && c <= 'f') {
                     code |= c - 'a' + 10;
                  } else if (c >= 'A' && c <= 'F') {
                     code |= c - 'A' + 10;
                  } else {
                     free(out);
                     return NULL;
                  }
               }
               if (code < 0x80) {
                  *q++ = code;
               } else if (code < 0x800) {
                  *q++ = 0xC0 | (code >> 6);
                  *q++ = 0x80 | (code & 0x3F);
               } else {
                  *q++ = 0xE0 | (code >> 12);
                  *q++ = 0x80 | ((code >> 6) & 0x3F);
                  *q++ = 0x80 | (code & 0x3F);
               }
               break;
            }
            default:
               *q++ = *p; // unknown escape, just copy
               break;
         }
      } else {
         *q++ = *p;
      }
      p++;
   }
   *q = '\0';
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
         case VAL_CHAR: {
            int val = va_arg(ap, int);
            char buf[2] = {0};
            snprintf(buf, sizeof(buf), "%c", val);
            dict_add(d, key, buf);
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
         case VAL_ULONG: {
            unsigned long val = va_arg(ap, unsigned long);
            char buf[32];
            snprintf(buf, sizeof(buf), "%lu", val);
            dict_add(d, key, buf);
            break;
         }
         case VAL_FLOAT: {
            double v = va_arg(ap, double);  // floats promote to double
            char buf[32];
            snprintf(buf, sizeof(buf), "%f", (float)v);
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
         }
         default: {
            // eat an extra va_arg if we don't know the type
            (void)va_arg(ap, void *);
            break;
         }
      }

      type = va_arg(ap, int);
   }
}

void dict_import_real(dict *d, int first_type, ...) {
   va_list ap;
   va_start(ap, first_type);
   dict_import_va(d, first_type, ap);
   va_end(ap);
}

// Higher-level: build a dict from varargs, send it, free it
const char *dict2json_mkstr_real(int first_type, ...) {
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

// parse JSON value (object, array, string, primitive)
static const char *json_parse_value(const char *s, const char *path, dict *d) {
   s = skip_ws(s);
   if (!*s) return NULL;

   if (*s == '"') {
      char *val = NULL;
      s = json_parse_str(s, &val);
      if (!s) return NULL;
      dict_add(d, path, val);
      free(val);
      return s;
   } else if (*s == '{') {
      return json_parse_obj(s, path, d);
   } else if (*s == '[') {
      return json_parse_array(s, path, d);
   } else {
      char *val = NULL;
      s = json_parse_primitive(s, &val);
      if (!s) return NULL;
      dict_add(d, path, val);
      free(val);
      return s;
   }
}

// public API: parse JSON into flattened dict
dict *json2dict(const char *json) {
   dict *d = dict_new();
   if (!d) return NULL;

   const char *res = json_parse_value(json, "", d); // start with empty root path
   if (!res) {
      dict_free(d);
      return NULL;
   }
   return d;
}

/*
 * Here we parse json into a dict
 */
void json_parse_and_flatten(const char *json, dict *dptr) {
   char path[4] = "$";
   json_parse_value(json, path, dptr);
}
