
#if	!defined(__libirc_struct_h)
#define	__libirc_struct_h

typedef struct irc_callback {
   char *message;			// IRC command
   int	min_args_client;		// Minimum args from a client
   int	min_args_server;		// Minimum args from a server
   int  max_args_client;		// Maximum args from a client
   int	max_args_server;		// Maximum args from a server
   bool (*callback)();			// callback
} irc_callback_t;

typedef struct irc_message {
   int    argc;				// number of arguments
   char **args;
} irc_message_t;

#endif	// !defined(__libirc_struct_h)
