//
// gtk-client/win32.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stdbool.h>

#ifdef _WIN32
bool win32_init(void) {
   return false;
}

char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *result = (char *)malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}
#endif
