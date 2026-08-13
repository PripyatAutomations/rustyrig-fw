//
// inc/rrclient/command.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_command_h)
#define	__rrclient_command_h
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

#endif // !defined(__rrclient_config_h)
