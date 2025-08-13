//
// cat.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(_rr_cat_control_h)
#define	_rr_cat_control_h
#include "common/config.h"

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
} CATCommand;

extern int32_t rr_cat_printf(char *str, ...);
extern int32_t rr_cat_parse_line(char *line);
extern int32_t rr_cat_init(void);
extern int32_t rr_cat_parse_line_real(char *line);
extern int32_t rr_cat_parse_line(char *line);

// rr_cat_kpa500.c
extern struct rr_cat_cmd cmd_kpa500[];
extern int32_t rr_cat_parse_amp_line(char *line);
extern int32_t rr_cat_printf(char *str, ...);

#if	1	//defined(FEATURE_HTTP)
//#include "rrserver/mongoose.h"
// for websocket.c
extern bool rr_cat_parse_ws(rr_cat_req_type reqtype, struct mg_ws_message *msg);
#endif	// defined(FEATURE_HTTP)

#include "common/cat.kpa500.h"
#include "common/cat.yaesu.h"

#endif	// !defined(_rr_cat_control_h)
