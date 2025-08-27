#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/dict.h"
#include "common/dict2json.h"
#include "../../ext/libmongoose/mongoose.h"

// escape JSON string (returns malloc'd buffer with quotes included)
static char *json_escape(const char *s) {
   if (!s) {
      return strdup("\"\"");
   }

   size_t len = strlen(s);
   // worst case every char becomes \uXXXX (6 bytes) + quotes
   char *out = malloc(len * 6 + 3);
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

static json_node *make_node(const char *key) {
   json_node *n = calloc(1, sizeof(*n));
   n->key = strdup(key);
   return n;
}

static json_node *find_child(json_node *parent, const char *key) {
   for (json_node *c = parent->child; c; c = c->next) {
      if (strcmp(c->key, key) == 0) {
         return c;
      }
   }
   json_node *n = make_node(key);
   n->next = parent->child;
   parent->child = n;
   return n;
}

static void json_insert(json_node *root, const char *fullkey, const char *val) {
   char *tmp = strdup(fullkey);
   char *tok = strtok(tmp, ".");
   json_node *cur = root;

   while (tok) {
      cur = find_child(cur, tok);
      tok = strtok(NULL, ".");
   }
   if (cur->value) free(cur->value);
   cur->value = strdup(val ? val : "UNDEF");
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
