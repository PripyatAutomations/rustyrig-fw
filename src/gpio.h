#if	!defined(_gpio_h)
#define	_gpio_h
#if	defined(HOST_BUILD)
// XXX: Include host gpio support
extern const int max_gpiochips;
#else
// Include uc specific gpio
#endif

extern int gpio_init(void);

#endif	// !defined(__h)

