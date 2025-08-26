#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/dict.h"
#include "common/dict2json.h"

// minimal JSON tree
typedef struct json_node {
   char *key;
   char *value;                  // leaf string, if set
   struct json_node *child;      // first child
   struct json_node *next;       // next sibling
} json_node;

// escape JSON string (returns malloc'd buffer with quotes included)
static char *json_escape(const char *s) {
   if (!s) return strdup("\"\"");

   size_t len = strlen(s);
   // worst case every char becomes \uXXXX (6 bytes) + quotes
   char *out = malloc(len * 6 + 3);
   char *p = out;
   *p++ = '"';
   for (size_t i = 0; i < len; i++) {
      unsigned char c = (unsigned char)s[i];
      switch (c) {
         case '\"': *p++='\\'; *p++='\"'; break;
         case '\\': *p++='\\'; *p++='\\'; break;
         case '\b': *p++='\\'; *p++='b';  break;
         case '\f': *p++='\\'; *p++='f';  break;
         case '\n': *p++='\\'; *p++='n';  break;
         case '\r': *p++='\\'; *p++='r';  break;
         case '\t': *p++='\\'; *p++='t';  break;
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
   for (json_node *c = parent->child; c; c = c->next)
      if (strcmp(c->key, key) == 0)
         return c;
   json_node *n = make_node(key);
   n->next = parent->child;
   parent->child = n;
   return n;
}

static void insert(json_node *root, const char *fullkey, const char *val) {
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
      while (b->len + slen + 1 > b->cap)
         b->cap *= 2;
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
      if (c->next) sbuf_putc(out, ',');
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

   while ((rank = dict_enumerate(d, rank, &key, &val)) >= 0)
      insert(&root, key, val);

   sbuf out;
   sbuf_init(&out);
   dump_json(&root, &out);
   free_json(root.child); // free children

   return out.buf; // malloc()'d buffer
}
