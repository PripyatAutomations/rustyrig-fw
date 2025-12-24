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

#define	LOGINLEN	240		// an email address
#define	IRC_MSGLEN	1024		// extended for IRCv3
#define	CHANLEN		64		// channel name length
#define	NICKLEN		40		// nick name length
#define	TOPICLEN	256
#define	PASSLEN 	128
#define	USERLEN		16
#define	HOSTLEN		256
#define	NETLEN		64
#define	RECVQLEN	16384		// read, but unprocessed data from the server
#define	SENDQLEN	16384		// data waiting to be sent to the server
#define	AUTOJOIN_LEN	1024		// auto-join channels
#define USER_HASHSZ 127  // prime number

typedef struct {
   char          mode;          // mode letter, e.g. 'o', 'v', 'k'
   const char   *desc;          // description
   bool          arg_set;       // requires argument when being set
   bool          arg_unset;     // requires argument when being unset
} irc_mode_t;

typedef struct irc_message {
   int           argc;				// number of arguments
   char        **argv;
   char         *prefix;			// server prefix, if given
} irc_message_t;

typedef struct irc_chan_user {
    char nick[NICKLEN + 1];
    bool is_op;       // @
    bool is_voice;    // +
    bool is_halfop;   // % optional
    struct irc_chan_user *next;  // hash bucket chain
} irc_chan_user_t;

typedef struct irc_channel {
    char name[CHANLEN + 1];
    char mode[16];
    char topic[TOPICLEN + 1];
    int users;

    bool names_in_progress;
    bool userlist_complete;

    irc_chan_user_t *user_table[USER_HASHSZ];  // hash table of users
    struct irc_channel *next;                   // list of channels
} irc_channel_t;

typedef struct server_cfg {
    char 	 host[HOSTLEN + 1];
    char 	 network[NETLEN + 1];
    char         nick[NICKLEN + 1];
    char         ident[USERLEN + 1];
    char         account[LOGINLEN + 1];
    char         pass[PASSLEN + 1];
    char         autojoin[AUTOJOIN_LEN + 1];		
    int 	 port;
    int		 priority;
    bool	 tls;
    struct server_cfg *next;
} server_cfg_t;

typedef struct irc_client {
   server_cfg_t *server;		// server config data (if a client)
   bool		 connected;		// is it connected?
   bool          sent_login;		// have we sent login?
   bool		 is_server;		// is this a server? If so, we'll send relayed commands to it
   char          account[LOGINLEN + 1];
   char          nick[NICKLEN + 1];
   char          user[USERLEN + 1];
   char          hostname[HOSTLEN + 1];	// hostname/servername
   int		 fd;			// socket fd
   char		 recvq[RECVQLEN + 1];
   char          sendq[SENDQLEN + 1];
   ev_io io_watcher;
} irc_conn_t;

typedef bool (*irc_command_cb)(irc_conn_t *cptr, irc_message_t *mp);
#include <librustyaxe/event-bus.h>

// XXX: we need to merge this into irc_command_cb
typedef struct irc_callback {
   char         *cmd;				// IRC command
   int           numeric;			// IRC numeric
   char         *desc;
   int	         min_args_client;		// Minimum args from a client
   int	         min_args_server;		// Minimum args from a server
   int           max_args_client;		// Maximum args from a client
   int	         max_args_server;		// Maximum args from a server
   irc_command_cb  cb;
   char         *event_key;			// event callback key
   bool		 relayed;			// is this message to be sent to other links?
   bool		 unidle;			// does this clear idle on the user?
   struct irc_callback *next;
} irc_callback_t;

typedef struct {
   const char    *name;
   const char    *desc;
   bool		 relayed;			// is this message to be sent to other links?
   bool		 unidle;			// clears idle for the user
   char         *event_key;			// event callback key
   event_cb_t    event_cb;			// event callback function
   irc_command_cb cb;
} irc_command_t;

typedef struct {
   int            code;
   const char    *name;
   const char    *desc;
   irc_command_cb cb;
   char          *event_key;
   event_cb_t     event_cb;
   bool		 unidle;			// clears idle for the user
} irc_numeric_t;

typedef struct {
   const char    *name;
   const char    *desc;
} irc_cap_t;

#endif	// !defined(__libirc_struct_h)
