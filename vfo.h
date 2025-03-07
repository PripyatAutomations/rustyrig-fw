//
// VFO Object
//
// Storage and interface for Variable Frequency Oscillator objecs
//
#if	!defined(__rr_vfo_h)
#define	__rr_vfo_h
#include "config.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum VFOType {
   VFO_NONE = 0,	// Not present
   VFS_DDS,		// Direct Digital Synthesizer
   VFO_EXTERNAL		// External frequency reference
} VFOType;

typedef struct VFO {
   VFOType	type;
   uint32_t	input;		// input #
   float	freq;		// Current frequency in Hertz
} VFO;

#endif	// !defined(__rr_vfo_h)
