#if	!defined(__RRSERVER_GLOBALSTATE_H)
#define	__RRSERVER_GLOBALSTATE_H
#include "build_config.h"
#include <rrserver/amp.h>
#include <rrserver/atu.h>
#include <rrserver/filters.h>

struct GlobalState {
   logpriority_t log_level;              // Minimum log level to show
   bool tx_blocked;                      // is TX blocked (user control)?
   bool ptt;                             // Are we transmitting?
   bool faultbeep;                       // Beep on faults
   bool bc_standby;                      // Stay in STANDBY on band change?
   uint32_t serial;                      // Device serial #
   uint8_t fan_speed;                    // Fan speed: 0-6 (0: auto)
   uint32_t fault_code;                  // Current fault code
   uint32_t faults;                      // Faults since last cleared
   uint32_t tr_delay;                    // T/R delay

   // Thermals
   float therm_inlet;                    // Air inlet temp
   float therm_enclosure;                // Current temperature INSIDE box

   // Statistics
   time_t time_tx_total,                // Lifetime TX time total
          time_tx_last;                  // Last transmission length
   float power_tx_watts;                 // Lifetime total watts used
                                         // transmitting

   // Sub-units

   struct AmpState amps[RR_MAX_AMPS];
   struct ATUState atus[RR_MAX_ATUS];
   struct FilterState filters[RR_MAX_FILTERS];
   struct rr_backend *backend;

#if     defined(HOST_POSIX)   // Host build fd's/buffers/etc
   uint32_t logfile_fd;
   uint32_t catpipe_fd;
#endif // defined(HOST_POSIX)
};

#endif	// !defined(__RRSERVER_GLOBALSTATE_H)

