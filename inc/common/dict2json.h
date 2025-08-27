#ifndef DICT2JSON_H
#define DICT2JSON_H

#include <stdio.h>
#include <stdarg.h>
#include "common/dict.h"

// minimal JSON tree
typedef struct json_node {
   char *key;
   char *value;                  // leaf string, if set
   struct json_node *child;      // first child
   struct json_node *next;       // next sibling
} json_node;

typedef enum {
   VAL_END    = 0,
   VAL_STR    = 1,
   VAL_INT    = 2,
   VAL_LONG   = 3,
   VAL_FLOAT  = 4,
   VAL_DOUBLE = 5,
   VAL_BOOL   = 6,
   VAL_FLOATP = 7,  // float with precision specified
   VAL_DOUBLEP = 8  // double with precision specified
} val_type_t;

/*
 * dict2jsonump - Convert a dict into a JSON string.
 *
 * Params:
 *   d   - pointer to dict to convert
 *   out - optional FILE* to also write the JSON into (may be NULL)
 *
 * Returns:
 *   A malloc'd JSON string (null-terminated).
 *   Caller MUST free() it when done.
 *
 * Notes:
 *   - Nested keys separated by '.' are converted into JSON objects.
 *   - Values are inserted as JSON strings.
 *   - If a key has no value, "UNDEF" is used.
 */
extern char *dict2json(dict *d);
extern dict *dict_new_ext(int first_type, ...);
extern void dict_import(dict *d, int first_type, ...);
extern void dict_import_va(dict *d, int first_type, va_list ap);

// XXX: Rework these eventually to use be static inline bit to wrap normal string versions of these...
// You must free ->ptr when you are done or memory will be leaked
extern const char *dict2json_mkstr(int first_type, ...);

#endif /* DICT2JSON_H */
