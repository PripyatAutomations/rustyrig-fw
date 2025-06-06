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

extern char *escape_html(const char *input);
extern void hash_to_hex(char *dest, const uint8_t *hash, size_t len);

#endif	// !defined(__rr_util_string_h)
