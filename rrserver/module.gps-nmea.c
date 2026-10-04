//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// NMEA adapter: the generic serial manager owns PTY/device IO and framing.
#include <librustyaxe/core.h>
#include <librustyaxe/io.serial.h>

static rr_event_token_t input_token;
rr_module_event_t modexports[] = {{0}};
static void input(const char *event, const char *sentence,
   rrconn_t *client, void *user) {
   (void)event; (void)client; (void)user;
   if (sentence) event_emit("gps.nmea.input", NULL, sentence);
}
bool rr_module_init(void) {
   input_token = event_on_token("serial.gps.input", input, NULL);
   return input_token == NULL;
}
void rr_module_shutdown(void) {
   event_off_token(input_token); input_token = NULL;
}
