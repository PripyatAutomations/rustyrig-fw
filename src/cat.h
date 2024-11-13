#if	!defined(_cat_control_h)
#define	_cat_control_h

#define	MAX_ARGS	12

struct cat_cmd {
   char 	verb[6];
   uint32_t		(*hndlr)();		// handler
   uint32_t		min_args,		// minimum arguments for SET mode
                max_args;		// maximum arguments for SET mode
};

extern uint32_t cat_pruint32_tf(char *str, ...);
extern uint32_t cat_parse_line(char *line);
extern uint32_t cat_init(void);
extern uint32_t cat_parse_line_real(char *line);
extern uint32_t cat_parse_line(char *line);

// cat_kpa500.c
extern struct cat_cmd cmd_kpa500[];
extern uint32_t cat_parse_amp_line(char *line);
extern uint32_t cat_printf(char *str, ...);

#endif	// !defined(_cat_control_h)
