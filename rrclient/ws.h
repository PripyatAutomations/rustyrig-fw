//
// rrclient/ws.h
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrclient_ws_h)
#define __rrclient_ws_h
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "librustyaxe/config.h"
#include "librustyaxe/logger.h"
#include "librustyaxe/dict.h"
#include "librustyaxe/posix.h"
#include "rrclient/auth.h"
#include "rrclient/audio.h"
#include "ext/libmongoose/mongoose.h"
#include "librustyaxe/codecneg.h"
#include "librustyaxe/http.h"

extern bool prepare_msg(char *buf, size_t len, const char *fmt, ...);
extern void ws_init(void);
extern void ws_fini(void);

// Handle incoming messages
extern void ws_handler(struct mg_connection *c, int ev, void *ev_data);
extern bool ws_binframe_process(const char *data, size_t len);

// Send messages
extern bool ws_send_ptt_cmd(struct mg_connection *c, const char *vfo, bool ptt);
extern bool ws_send_mode_cmd(struct mg_connection *c, const char *vfo, const char *mode);
extern bool ws_send_freq_cmd(struct mg_connection *c, const char *vfo, float freq);
extern bool ws_send_error(http_client_t *cptr, const char *fmt, ...);
extern bool ws_send_alert(http_client_t *cptr, const char *fmt, ...);
extern bool ws_send_notice(http_client_t *cptr, const char *fmt, ...);

// ws.audio.c
extern bool ws_audio_init(void);
extern bool ws_select_codec(struct mg_connection *c, const char *codec, bool is_tx);

#endif	// !defined(__rrclient_ws_h)
