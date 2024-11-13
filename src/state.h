// This file contains the structures used for statistics and state
#if	!defined(_state_h)
#define	_state_h
#include <time.h>
#include <stdbool.h>

extern bool dying;

enum TuningState {
    TS_UNKNOWN = 0,
    TS_TUNING,
    TS_TUNE_FAILED,
    TS_TX_READY
};

enum LPFSelection {
    LPF_NONE = 0,			// No filter selected
    LPF_160M,				// 160M LPF
    LPF_80M,				// 80M LPF
    LPF_40M,				// 40M LPF
    LPF_30_20M,				// 30/20M LPF
    LPF_17_15M,				// 17/15M LPF
    LPF_12_10M,				// 12/10M LPF
    LPF_LOW_USER1,			// Low Band, User Filter 1
    LPF_LOW_USER2,			// Low Band, User Filter 2
    LPF_6M,				// 6M LPF
    LPF_2M,				// 2M LPF
    LPF_1_25M,				// 1.25M LPF
    LPF_70CM,				// 70CM LPF
    LPF_HIGH_USER1,			// High Band, User Filter 1
    LPF_HIGH_USER2			// High Band, User Filter 2
};

enum BPFSelection {
    BPF_NONE = 0,			// No filter selected
    BPF_160_80M,			// 160 - 80M BPF
    BPF_60_40M,				// 60 - 40M BPF
    BPF_30_20M,				// 30 - 20M BPF
    BPF_17_15M,				// 17 - 15M BPF
    BPF_12_10M				// 12 - 10M BPF
};

struct AmpState {
   uint32_t   alc[MAX_BANDS];		// ALC: 0-210, per band
   uint32_t   current_band;			// Current band selection
   uint32_t   afr;				// AFR:
   uint32_t   inhibit;			// Inhibit TX
   uint32_t   power;				// Power control
   uint32_t   standby;			// Standby mode
   uint32_t   output_target[MAX_BANDS];	// Target power (see formula in .c)
   float power_target;			// Target power configuration
   float therm_final;			// Thermal state of Final Transistor
   float therm_lpf;			// Thermal state of LPF board
};

struct ATUState {
   float power_fwd,			// Measured forward power
         power_rev;			// Measured reflected power
};

struct FilterState {
   enum LPFSelection LPF;		// Chosen TX Low Pass Filter
   enum BPFSelection BPF;		// Chosen RX Band Pass Filter
};

struct GlobalState {
   bool  tx_blocked;			// is TX blocked (user control)?
   bool  ptt;				// Are we transmitting?
   bool  faultbeep;			// Beep on faults
   bool  bc_standby;			// Stay in STANDBY on band change?
   uint8_t    fan_speed;		// Fan speed: 0-6 (0: auto)
   uint32_t   fault_code;		// Current fault code
   uint32_t   faults;			// Faults since last cleared
   uint32_t   tr_delay;			// T/R delay
   bool       eeprom_ready;		// EEPROM initialized
   bool       eeprom_dirty;		// EEPROM needs written out
   bool       eeprom_corrupted;		// EEPROM is corrupted; prompt before write out

   // Thermals
   float therm_inlet;			// Air inlet temp
   float therm_enclosure;		// Current temperature INSIDE box

   // Statistics
   time_t		time_tx_total,	// Lifetime TX time total
                        time_tx_last;	// Last transmission length
   float		power_tx_watts; // Lifetime total watts used transmitting

   // Sub-units
   struct AmpState 	low_amp,
                        high_amp;
   struct ATUState 	low_atu,
                        high_atu;
   struct FilterState 	low_filters,
                        high_filters;

#if	defined(HOST_POSIX)
   // Host build fd's/buffers/etc
   uint32_t		eeprom_fd;
   u_int8_t		*eeprom_mmap;
   uint32_t		logfile_fd;
   uint32_t		catpipe_fd;
#endif	// defined(HOST_POSIX)
};

extern void shutdown_rig(uint32_t signum);	// main.c

#endif	// !defined(_state_h)
