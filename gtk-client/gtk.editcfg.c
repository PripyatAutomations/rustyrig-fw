#include <gtk/gtk.h>
#include "common/logger.h"

extern bool cfg_load(const char *path);

GtkWidget *cfgedit_window = NULL;

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
   Log(LOG_DEBUG, "config", "Edit config closed without saving for %s", ctx->filepath);
   gtk_widget_destroy(ctx->window);
   g_free(ctx->filepath);
   g_free(ctx);
}

static gboolean on_destroy(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   cfgedit_window = NULL;
   return FALSE;
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (!ctx->modified)
      return FALSE;

   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->window),
      GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
      "You have unsaved changes. Close anyway?");
   gboolean cancel = gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_YES;
   gtk_widget_destroy(dialog);

   if (!cancel)
      Log(LOG_DEBUG, "config", "Edit config closed without saving for %s", ctx->filepath);

   return cancel;
}

void gui_edit_config(const char *filepath) {
   if (cfgedit_window) {
      // Focus the window and leave
      gtk_window_present(GTK_WINDOW(cfgedit_window)); 
      return;
   }

   if (!filepath) {
      return;
   }

   Log(LOG_DEBUG, "gtk.editcfg", "Opening %s for editing", filepath);

   EditorContext *ctx = g_malloc0(sizeof(EditorContext));
   ctx->filepath = g_strdup(filepath);
   ctx->modified = FALSE;

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
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
      fread(buf, 1, len, fp);
      buf[len] = '\0';
      gtk_text_buffer_set_text(ctx->buffer, buf, -1);
      free(buf);
      fclose(fp);
   }

   GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 3);

   GtkWidget *btn_save = gtk_button_new_with_label("Save");
   GtkWidget *btn_save_as = gtk_button_new_with_label("Save Other");
   GtkWidget *btn_discard = gtk_button_new_with_label("Discard");

   gtk_box_pack_end(GTK_BOX(hbox), btn_discard, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(hbox), btn_save_as, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(hbox), btn_save, FALSE, FALSE, 0);

   g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), ctx);
   g_signal_connect(btn_save_as, "clicked", G_CALLBACK(on_save_other_clicked), ctx);
   g_signal_connect(btn_discard, "clicked", G_CALLBACK(on_discard_clicked), ctx);
   g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), ctx);
   g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), ctx);

   gtk_widget_show_all(window);

   cfgedit_window = window;
}
