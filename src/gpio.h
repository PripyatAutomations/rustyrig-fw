#if	!defined(_gpio_h)
#define	_gpio_h
#if	defined(HOST_POSIX)
// XXX: Include host gpio support
extern const int max_gpiochips;
#else
// Include uc specific gpio
#endif

struct GPIO_pin {
    int gpiochip;       // index into GPIOchips array
    // XXX: properties (in/out/etc)
};
typedef struct GPIO_pin GPIOpin;

extern int gpio_init(void);

#endif	// !defined(__h)

