#ifndef DICT2JSON_H
#define DICT2JSON_H

#include <stdio.h>
#include "common/dict.h"


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

#endif /* DICT2JSON_H */
