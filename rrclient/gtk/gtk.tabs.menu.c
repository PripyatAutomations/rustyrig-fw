//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <rrclient/gtk/gtk.tabs.h>

/* Hold widget identities, never notebook indexes or borrowed conversation state. */
typedef struct {
   GtkNotebook *notebook;
   GtkWidget *page;
} TabCloseRequest;

/* Release references when a popup or confirmation is destroyed. */
static void tab_close_request_free(gpointer data) {
   TabCloseRequest *request = data;
   g_object_unref(request->notebook);
   g_object_unref(request->page);
   g_free(request);
}

/* Give an asynchronous menu/dialog its own references to the target page. */
static TabCloseRequest *tab_close_request_new(GtkNotebook *notebook, GtkWidget *page) {
   TabCloseRequest *request = g_new0(TabCloseRequest, 1);
   request->notebook = g_object_ref(notebook);
   request->page = g_object_ref(page);

   return request;
}

/* Only conversation pages opt in; admin/config/status/log pages stay protected. */
void gtk_tabs_page_set_close_func(GtkWidget *page, GtkTabCloseFunc function) {
   GtkTabCloseFunc *stored = function ? g_new(GtkTabCloseFunc, 1) : NULL;

   if (stored) {
      *stored = function;
   }
   g_object_set_data_full(G_OBJECT(page), "rr-tab-close-func", stored, g_free);
}

/* Recheck membership after arbitrary reordering or server-driven page removal. */
static void tab_close_request_apply(TabCloseRequest *request, gboolean force) {
   if (gtk_notebook_page_num(request->notebook, request->page) < 0) {
      return;
   }
   GtkTabCloseFunc *function = g_object_get_data(G_OBJECT(request->page), "rr-tab-close-func");

   if (function) {
      (*function)(request->page, force);
   }
}

/* Cancellation leaves the conversation, subscriptions and draft untouched. */
static void tab_force_response(GtkDialog *dialog, gint response, TabCloseRequest *request) {
   if (response == GTK_RESPONSE_ACCEPT) {
      tab_close_request_apply(request, TRUE);
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
}

/* Force closes require explicit confirmation and never default to acceptance. */
static void tab_force_activate(GtkMenuItem *item, TabCloseRequest *request) {
   (void)item;
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(request->notebook));
   GtkWidget *dialog = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
      "Force close this tab?");
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "Close without waiting for the server to confirm? Any unsent text in this tab will be lost.");
   gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL, "Force close", GTK_RESPONSE_ACCEPT, NULL);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
   TabCloseRequest *confirmation = tab_close_request_new(request->notebook, request->page);
   g_object_set_data_full(G_OBJECT(dialog), "rr-tab-close-request", confirmation, tab_close_request_free);
   g_signal_connect(dialog, "response", G_CALLBACK(tab_force_response), confirmation);
   gtk_widget_show(dialog);
}

/* Normal closes reuse the client's acknowledged PART/query-close behavior. */
static void tab_close_activate(GtkMenuItem *item, TabCloseRequest *request) {
   (void)item;
   tab_close_request_apply(request, FALSE);
}

/* Build the same two actions for every tab, disabling both for protected pages. */
static GtkWidget *tab_close_menu_new(GtkNotebook *notebook, GtkWidget *page) {
   GtkWidget *menu = gtk_menu_new();
   gtk_menu_attach_to_widget(GTK_MENU(menu), GTK_WIDGET(notebook), NULL);
   TabCloseRequest *request = tab_close_request_new(notebook, page);
   g_object_set_data_full(G_OBJECT(menu), "rr-tab-close-request", request, tab_close_request_free);
   GtkWidget *close = gtk_menu_item_new_with_label("Close");
   GtkWidget *force = gtk_menu_item_new_with_label("Force close…");
   gboolean allowed = g_object_get_data(G_OBJECT(page), "rr-tab-close-func") != NULL;
   gtk_widget_set_sensitive(close, allowed);
   gtk_widget_set_sensitive(force, allowed);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), close);
   gtk_menu_shell_append(GTK_MENU_SHELL(menu), force);
   g_signal_connect(close, "activate", G_CALLBACK(tab_close_activate), request);
   g_signal_connect(force, "activate", G_CALLBACK(tab_force_activate), request);
   g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
   gtk_widget_show_all(menu);

   return menu;
}

/* Right-click targets the clicked page without first changing the active tab. */
static gboolean tab_menu_button_press(GtkWidget *widget, GdkEventButton *event, GtkNotebook *notebook) {
   if (event->type != GDK_BUTTON_PRESS || event->button != GDK_BUTTON_SECONDARY) {
      return FALSE;
   }
   GtkWidget *page = g_object_get_data(G_OBJECT(widget), "rr-tab-page");

   if (!page || gtk_notebook_page_num(notebook, page) < 0) {
      return FALSE;
   }
   GtkWidget *menu = tab_close_menu_new(notebook, page);
   gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);

   return TRUE;
}

/* Bind wrapped buttons and native vertical labels through the same handler. */
void gtk_tabs_bind_menu(GtkWidget *widget, GtkNotebook *notebook, GtkWidget *page) {
   g_object_set_data(G_OBJECT(widget), "rr-tab-page", page);
   gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK);
   g_signal_connect_object(widget, "button-press-event", G_CALLBACK(tab_menu_button_press), notebook, 0);
}
