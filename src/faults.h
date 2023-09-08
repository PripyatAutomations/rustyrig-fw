#if	!defined(_faults_h)
#define	_faults_h

// A higher (numeric) error will replace a lower numbered one in the fault code register...
// XXX: Sort these by order of severity
#define	FAULT_NONE		0x00	// No faults
#define	FAULT_STUCK_RELAY	0x01	// Power On Self Test determined a relay is stuck
#define	FAULT_INLET_THERMAL	0x02	// Inlet air too hot
#define	FAULT_TOO_HOT		0x03	// Enclosure ambient too high
#define FAULT_HIGH_SWR		0x04	// VSWR is above threshold
#define	FAULT_FINAL_THERMAL	0x05	// Final is too hot
#define	FAULT_FINAL_LOW_CURRENT	0x06	// Final supply current much below expected
#define	FAULT_FINAL_HIGH_CURRENT 0x07	// Final supply current too high
#define	FAULT_FINAL_LOW_VOLT	0x08	// Final supply voltage too low
#define	FAULT_FINAL_HIGH_VOLT	0x09	// Final supply voltage too high
#define	FAULT_TOT_TIMEOUT	0x0a	// TOT Expired

// Last fault needs to be 99 (0x64)
#define	FAULT_UNKNOWN		0x64

extern int set_fault(int fault);

#endif	// !defined(_faults_h)
