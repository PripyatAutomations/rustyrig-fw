//
// util.string.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_util_string_h)
#define __rr_util_string_h

#include <stdbool.h>
#include <ctype.h>

extern char *escape_html(const char *input);
extern void unescape_html(char *s);
extern void hash_to_hex(char *dest, const uint8_t *hash, size_t len);
extern bool parse_bool(const char *str);
extern int split_args(char *line, char ***argv_out);
extern int ansi_strlen(const char *s);
extern int visible_length(const char *s);

#endif	// !defined(__rr_util_string_h)
