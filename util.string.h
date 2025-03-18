#if	!defined(__rr_util_string_h)
#define __rr_util_string_h

extern char *escape_html(const char *input);
extern void hash_to_hex(char *dest, const uint8_t *hash, size_t len);

#endif	// !defined(__rr_util_string_h)
