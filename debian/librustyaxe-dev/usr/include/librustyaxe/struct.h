#if     !defined(__librustyaxe_struct_h)
#define	__librustyaxe_struct_h

// XXX:ASAP Move these to build time config file
// Maximum number of subscribed channels for users
#define	MAX_RX_CHANNELS 64       // User RX channels
#define	MAX_TX_CHANNELS 16       // User TX channels

// HTTP Basic-auth user
#define	HTTP_MAX_USERS 64                // How many users are allowed in
                                          // http.users?
#define	HTTP_USER_LEN 16                 // username length (16 char)
#define	HTTP_PASS_LEN 40                 // sha1: 40, sha256: 64
#define	HTTP_HASH_LEN 40                 // sha1
#define	HTTP_TOKEN_LEN 14                // session-id / nonce length,
                                          // longer moar secure
#define	HTTP_UA_LEN 512                  // allow 128 bytes
#define	USER_PRIV_LEN 100                // privileges list
#define	USER_EMAIL_LEN 128               // email address

#define	HTTP_MAX_SESSIONS 32             // max sessions total
#define	HTTP_MAX_ELMERS 8                // how many elmers can accept
                                          // elevate request from the
                                          // user?
#define	HTTP_MAX_NOOBS 8                 // how many noobs can an elmer
#define	HTTP_MAX_USERS 64                // How many users are allowed in
#define	HTTP_MAX_ROUTES 64

#define	LOGINLEN 240             // an email address
#define	IRC_MSGLEN 1024          // extended for IRCv3
#define	CHANLEN 64               // channel name length
#define	NICKLEN 40               // nick name length
#define	TOPICLEN 256
#define	PASSLEN 128
#define	USERLEN 16
#define	HOSTLEN 256
#define	NETLEN 64
#define	RECVQLEN 16384           // read, but unprocessed data from the
                                  // server
#define	SENDQLEN 16384           // data waiting to be sent to the server
#define	AUTOJOIN_LEN 1024        // auto-join channels
#define	USER_HASHSZ 127 // prime number

typedef struct rrconn rrconn_t;
typedef struct irc_command irc_command_t;

typedef struct {
   char mode;                    // mode letter, e.g. 'o', 'v', 'k'
   const char   *desc;           // description
   bool arg_set;                 // requires argument when being set
   bool arg_unset;               // requires argument when being unset
} irc_mode_t;

typedef struct irc_message {
   int argc;                                     // number of arguments
   char        **argv;
   char         *prefix;                         // server prefix, if given
} irc_message_t;

typedef struct irc_chan_user {
   char nick[NICKLEN + 1];
   bool is_op;         // @
   bool is_voice;      // +
   bool is_halfop;     // % optional
   struct irc_chan_user *next;    // hash bucket chain
} irc_chan_user_t;

typedef struct irc_channel {
   char name[CHANLEN + 1];
   char mode[16];
   char topic[TOPICLEN + 1];
   int users;

   bool names_in_progress;
   bool userlist_complete;

   irc_chan_user_t *user_table[USER_HASHSZ];    // hash table of users
   struct irc_channel *next;                     // list of channels
} irc_channel_t;


// XXX: we need to merge this into irc_command_cb
typedef struct irc_callback {
   char         *cmd;                            // IRC command
   int numeric;                                  // IRC numeric
   char         *desc;
   int min_args_client;                          // Minimum args from a client
   int min_args_server;                          // Minimum args from a server
   int max_args_client;                          // Maximum args from a client
   int max_args_server;                          // Maximum args from a server
//   irc_command_cb cb;
   char         *event_key;                      // event callback key
   bool relayed;                                 // is this message to be sent
                                                 // to
                                                 // other links?
   bool unidle;                                  // does this clear idle on the
                                                 // user?
   struct irc_callback *next;
} irc_callback_t;


typedef void (*event_cb_t)(const char *event, const char *data, rrconn_t *cptr, void *user);

struct irc_command_t {
   const char    *name;
   const char    *desc;
   bool relayed;                                 // is this message to be sent
                                                 // to
                                                 // other links?
   bool unidle;                                  // clears idle for the user
   char         *event_key;                      // event callback key
   event_cb_t event_cb;                          // event callback function
//   irc_command_cb cb;
};

typedef struct {
   int code;
   const char    *name;
   const char    *desc;
//   irc_command_cb cb;
   char          *event_key;
   event_cb_t event_cb;
   bool unidle;                                  // clears idle for the user
} irc_numeric_t;

typedef struct {
   const char    *name;
   const char    *desc;
} irc_cap_t;

typedef struct server_cfg {
   char host[HOSTLEN + 1];
   char network[NETLEN + 1];
   char nick[NICKLEN + 1];
   char ident[USERLEN + 1];
   char account[LOGINLEN + 1];
   char pass[PASSLEN + 1];
   char autojoin[AUTOJOIN_LEN + 1];
   int port;
   int priority;
   bool tls;
   struct server_cfg *next;
} server_cfg_t;

