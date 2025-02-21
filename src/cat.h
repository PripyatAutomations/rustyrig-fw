#if	!defined(_cat_control_h)
#define	_cat_control_h
#include "config.h"

#define	MAX_ARGS	12

struct cat_cmd {
   char 	verb[6];
   int32_t		(*hndlr)();		// handler
   int32_t	min_args,		// minimum arguments for SET mode
                max_args;		// maximum arguments for SET mode
};

extern int32_t cat_printf(char *str, ...);
extern int32_t cat_parse_line(char *line);
extern int32_t cat_init(void);
extern int32_t cat_parse_line_real(char *line);
extern int32_t cat_parse_line(char *line);

// cat_kpa500.c
extern struct cat_cmd cmd_kpa500[];
extern int32_t cat_parse_amp_line(char *line);
extern int32_t cat_printf(char *str, ...);

#endif	// !defined(_cat_control_h)
