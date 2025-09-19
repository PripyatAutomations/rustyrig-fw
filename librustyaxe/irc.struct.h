
#if	!defined(__libirc_struct_h)
#define	__libirc_struct_h


typedef bool (*irc_command_cb)(const char *prefix, int argc, char **argv);

typedef struct {
   const char     *name;
   const char     *desc;
   irc_command_cb  cb;
} irc_command_t;

typedef struct irc_message {
   int    argc;				// number of arguments
   char **args;
} irc_message_t;

typedef struct {
   int code;
   const char *name;
   const char *desc;
   bool (*cb)(const irc_message_t *msg);
} irc_numeric_t;

typedef struct irc_callback {
   char *message;			// IRC command
   int	min_args_client;		// Minimum args from a client
   int	min_args_server;		// Minimum args from a server
   int  max_args_client;		// Maximum args from a client
   int	max_args_server;		// Maximum args from a server
   bool (*callback)();			// callback
   struct irc_callback *next;
} irc_callback_t;

#endif	// !defined(__libirc_struct_h)
