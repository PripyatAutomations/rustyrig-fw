#if	!defined(__libirc_struct_h)
#define	__libirc_struct_h

#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <ev.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/irc.struct.h>

#define	HOSTLEN	256
#define	NETLEN	64
#define	USERLEN	16
#define	NICKLEN	48
#define	PASSLEN 128

#define	RECVQLEN	16384		// read, but unprocessed data from the server
#define	SENDQLEN	16384		// data waiting to be sent to the server


typedef struct {
   char mode;          // mode letter, e.g. 'o', 'v', 'k'
   const char *desc;   // description
   bool arg_set;       // requires argument when being set
   bool arg_unset;     // requires argument when being unset
} irc_mode_t;

typedef struct irc_message {
   int    argc;				// number of arguments
   char **argv;
} irc_message_t;

typedef struct server_cfg {
    char 	host[HOSTLEN+1];
    char 	network[NETLEN+1];
    char        nick[NICKLEN+1];
    char        ident[USERLEN+1];
    char        pass[PASSLEN+1];
    int 	port;
    int		priority;
    bool	tls;
    struct server_cfg *next;
} server_cfg_t;

typedef struct irc_client {
   server_cfg_t *server;
   bool		 connected;		// is it connected?
   char          nick[NICKLEN+1];
   int		 fd;			// socket fd
   char		 recvq[RECVQLEN+1];
   char          sendq[SENDQLEN+1];
   ev_io io_watcher;
} irc_client_t;

typedef bool (*irc_command_cb)(irc_client_t *cptr, irc_message_t *mp);

// XXX: we need to merge this into irc_command_cb
typedef struct irc_callback {
   char *message;			// IRC command
   int	min_args_client;		// Minimum args from a client
   int	min_args_server;		// Minimum args from a server
   int  max_args_client;		// Maximum args from a client
   int	max_args_server;		// Maximum args from a server
   bool (*callback)();			// callback
   struct irc_callback *next;
} irc_callback_t;


typedef struct {
   const char     *name;
   const char     *desc;
   irc_command_cb  cb;
} irc_command_t;

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


#endif	// !defined(__libirc_struct_h)
