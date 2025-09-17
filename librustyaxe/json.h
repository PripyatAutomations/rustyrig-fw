//
// logger.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__common_json_h)
#define	__common_json_h
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include "librustyaxe/config.h"
#include "librustyaxe/dict.h"

// minimal JSON tree
typedef struct json_node {
   char *key;
   char *value;                  // leaf string, if set
   struct json_node *child;      // first child
   struct json_node *next;       // next sibling
} json_node;

typedef enum {
   VAL_END = 0,
   VAL_STR,
   VAL_INT,
   VAL_LONG,
   VAL_ULONG,
   VAL_FLOAT,
   VAL_DOUBLE,
   VAL_BOOL,
   VAL_FLOATP,  // float with precision specified
   VAL_DOUBLEP, // double with precision specified
   VAL_CHAR	    // a single character/byte (%c)
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
extern void dict_import_real(dict *d, int first_type, ...);
extern void dict_import_va(dict *d, int first_type, va_list ap);

#define dict_import(d, ...) dict_import_va((d), __VA_ARGS__, VAL_END)

extern char *json_escape(const char *s);

// XXX: Rework these eventually to use be static inline bit to wrap normal string versions of these...
// You must free ->ptr when you are done or memory will be leaked
extern const char *dict2json_mkstr_real(int first_type, ...);
#define dict2json_mkstr(...) dict2json_mkstr_real(__VA_ARGS__, VAL_END)
extern void json_parse_and_flatten(const char *json, dict *dptr);
extern dict *json2dict(const char *json);


#endif	// !defined(__common_json_h)
