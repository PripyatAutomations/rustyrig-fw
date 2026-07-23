#include <stdbool.h>
#include <time.h>

__attribute__((weak)) const char *server_name = NULL;
__attribute__((weak)) bool ws_connected = false;
__attribute__((weak)) bool server_ptt_state = false;
__attribute__((weak)) time_t poll_block_expire = 0;
__attribute__((weak)) time_t poll_block_delay = 1;

__attribute__((weak)) const char *get_server_property(const char *server, const char *prop) {
   (void)server;
   (void)prop;
   return NULL;
}
