#if	!defined(__libirc_struct_h)
#define	__libirc_struct_h

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/irc.struct.h>

typedef bool (*irc_command_cb)(const char *prefix, int argc, char **argv);

typedef struct {
   char mode;          // mode letter, e.g. 'o', 'v', 'k'
   const char *desc;   // description
   bool arg_set;       // requires argument when being set
   bool arg_unset;     // requires argument when being unset
} irc_mode_t;

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

typedef struct {
   const char *name;
   const char *desc;
} irc_cap_t;

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
