//
// util.math.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_util_math_h)
#define	__rr_util_math_h

extern float safe_atof(const char *s);
extern double safe_atod(const char *s);
extern long safe_atol(const char *s);
extern long long safe_atoll(const char *s);

#endif	// !defined(__rr_util_math_h)
