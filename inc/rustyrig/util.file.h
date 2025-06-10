//
// util.file.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_util_file_h)
#define	__rr_util_file_h

extern bool file_exists(const char *path);
extern bool is_dir(const char *path);
extern const char *expand_path(const char *path);
extern const char *find_file_by_list(const char *files[], int file_count);

#endif	// !defined(__rr_util_file_h)
