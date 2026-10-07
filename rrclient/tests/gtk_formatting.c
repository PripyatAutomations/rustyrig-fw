#include <assert.h>
#include "rrclient/gtk/gtk.core.c"
#include "rrclient/ui.colors.c"
time_t now;
bool dying, restarting;
int main(void) {
   char *text = gtk_colorize_string("\00304Radio and discovery\017");
   assert(!strcmp(text, "<span foreground=\"red\">Radio and discovery</span>"));
   g_free(text);
   text = gtk_colorize_string("\0033,1green on black\017");
   assert(!strcmp(text, "<span foreground=\"green\" background=\"black\">green on black</span>"));
   g_free(text);
   text = gtk_colorize_string("\0036,1magenta on black\017");
   assert(strstr(text, "foreground=\"magenta\" background=\"black\""));
   g_free(text);
   text = gtk_colorize_string("\0031,6black on magenta\017");
   assert(strstr(text, "foreground=\"black\" background=\"magenta\""));
   g_free(text);
   text = gtk_colorize_string("\00304123,45\017");
   assert(strstr(text, ">123,45</span>")); g_free(text);
   text = gtk_colorize_string("\00304<&>\017");
   assert(strstr(text, "&lt;&amp;&gt;")); g_free(text);
   puts("PASS: GTK mIRC red headings, numeric text, reset and markup escaping");
}
