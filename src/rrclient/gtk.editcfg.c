//
// gtk-client/gtk.editcfg.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "common/config.h"
#define	__RRCLIENT	1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include "../ext/libmongoose/mongoose.h"
#include "common/logger.h"
#include "common/dict.h"
#include "common/posix.h"
#include "common/util.file.h"
#include "rrclient/auth.h"
#include "rrclient/gtk.core.h"
#include "rrclient/ws.h"
#include "rrclient/audio.h"
#include "rrclient/userlist.h"
#include "common/client-flags.h"

extern dict *cfg;
extern struct mg_connection *ws_conn;
extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern GtkWidget *toggle_userlist_button; // userlist.c
extern GtkWidget *main_notebook;	  // gtk.core.c
extern dict *cfg_load(const char *path);
GtkWidget *config_tab = NULL;

typedef struct {
   GtkWidget *window;
   GtkTextBuffer *buffer;
   gchar *filepath;
   gboolean modified;
} EditorContext;

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
   ((EditorContext *)user_data)->modified = TRUE;
}

static void apply_config(const char *filename) {
   cfg_load(filename);
}

static void on_save_clicked(GtkButton *btn, gpointer user_data) {
   EditorContext *ctx = user_data;
   GtkTextIter start, end;
   gtk_text_buffer_get_bounds(ctx->buffer, &start, &end);
   gchar *text = gtk_text_buffer_get_text(ctx->buffer, &start, &end, FALSE);

   FILE *fp = fopen(ctx->filepath, "w");
   if (fp) {
      fwrite(text, 1, strlen(text), fp);
      fclose(fp);
      ctx->modified = FALSE;
      apply_config(ctx->filepath);
   }
   g_free(text);

   Log(LOG_DEBUG, "config", "Edits saved for %s", ctx->filepath);
   gtk_widget_destroy(ctx->window);
   g_free(ctx->filepath);
   g_free(ctx);
}

static void on_save_other_clicked(GtkButton *btn, gpointer user_data) {
   EditorContext *ctx = user_data;

   GtkWidget *dialog = gtk_file_chooser_dialog_new("Save As",
      GTK_WINDOW(ctx->window),
      GTK_FILE_CHOOSER_ACTION_SAVE,
      "_Cancel", GTK_RESPONSE_CANCEL,
      "_Save", GTK_RESPONSE_ACCEPT,
      NULL);

   gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
   gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "config.cfg");

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds(ctx->buffer, &start, &end);
      gchar *text = gtk_text_buffer_get_text(ctx->buffer, &start, &end, FALSE);

      FILE *fp = fopen(filename, "w");
      if (fp) {
         fwrite(text, 1, strlen(text), fp);
         fclose(fp);
         ctx->modified = FALSE;

         GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(ctx->window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "Apply new config from \"%s\"?", filename);
         if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_YES) {
            apply_config(filename);
         }
         gtk_widget_destroy(confirm);
      }

      g_free(text);
      g_free(filename);
   }
   gtk_widget_destroy(dialog);
}

static void on_discard_clicked(GtkButton *btn, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (ctx->modified) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->window),
         GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
         "You have unsaved changes. Discard them?");
      gboolean cancel = gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_YES;
      gtk_widget_destroy(dialog);
      if (cancel) {
         return;
      }
   }

   Log(LOG_DEBUG, "config", "Edit config closed without saving for %s", ctx->filepath);
   gtk_widget_destroy(ctx->window);
   g_free(ctx->filepath);
   g_free(ctx);
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (!ctx->modified) {
      return FALSE;
   }

   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->window),
      GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
      "You have unsaved changes. Close anyway?");
   gboolean cancel = gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_YES;
   gtk_widget_destroy(dialog);

   if (!cancel) {
      Log(LOG_DEBUG, "config", "Edit config closed without saving for %s", ctx->filepath);
   }

   return cancel;
}

void gui_edit_config(const char *filepath) {
   gui_window_t *win = gui_find_window(NULL, "editcfg");

   if (win) {
      GtkWidget *cfgedit_window = win->gtk_win;

      if (cfgedit_window) {
         // Focus the window and leave
         gtk_window_present(GTK_WINDOW(cfgedit_window)); 
         return;
      }
   }

   if (!filepath) {
      return;
   }

   Log(LOG_DEBUG, "gtk.editcfg", "Opening %s for editing", filepath);

   EditorContext *ctx = g_malloc0(sizeof(EditorContext));
   ctx->filepath = g_strdup(filepath);
   ctx->modified = FALSE;

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gui_window_t *window_t = ui_new_window(window, "editcfg");
   gtk_window_set_title(GTK_WINDOW(window), filepath);
   gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);
   ctx->window = window;

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

   GtkWidget *textview = gtk_text_view_new();
   gtk_container_add(GTK_CONTAINER(scrolled), textview);

   ctx->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
   g_signal_connect(ctx->buffer, "changed", G_CALLBACK(on_buffer_changed), ctx);

   FILE *fp = fopen(filepath, "r");
   if (fp) {
      fseek(fp, 0, SEEK_END);
      long len = ftell(fp);
      rewind(fp);
      char *buf = malloc(len + 1);
      if (!buf) {
         fprintf(stderr, "OOM in gui_edit_config?!\n");
         fclose(fp);
         return;
      }
      fread(buf, 1, len, fp);
      buf[len] = '\0';
      gtk_text_buffer_set_text(ctx->buffer, buf, -1);
      free(buf);
      fclose(fp);
   }

   GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 3);

   GtkWidget *btn_save = gtk_button_new_with_label("Save");
   GtkWidget *btn_save_as = gtk_button_new_with_label("Save As");
   GtkWidget *btn_discard = gtk_button_new_with_label("Discard");

   gtk_box_pack_end(GTK_BOX(hbox), btn_discard, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(hbox), btn_save_as, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(hbox), btn_save, FALSE, FALSE, 0);

   g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), ctx);
   g_signal_connect(btn_save_as, "clicked", G_CALLBACK(on_save_other_clicked), ctx);
   g_signal_connect(btn_discard, "clicked", G_CALLBACK(on_discard_clicked), ctx);
   g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), ctx);

   gtk_widget_show_all(window);
   gtk_widget_realize(window);
   place_window(window);
}

/////////////////////////////
// Config tab in tab strip //
/////////////////////////////
extern char *config_file;		// main.c

static void on_edit_config_button(GtkComboBoxText *combo, gpointer user_data) {
   if (user_data != NULL) {
      gui_edit_config(user_data);
   } else {
      gui_edit_config(config_file);
   }
}

GtkWidget *init_config_tab(void) {
   GtkWidget *nw = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   GtkWidget *cfg_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(cfg_tab_label), "(<u>3</u>) Config");

   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, cfg_tab_label);

   GtkWidget *config_label = gtk_label_new("Configuration will go here...");
   gtk_box_pack_start(GTK_BOX(nw), config_label, FALSE, FALSE, 12);

   GtkWidget *btn = gtk_button_new_with_label("Edit Config");
   g_signal_connect(btn, "clicked", G_CALLBACK(on_edit_config_button), config_file);
   gtk_box_pack_start(GTK_BOX(nw), btn, FALSE, FALSE, 0);

   toggle_userlist_button = gtk_button_new_with_label("Toggle Userlist");
   gtk_box_pack_start(GTK_BOX(nw), toggle_userlist_button, FALSE, FALSE, 3);
   g_signal_connect(toggle_userlist_button, "clicked", G_CALLBACK(on_toggle_userlist_clicked), NULL);
   return nw;
}
