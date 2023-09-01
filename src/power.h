#if	!defined(_power_h)
#define	_power_h

extern float get_voltage(int src);
extern float get_current(int src);
extern float get_swr(int amp);
extern float get_power(int amp);

#endif	// !defined(_power_h)
