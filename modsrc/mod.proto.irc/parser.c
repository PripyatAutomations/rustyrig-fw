#include "librustyaxe/irc.struct.h"
#include "librustyaxe/irc.parser.h"

typedef struct rusty_client {
  int sock_fd;		// 	  // Socket file descriptor
  struct mg_connection *mg_conn;  // Store socket or mongoose connection
} client_t;

// return false if success, true if error
bool irc_socket_ready(client_t *cptr, const char *msg_data, size_t msg_len) {
   return false;
}

// return false if success, true if error
bool irc_socket_closed(client_t *cptr, const char *msg_data, size_t msg_len) {
   return false;
}

// return false if success, true if error
bool irc_socket_connected(client_t *cptr, const char *msg_data, size_t msg_len) {
   return false;
}
rusty_module_hook_t proto_irc_hooks[] = {
   { "socket.closed", CB_MSG, &irc_socket_closed, NULL },
   { "socket.connected", CB_MSG, &irc_socket_connected, NULL },
   { "socket.ready", CB_MSG, &irc_socket_ready, NULL }
};

// Module file header
typedef struct rusty_module_header {
   // Module API version
   unsigned int		mod_api_ver;
   char			*mod_author;
   char			*mod_copyright;
   char			*mod_ver;
   rusty_module_hook_t	*static_hooks[];
} rusty_module_header_t;

rusty_module_header_t mod_proto_irc = {
      .mod_api_ver = RUSTY_MODULE_API_VER,
      .mod_author = "rustyaxe <rustyaxe@istabpeople.com>",
      .mod_copyright = "Copyright 2025 rustyaxe. Released under MIT license",
      .mod_ver = VERSION,
      .static_hooks = proto_irc_hooks
};
