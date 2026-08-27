//
// rrclient/command.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#ifndef	__rrclient_cmd_h
#define	__rrclient_cmd_h
#include <librustyaxe/core.h>
#include <librustyaxe/config.h>
#include <librustyaxe/struct.h>
#include <librustyaxe/event-bus.h>

typedef struct client_cmd {
   const char *cmd;
   const char *desc;
   int min_args;
   int max_args;
   bool (*cb)(int argc, char **args);
   event_cb_t (*event_cb)(const char *event, void *data, rrconn_t *cptr, void *user);
} client_cmd_t;

#ifdef USE_GTK
#include <gtk/gtk.h>
extern bool parse_chat_input_gtk(GtkButton *button, gpointer entry);
#endif

extern bool parse_chat_input_real(const char *msg);
extern bool cmd_admin(int argc, char **args);
extern bool cmd_chat(int argc, char **args);
extern bool cmd_clear(int argc, char **args);
extern bool cmd_clearlog(int argc, char **args);
extern bool cmd_config(int argc, char **args);
extern bool cmd_die(int argc, char **args);
extern bool cmd_disconnect(int argc, char **args);
extern bool cmd_help(int argc, char **args);
extern bool cmd_join(int argc, char **args);
extern bool cmd_kick(int argc, char **args);
extern bool cmd_log(int argc, char **args);
extern bool cmd_me(int argc, char **args);
extern bool cmd_msg(int argc, char **args);
extern bool cmd_notice(int argc, char **args);
extern bool cmd_part(int argc, char **args);
extern bool cmd_quit(int argc, char **args);
extern bool cmd_quote(int argc, char **args);
extern bool cmd_restart(int argc, char **args);
extern bool cmd_rxvol(int argc, char **args);
extern bool cmd_server(int argc, char **args);
extern bool cmd_topic(int argc, char **args);
extern bool cmd_whois(int argc, char **args);
extern bool cmd_win(int argc, char **args);

#endif // __rrclient_cmd_h
