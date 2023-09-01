#if	!defined(_parser_h)
#define	_parser_h

#define	MAX_ARGS	12

struct cat_cmd {
   char 	verb[6];
   int		(*hndlr)();		// handler
   int		min_args,		// minimum arguments for SET mode
                max_args;		// maximum arguments for SET mode
};

#endif	// !defined(_parser_h)
