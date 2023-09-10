#if	!defined(_i2c_h)
#define	_i2c_h
#if	defined(HOST_POSIX)
// XXX: Include host i2c support
#else
// Include uc specific i2c
#endif

extern int i2c_init(int bus, int addr);

#endif	// !defined(_i2c_h)

