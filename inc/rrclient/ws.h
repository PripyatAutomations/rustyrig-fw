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
#include "inc/config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "inc/logger.h"
#include "inc/dict.h"
#include "inc/posix.h"
#include "inc/mongoose.h"
#include "inc/http.h"
#include "rrclient/auth.h"
#include "rrclient/gtk-gui.h"
#include "rrclient/ws.h"

extern void ws_handler(struct mg_connection *c, int ev, void *ev_data);
extern void ws_init(void);
extern void ws_fini(void);
extern bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt);
extern bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode);
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);
extern bool connect_or_disconnect(GtkButton *button);
extern bool ws_binframe_process(const char *buf, size_t len);

#endif	// !defined(__rrclient_ws_h)
