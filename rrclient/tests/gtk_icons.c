/* Check the actual X11 icon property for both registered and ordinary windows. */
#include <assert.h>
#include <string.h>
#include <glib/gstdio.h>
#include <rrclient/gtk/gtk.icons.c>

static guchar *window_icon(GtkWidget *window, gint *length) {
   GdkAtom type;
   gint format;
   guchar *bytes = NULL;
   assert(gdk_property_get(gtk_widget_get_window(window), gdk_atom_intern_static_string("_NET_WM_ICON"),
      GDK_NONE, 0, 1024 * 1024, FALSE, &type, &format, length, &bytes));
   assert(format == 32 && *length > 0);
   return bytes;
}
int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   gchar *old_directory = g_get_current_dir();
   gchar *directory = g_dir_make_tmp("rr-gtk-icons-XXXXXX", NULL);
   assert(directory && g_chdir(directory) == 0);
   GdkPixbuf *icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
   gdk_pixbuf_fill(icon, 0x224488ff);
   assert(gdk_pixbuf_save(icon, "rr-icon-fixture.png", "png", NULL, NULL));
   g_object_unref(icon);
   GtkWidget *main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   assert(set_window_icon(main, "rr-icon-fixture")); /* Exercise the file fallback. */
   GtkWidget *extra = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main), 0, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Additional dialog");
   gtk_widget_show_all(main);
   gtk_widget_show_all(extra);
   gtk_widget_show_all(dialog);
   while (gtk_events_pending()) gtk_main_iteration();
   gdk_display_sync(gtk_widget_get_display(main));
   gint main_size, extra_size, dialog_size;
   guchar *main_icon = window_icon(main, &main_size);
   guchar *extra_icon = window_icon(extra, &extra_size);
   guchar *dialog_icon = window_icon(dialog, &dialog_size);
   assert(main_size == extra_size && main_size == dialog_size);
   assert(!memcmp(main_icon, extra_icon, main_size) && !memcmp(main_icon, dialog_icon, main_size));
   g_free(main_icon);
   g_free(extra_icon);
   g_free(dialog_icon);
   gtk_widget_destroy(dialog);
   gtk_widget_destroy(extra);
   gtk_widget_destroy(main);
   assert(g_remove("rr-icon-fixture.png") == 0 && g_chdir(old_directory) == 0 && g_rmdir(directory) == 0);
   g_free(old_directory);
   g_free(directory);
   puts("PASS: application file-fallback icon is identical on main, extra window and dialog");
   return 0;
}
