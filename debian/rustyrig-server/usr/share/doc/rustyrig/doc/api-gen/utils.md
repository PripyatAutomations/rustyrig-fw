# librustyaxe Utilities

Various utility functions for common operations.

## Key Functions
```c
// String utilities
char *strdup(const char *s);
int strcasecmp(const char *s1, const char *s2);

// File utilities
bool file_exists(const char *path);
bool file_read(const char *path, char **content, size_t *length);

// Memory utilities
void *xmalloc(size_t size);
void xfree(void *ptr);
```
