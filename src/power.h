#if	!defined(_power_h)
#define	_power_h

extern float get_voltage(uint32_t src);
extern float get_current(uint32_t src);
extern float get_swr(uint32_t amp);
extern float get_power(uint32_t amp);
extern uint32_t check_power_thresholds(void);

#endif	// !defined(_power_h)
