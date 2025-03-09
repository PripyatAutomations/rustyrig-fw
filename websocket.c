#include "config.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include "i2c.h"
#include "state.h"
#include "eeprom.h"
#include "logger.h"
#include "cat.h"
#include "posix.h"
#include "websocket.h"

extern bool dying;		// in main.c
extern struct GlobalState rig;	// Global state

bool ws_init(struct mg_mgr *mgr) {
   if (mgr == NULL) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");
      return true;
   }
   return false;
}

bool ws_handle(struct mg_ws_message *msg, struct mg_connection *c) {
   if (!cat_parse_ws(REQ_WS, msg)) {
//      mg_ws_send(c, msg->data.buf, msg->data.len, WEBSOCKET_OP_TEXT);
   }

   return false;
}
