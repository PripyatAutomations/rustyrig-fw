//
// ws.h
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_ws_h)
#define __rrclient_ws_h
#include "rustyrig/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "rustyrig/logger.h"
#include "rustyrig/dict.h"
#include "rustyrig/posix.h"
#include "rustyrig/mongoose.h"
#include "rustyrig/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

//#define	DEBUG_WS_BINFRAMES		// turn this off soon
extern void ws_handler(struct mg_connection *c, int ev, void *ev_data);
extern void ws_init(void);
extern void ws_fini(void);
extern bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt);
extern bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode);
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);
extern bool ws_binframe_process(const char *data, size_t len);
extern bool connect_or_disconnect(GtkButton *button);
extern bool disconnect_server(void);
extern bool connect_server(void);
extern bool prepare_msg(char *buf, size_t len, const char *fmt, ...);
extern const char *get_server_property(const char *server, const char *prop);
extern char active_server[512];

#endif	// !defined(__rrclient_ws_h)
