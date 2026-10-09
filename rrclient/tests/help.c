#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <rrclient/cmd.h>
#include <rrclient/ui.h>

client_cmd_t client_cmds[] = {
   {
      .cmd = "help", .desc = "help", .help_section = "Connection"
   },
   {
      .cmd = "media", .desc = "media", .help_section = "Media"
   },
   {
      .cmd = "rxcodec", .desc = "codec", .help_section = "Media"
   },
   {
      .cmd = "syslog", .desc = "syslog", .help_section = "Administration", .admin = true
   },
   {
      0
   }
};
enum GuiMode ui_mode = UI_MODE_NONE;
static bool staff;
static char output[16384];
static unsigned defer_count, flush_count;
bool media_have_priv(const char *priv) {
   assert(!strcmp(priv, "admin|owner"));

   return staff;
}
const char *ui_active_window_name(void) {
   return "#chat";
}
void tui_redraw_defer(void) {
   defer_count++;
}
void tui_redraw_flush(void) {
   flush_count++;
}
bool ui_print(const char *room, const char *fmt, ...) {
   assert(!strcmp(room, "#chat"));
   va_list args;
   va_start(args, fmt);
   size_t offset = strlen(output);
   vsnprintf(output + offset, sizeof(output) - offset, fmt, args);
   va_end(args);

   return true;
}
int main(void) {
   cmd_help(0, NULL);
   assert(strstr(output, "\00304Connection\017"));
   assert(strstr(output, "\00304Media\017"));
   assert(!strstr(output, "{bright-") && !strstr(output, "{headers}") && !strstr(output, "{reset}"));
   assert(strstr(output, "Connection") < strstr(output, "/help"));
   assert(strstr(output, "Media") < strstr(output, "/media"));
   assert(!strstr(strstr(output, "Media") + 5, "Media"));
   assert(!strstr(output, "Administration") && !strstr(output, "/syslog"));
   output[0] = 0;
   staff = true;
   ui_mode = UI_MODE_TUI;
   cmd_help(0, NULL);
   assert(strstr(output, "Administration") < strstr(output, "/syslog"));
   assert(defer_count == 1 && flush_count == 1);
   puts("PASS: sectioned help, single group headings, staff filtering and deferred TUI rendering");
}