// http.users entry
struct http_user {
   int uid;
   char name[HTTP_USER_LEN + 1];                         // Username
   char pass[HTTP_PASS_LEN + 1];                         // Password hash
   char email[USER_EMAIL_LEN + 1];                       // Email address
   char privs[USER_PRIV_LEN + 1];                        // privileges string?
   bool enabled;                                         // Is the user enabled?
   int max_sessions;                                       // maximum allowed
                                                         // sessions
   int sessions;                                           // active logins
   int is_muted;                                         // is this user muted?
};
typedef struct http_user http_user_t;

struct rrconn {
   bool active;                  // Is this slot actually used or is it
                                 // free-listed?
   bool authenticated;           // Is the user fully logged in?
   bool ghost;                   // Is the session a ghost?
   bool is_ws;                   // Flag to indicate if it's a WebSocket client
   bool is_ptt;                  // Is the user keying up ANY attached rig?
   char ptt_vfo;                 // VFO letter ('A'...) they keyed, 0 if none
   bool sent_login;                             // have we sent login?
   bool is_server;                              // is this a server? If so, we'll send
                                                // relayed commands to it
   int fd;                                      // socket fd
   int guest_id;                 // 4 digit unique id for guest users in
                                 // chat/etc
                                 // for comfort
   int ping_attempts;            // How many times have we tried to ping the
                                  // client without answer?
   int latency_ms;               // Last measured RTT to this client (ms)
   int ptt_session;              // Set when PTT has been raised
   u_int32_t user_flags;         // Bit flags for user features, permissions,
                                 // etc.
   time_t connected;             // when was the socket connected?
   time_t session_expiry;        // When does the session expire?
   time_t session_start;         // When did they login?
   time_t last_cat;	         // Last rigctl message acted on
   time_t last_chat;		 // Last chat message received
   time_t last_heard;            // when a last valid message was heard from client
   time_t last_ping;             // If client is pending timeout, this will
                                 // contain the time a ping was sent to check
                                 // for dead connection
   time_t last_cat_update;                      // Last time we sent a cat message
   time_t noob_cooldown;         // Noobs can't TX until this time (noob.cool-down)
   http_user_t *user;            // pointer to http user, once login is sent. DO
                                 // NOT TRUST IF authenticated != true!
   char nonce[HTTP_TOKEN_LEN + 1];   // Authentication nonce - only used between
                                     // challenge & pass stages
   char chatname[HTTP_USER_LEN + 1];   // username to show in chat (GUESTxxxx or
                                       // USER)
   char account[LOGINLEN + 1];                  // Account
   char nick[NICKLEN + 1];                      // Nickname (if not account)
   char username[USERLEN + 1];                  // Username ('ident')
   char hostname[HOSTLEN + 1];                  // hostname/servername
   char token[HTTP_TOKEN_LEN + 1];   // Session token
   char  *user_agent;            // User-agent
   char user_ip[256];            // string containing the user's IP
   int  user_port;		 // the local port of the tcp connection
   char  *cli_version;           // Client version
#if     defined(USE_MONGOOSE)
   struct mg_connection *conn;   // Connection pointer (HTTP or WebSocket)
#endif
   time_t ghost_time;            // When did the session become a ghost?

   // These contain arrays of audio channel IDs
   u_int32_t rx_channels[MAX_RX_CHANNELS];
   u_int32_t tx_channels[MAX_RX_CHANNELS];
   // This is for connections between instances (NYI)
   enum {
      CONN_NONE = 0,
      CONN_RIGUI,               // User-interface (chat and CAT)
      CONN_AUDIO_RX,            // RX audio
      CONN_AUDIO_TX             // TX audio
   } connection_type;
   char codec_rx[5], codec_tx[5];                // 4 byte ID of the codec for
                                                 // each audio direction
   // Codecs advertised by this connection in media.cmd=capab.  The server
   // uses this when validating shared media-channel selections.
   char media_codecs[256];
   // Comma-separated WebSocket chat rooms this session has joined.
   char rooms[AUTOJOIN_LEN];

   // This is a little ugly, but this stores pointers to the users associated with elmer/noob system
   union {
      rrconn_t *elmers[HTTP_MAX_ELMERS];       // pointer(s) to elmers who have accepted to babysit noob user
      rrconn_t *noobs[HTTP_MAX_NOOBS];         // pointer(s) to noobs this user is babysitting
   } en_data;
   rrconn_t *next;     // pointer to next client in list
   server_cfg_t *server;                        // server config data (if a client)
   char recvq[RECVQLEN + 1];                    // Stuff pending processing from the
                                                // socket
   char sendq[SENDQLEN + 1];                    // Stuff pending being sent on the socket
   size_t tx_bytes,                             // Bytes we've sent
          tx_packets;                           // Packets we've sent
   size_t rx_bytes,                             // Bytes we've received
          rx_packets;                           // Packets we've received
#ifdef  USE_MONGOOSE
   struct mg_connection *mg;
#endif
};
typedef struct rrconn rrconn_t;

//typedef bool (*irc_command_cb)(rrconn_t *cptr, irc_message_t *mp);

#endif // !defined(__librustyaxe_struct_h)
