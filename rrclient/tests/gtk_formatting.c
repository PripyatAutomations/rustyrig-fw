#include <assert.h>
#include "rrclient/gtk/gtk.core.c"
#include "rrclient/gtk/gtk.chat.c"
time_t now;
bool dying, restarting;

static void drain_gtk_events(void) {
   while (g_main_context_pending(NULL)) {
      g_main_context_iteration(NULL, FALSE);
   }
}

static void type_entry_text(GtkWidget *entry, const char *text) {
   gint position = gtk_editable_get_position(GTK_EDITABLE(entry));
   gtk_editable_insert_text(GTK_EDITABLE(entry), text, -1, &position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);
   drain_gtk_events();
}

static GtkWidget *find_color_choice(GtkWidget *popover, guint color) {
   GtkWidget *grid = gtk_bin_get_child(GTK_BIN(popover));
   assert(GTK_IS_GRID(grid));
   GList *children = gtk_container_get_children(GTK_CONTAINER(grid));
   GtkWidget *found = NULL;
   assert(g_list_length(children) == 16);

   for (GList *item = children ; item ; item = item->next) {
      if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item->data), "rr-color-value")) == color) {
         found = GTK_WIDGET(item->data);
      }
   }

   g_list_free(children);
   assert(found);

   return found;
}

static void test_color_popup(void) {
   int argc = 0;
   char **argv = NULL;

   if (!gtk_init_check(&argc, &argv)) {
      puts("SKIP: GTK popup test requires a display");

      return;
   }

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *entry = gtk_entry_new();
   gtk_container_add(GTK_CONTAINER(window), entry);
   GtkWidget *popover = gtk_popover_new(entry);
   gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
   g_object_set_data(G_OBJECT(entry), "rr-color-popover", popover);
   g_signal_connect(entry, "changed", G_CALLBACK(gtk_chat_entry_changed), NULL);
   gtk_widget_show_all(window);
   gtk_widget_grab_focus(entry);
   drain_gtk_events();

   gtk_chat_insert_format(entry, 0x03);
   drain_gtk_events();
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C");
   gint marker_selection_start, marker_selection_end;
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &marker_selection_start, &marker_selection_end));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 1);
   GtkWidget *sample = gtk_bin_get_child(GTK_BIN(find_color_choice(popover, 4)));
   g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(sample)), ==, "[4]");
   gtk_editable_select_region(GTK_EDITABLE(entry), 0, 1);
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 4)));
   drain_gtk_events();
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C4"));
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 3);
   sample = gtk_bin_get_child(GTK_BIN(find_color_choice(popover, 3)));
   g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(sample)), ==, "[4,3]");
   gint selection_start, selection_end;
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &selection_start, &selection_end));

   gtk_editable_select_region(GTK_EDITABLE(entry), 1, 2);
   gtk_button_clicked(GTK_BUTTON(find_color_choice(popover, 3)));
   drain_gtk_events();
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C4,3"));
   assert(!gtk_editable_get_selection_bounds(GTK_EDITABLE(entry), &selection_start, &selection_end));
   type_entry_text(entry, "hello");
   assert(!strcmp(gtk_entry_get_text(GTK_ENTRY(entry)), "C4,3hello"));
   char *serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\0034,3hello"));
   g_free(serialized);

   gtk_editable_delete_text(GTK_EDITABLE(entry), 0, -1);
   gint position = 0;
   gtk_editable_insert_text(GTK_EDITABLE(entry), "C", 1, &position);
   gtk_editable_set_position(GTK_EDITABLE(entry), position);
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "C"));
   g_free(serialized);

   gtk_editable_delete_text(GTK_EDITABLE(entry), 0, -1);
   drain_gtk_events();
   gtk_widget_grab_focus(entry);
   drain_gtk_events();
   gtk_chat_insert_format(entry, 0x03);
   drain_gtk_events();
   type_entry_text(entry, "1");
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C1");
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 1);
   type_entry_text(entry, ",");
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C1,");
   assert(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "rr-color-mode")) == 2);
   type_entry_text(entry, "3");
   g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(entry)), ==, "C1,3");
   serialized = gtk_chat_serialize_entry(entry);
   assert(!strcmp(serialized, "\0031,3"));
   g_free(serialized);
   gtk_widget_destroy(window);
}

int main(void) {
   assert(gtk_formatting_control('B') == 0x02);
   assert(gtk_formatting_control('C') == 0x03);
   assert(gtk_formatting_control('U') == 0x1f);
   assert(gtk_formatting_control('X') == 0);
   assert(!strcmp(gtk_formatting_token(0x02), "B"));
   char *literal = gtk_colorize_string("{red} stays literal");
   assert(!strcmp(literal, "{red} stays literal"));
   g_free(literal);
   char *text = gtk_colorize_string("\00304Radio and discovery\017");
   assert(!strcmp(text, "<span foreground=\"#ff0000\">Radio and discovery</span>"));
   g_free(text);
   text = gtk_colorize_string("\0033,1green on black\017");
   assert(!strcmp(text, "<span foreground=\"#009300\" background=\"#000000\">green on black</span>"));
   g_free(text);
   text = gtk_colorize_string("\0031,3hello\003");
   assert(!strcmp(text, "<span foreground=\"#000000\" background=\"#009300\">hello</span>"));
   g_free(text);
   text = gtk_colorize_string("\0036,1magenta on black\017");
   assert(strstr(text, "foreground=\"#9c009c\" background=\"#000000\""));
   g_free(text);
   text = gtk_colorize_string("\0031,6black on magenta\017");
   assert(strstr(text, "foreground=\"#000000\" background=\"#9c009c\""));
   g_free(text);
   text = gtk_colorize_string("\00302A\00312B\00306C\00313D\00310E\00311F\00300G\00315H");
   assert(strstr(text, "foreground=\"#00007f\">A</span><span foreground=\"#0000fc\">B"));
   assert(strstr(text, "foreground=\"#9c009c\">C</span><span foreground=\"#ff00ff\">D"));
   assert(strstr(text, "foreground=\"#009393\">E</span><span foreground=\"#00ffff\">F"));
   assert(strstr(text, "foreground=\"#ffffff\">G</span><span foreground=\"#d2d2d2\">H"));
   g_free(text);
   text = gtk_colorize_string("\00304123,45\017");
   assert(strstr(text, ">123,45</span>"));
   g_free(text);
   text = gtk_colorize_string("\00304<&>\017");
   assert(strstr(text, "&lt;&amp;&gt;"));
   g_free(text);
   text = gtk_colorize_string("\002bold\002 and \037underlined\037");
   assert(strstr(text, "<b>bold</b>"));
   assert(strstr(text, "<u>underlined</u>"));
   g_free(text);
   text = gtk_colorize_string("\035italic\035 \036strike\036 \021mono\021");
   assert(strstr(text, "<i>italic</i>"));
   assert(strstr(text, "<s>strike</s>"));
   assert(strstr(text, "<tt>mono</tt>"));
   g_free(text);
   text = gtk_colorize_string("\0034red \026reversed\026");
   assert(strstr(text, "foreground=\"black\" background=\"#ff0000\""));
   g_free(text);
   text = gtk_colorize_string("\026reverse\017plain");
   assert(!strcmp(text, "<span foreground=\"black\" background=\"white\">reverse</span>plain"));
   g_free(text);
   test_color_popup();
   puts("PASS: GTK mIRC red headings, numeric text, reset and markup escaping");
}
