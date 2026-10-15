#include <assert.h>
#include <rrclient/gtk/gtk.core.c>
#include <rrclient/gtk/gtk.help.c>
#include <rrclient/cmd.h>
bool dying, restarting;
time_t now;
enum GuiMode ui_mode = UI_MODE_GTK;
static bool staff;
client_cmd_t client_cmds[] = {
   {.cmd = "help", .desc = "Show help", .help_section = "Connection"},
   {.cmd = "admin", .desc = "Manage accounts", .help_section = "Administration", .admin = true},
   {0}
};
bool media_have_priv(const char *priv) { return staff; }
static char *contents(void) {
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(help_buffer, &start, &end);
   return gtk_text_buffer_get_text(help_buffer, &start, &end, FALSE);
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_help_show();
   assert(help_window && gtk_window_get_transient_for(GTK_WINDOW(help_window)) == GTK_WINDOW(main_window));
   char *text = contents();
   assert(strstr(text, "/help") && strstr(text, "F1") && !strstr(text, "/admin"));
   g_free(text);
   GtkWidget *original = help_window;
   staff = true;
   gtk_help_show();
   assert(help_window == original);
   text = contents();
   assert(strstr(text, "/admin") && strstr(text, "Administration"));
   g_free(text);
   GdkEventKey event = {.keyval = GDK_KEY_Escape};
   assert(help_key(help_window, &event, NULL));
   assert(!help_window);
   gtk_help_show();
   assert(help_window);
   gtk_help_shutdown();
   assert(!help_window && !help_buffer);
   gtk_widget_destroy(main_window);
   main_window = NULL;
   puts("PASS: GTK help popup shares categorized/filtered help, refreshes, reopens and shuts down");
}
