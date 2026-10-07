// No display is needed: exercise the shutdown timer's control flow.
#include <assert.h>
#include "rrclient/gtk/gtk.core.c"
time_t now;
bool dying, restarting;
static unsigned quit_calls, refresh_calls;
guint gtk_main_level(void) { return 1; }
void gtk_main_quit(void) { quit_calls++; }
void ptt_button_refresh(void) { refresh_calls++; }
int main(void) {
   assert(frontend_gtk_update_now(NULL) == G_SOURCE_CONTINUE);
   assert(refresh_calls == 1 && !quit_calls);
   dying = true;
   frontend_gtk_update_source = 123;
   assert(frontend_gtk_update_now(NULL) == G_SOURCE_REMOVE);
   assert(!frontend_gtk_update_source);
   assert(quit_calls == 1 && refresh_calls == 1);
   puts("PASS: GTK shutdown timer quits its loop and returns without unloading executing module code");
}
