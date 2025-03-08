#if	!defined(__rr_debug_h)
#define	__rr_debug_h

extern bool debug_init(void);
extern bool debug_filter(const char *subsys, const char *fmt);

#endif	// !defined(__rr_debug_h)
