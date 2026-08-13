//
// rrclient/cmd.admin.c: Admin related commands
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <termios.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librustyaxe/tui.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>
#include <ev.h>

extern bool dying;
extern time_t now;
extern bool ws_connected;
extern bool rrclient_send_chat(const char *data);
extern bool syslog_clear(void);
extern const char *server_name;	// remove this (connman.c)

#if     defined(USE_MONGOOSE)
extern struct mg_connection *ws_conn;
#endif

#if     defined(USE_GTK)
extern GtkWidget *chat_entry;
extern GtkWidget *rx_vol_slider;
extern GtkWidget *config_tab;
extern GtkWidget *main_notebook;
extern GtkWidget *main_tab;
extern GtkWidget *log_tab;
#endif // defined(USE_GTK)

bool cmd_die(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "die", VAL_STR, "talk.args", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );
   return false;
}

bool cmd_restart(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "restart", VAL_STR, "talk.reason", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );

   return false;
}

bool cmd_kick(int argc, char **args) {
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "kick", VAL_STR, "talk.reason", args[1]);
   mg_ws_send(ws_conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   free( (char *)jp );
   return false;
}
