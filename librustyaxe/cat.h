//
// cat.h: Interface to the CAT parsers
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(_rr_cat_control_h)
#define	_rr_cat_control_h
#include <librustyaxe/config.h>
#include "build_config.h"
// Maximum arguments
#define	MAX_ARGS	12

typedef enum rr_cat_req_type {
   REQ_NONE = 0,                // Not set (invalid)
   REQ_IO,                      // Via io.c
   REQ_WS                       // Via mongoose websocket
} rr_cat_req_type;


// For the command table structure
struct rr_cat_cmd {
   char 	verb[6];
   int32_t		(*hndlr)();		// handler
   int32_t	min_args,		// minimum arguments for SET mode
                max_args;		// maximum arguments for SET mode
};

// Command lookup table
typedef struct {
    const char *command;
    uint8_t min_args;
    uint8_t max_args;
    void (*rr_cat_yaesu_r)(const char *args);
} CATCommandTable;

// User callback signature
typedef void (*CATCallback)(const char *args);

// Built-in command table entries
typedef struct {
   const char *cmd;
   int min_args;
   int max_args;
   CATCallback cb;
} CATBuiltin;

// Linked list node for callbacks
typedef struct CATCallbackNode {
   CATCallback cb;
   struct CATCallbackNode *next;
} CATCallbackNode;

// Linked list of commands
typedef struct CATCommand {
   char *cmd;
   CATCallbackNode *callbacks;
   struct CATCommand *next;
} CATCommand;

/////////
// Amp //
/////////
// State of the amplifier module
struct AmpState {
   uint32_t   alc[MAX_BANDS];           // ALC: 0-210, per band
   uint32_t   current_band;             // Current band selection
   uint32_t   afr;                      // AFR:
   bool       inhibit;                  // Inhibit TX / Locked out
   uint32_t   power;                    // Power control
   uint32_t   standby;                  // Standby mode
   uint32_t   output_target[MAX_BANDS]; // Target power (see formula in .c)
   float      power_target;             // Target power configuration
   float      thermal;                  // Thermal state of Final Transistor (in degF)
   bool       warmup_required;          // If true, we will enforce a warmup time
   uint32_t   warmup_time;              // Warmup time required for device
};
   
   
extern int32_t rr_cat_printf(char *str, ...);
extern int32_t rr_cat_parse_line(char *line);
extern int32_t rr_cat_init(void);
extern int32_t rr_cat_parse_line_real(char *line);
extern int32_t rr_cat_parse_line(char *line);
extern int32_t rr_cat_parse_amp_line(char *line);
extern int32_t rr_cat_printf(char *str, ...);
//extern bool rr_cat_parse_ws(rr_cat_req_type reqtype, struct mg_ws_message *msg);
extern bool cat_register_callback(const char *cmd, CATCallback cb);
extern bool cat_invoke_callbacks(const char *cmd, const char *args);
extern bool cat_register_builtin_array(const CATBuiltin *arr);

#include <librustyaxe/cat.kpa500.h>
#include <librustyaxe/cat.yaesu.h>

#endif	// !defined(_rr_cat_control_h)
