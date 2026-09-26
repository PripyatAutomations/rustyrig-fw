//
// rrclient/gtk.editcfg.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#define	__RRCLI 1
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librustyaxe/config.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/userlist.h>
#include <rrclient/gtk.core.h>

extern dict *cfg;
extern void on_toggle_userlist_clicked(GtkButton *button, gpointer user_data);
extern GtkWidget *toggle_userlist_button;
extern GtkWidget *main_notebook;
extern dict *cfg_load(const char *path);

GtkWidget *config_tab = NULL;
extern defconfig_t defcfg[];

typedef struct cfg_editor_binding {
   const char *key;
   defconfig_type_t type;
   GtkWidget *widget;
} cfg_editor_binding_t;

static bool cfg_editor_numeric_candidate(GtkEditable *editable,
   const char *insert, gint position, gint selection_start, gint selection_end,
   defconfig_type_t type) {
   const char *current = gtk_entry_get_text(GTK_ENTRY(editable));
   size_t current_len = strlen(current);
   size_t insert_len = strlen(insert);
   size_t prefix_len = (size_t)(selection_start < 0 ? position : selection_start);
   size_t suffix_start = (size_t)(selection_end < 0 ? position : selection_end);
   if (prefix_len > current_len) prefix_len = current_len;
   if (suffix_start > current_len) suffix_start = current_len;
   if (suffix_start < prefix_len) suffix_start = prefix_len;

   char *candidate = calloc(1, prefix_len + insert_len +
      (current_len - suffix_start) + 1);
   if (!candidate) return false;
   memcpy(candidate, current, prefix_len);
   memcpy(candidate + prefix_len, insert, insert_len);
   memcpy(candidate + prefix_len + insert_len, current + suffix_start,
      current_len - suffix_start);

   bool valid = true;
   unsigned dots = 0;
   for (size_t i = 0; candidate[i]; i++) {
      if (candidate[i] >= '0' && candidate[i] <= '9') continue;
      if (candidate[i] == '-' && type != DEFCONFIG_UINT && i == 0) continue;
      if (candidate[i] == '.' && type == DEFCONFIG_FLOAT && dots++ == 0) continue;
      valid = false;
      break;
   }
   free(candidate);
   return valid;
}

static void cfg_editor_numeric_insert(GtkEditable *editable, gchar *insert,
   gint length, gint *position, gpointer user_data) {
   (void)length;
   cfg_editor_binding_t *binding = (cfg_editor_binding_t *)user_data;
   if (!binding || !insert || !position) return;
   gint selection_start = -1, selection_end = -1;
   gtk_editable_get_selection_bounds(editable, &selection_start, &selection_end);
   if (!cfg_editor_numeric_candidate(editable, insert, *position,
      selection_start, selection_end, binding->type))
      g_signal_stop_emission_by_name(editable, "insert-text");
}

static void cfg_editor_changed(GtkWidget *widget, gpointer user_data) {
   cfg_editor_binding_t *binding = (cfg_editor_binding_t *)user_data;
   if (!binding || !binding->key) return;
   char value[512] = "";
   switch (binding->type) {
   case DEFCONFIG_BOOL:
      snprintf(value, sizeof(value), "%s",
         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? "true" : "false");
      break;
   case DEFCONFIG_INT:
   case DEFCONFIG_UINT:
   case DEFCONFIG_FLOAT:
      snprintf(value, sizeof(value), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
      break;
   default:
      if (GTK_IS_COMBO_BOX_TEXT(widget)) {
         const char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
         snprintf(value, sizeof(value), "%s", text ? text : "");
         g_free((gpointer)text);
      } else {
         snprintf(value, sizeof(value), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
      }
      break;
   }
   if (!cfg_set_value(binding->key, value))
      Log(LOG_WARN, "gtk.config", "Rejected value for %s", binding->key);
}

static GtkWidget *cfg_editor_widget(const defconfig_t *def, const char *value,
   cfg_editor_binding_t **binding_out) {
   GtkWidget *widget = NULL;
   if (def->type == DEFCONFIG_BOOL) {
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      GtkWidget *on = gtk_radio_button_new_with_label(NULL, "true");
      GtkWidget *off = gtk_radio_button_new_with_label_from_widget(
         GTK_RADIO_BUTTON(on), "false");
      bool enabled = value && (!strcasecmp(value, "true") ||
         !strcasecmp(value, "yes") || !strcmp(value, "1"));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled ? on : off), TRUE);
      gtk_box_pack_start(GTK_BOX(box), on, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), off, FALSE, FALSE, 0);
      widget = box;
   } else if (def->type == DEFCONFIG_ENUM && def->choices && *def->choices) {
      widget = gtk_combo_box_text_new();
      char *choices = strdup(def->choices), *save = NULL;
      int active = 0, selected = -1;
      for (char *p = strtok_r(choices, "|", &save); p; p = strtok_r(NULL, "|", &save), active++) {
         gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), p);
         if (value && !strcasecmp(p, value)) selected = active;
      }
      free(choices);
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), selected >= 0 ? selected : 0);
   } else {
      widget = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(widget), value ? value : "");
      gtk_entry_set_visibility(GTK_ENTRY(widget), def->type != DEFCONFIG_PASSWORD);
      g_signal_connect(widget, "changed", G_CALLBACK(cfg_editor_changed), NULL);
   }
   if (!widget) return NULL;
   cfg_editor_binding_t *binding = calloc(1, sizeof(*binding));
   if (!binding) { gtk_widget_destroy(widget); return NULL; }
   binding->key = def->key;
   binding->type = def->type;
   binding->widget = widget;
   g_object_set_data_full(G_OBJECT(widget), "rr-cfg-binding", binding, free);
   if (def->type == DEFCONFIG_BOOL) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
      for (GList *it = children; it; it = it->next)
         g_signal_connect(it->data, "toggled", G_CALLBACK(cfg_editor_changed), binding);
      g_list_free(children);
   } else if (def->type == DEFCONFIG_ENUM && def->choices && *def->choices)
      g_signal_connect(widget, "changed", G_CALLBACK(cfg_editor_changed), binding);
   else {
      if (def->type == DEFCONFIG_INT || def->type == DEFCONFIG_UINT ||
          def->type == DEFCONFIG_FLOAT)
         g_signal_connect(widget, "insert-text",
            G_CALLBACK(cfg_editor_numeric_insert), binding);
      g_signal_connect(widget, "changed", G_CALLBACK(cfg_editor_changed), binding);
   }
   if (binding_out) *binding_out = binding;
   return widget;
}

static int cfg_editor_defconfig_compare(const void *left, const void *right) {
   const defconfig_t *a = *(const defconfig_t * const *)left;
   const defconfig_t *b = *(const defconfig_t * const *)right;
   bool a_section = strchr(a->key, ':') != NULL;
   bool b_section = strchr(b->key, ':') != NULL;

   if (a_section != b_section) return a_section ? 1 : -1;
   return strcmp(a->key, b->key);
}

static GtkWidget *cfg_editor_panel(void) {
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
      GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
   gtk_container_set_border_width(GTK_CONTAINER(grid), 6);
   int row = 0;
   size_t def_count = 0;
   while (defcfg[def_count].key) def_count++;
   const defconfig_t **defs = calloc(def_count, sizeof(*defs));
   if (!defs) {
      gtk_container_add(GTK_CONTAINER(scroll), grid);
      return scroll;
   }
   for (size_t i = 0; i < def_count; i++) defs[i] = &defcfg[i];
   qsort(defs, def_count, sizeof(*defs), cfg_editor_defconfig_compare);

   for (size_t i = 0; i < def_count; i++) {
      const defconfig_t *def = defs[i];
      const char *value = cfg_get(def->key);
      GtkWidget *label = gtk_label_new(def->key);
      gtk_widget_set_halign(label, GTK_ALIGN_START);
      gtk_widget_set_tooltip_text(label, def->help ? def->help : "");
      GtkWidget *help = gtk_label_new(def->help ? def->help : "");
      gtk_widget_set_halign(help, GTK_ALIGN_START);
      gtk_widget_set_hexpand(help, TRUE);
      gtk_widget_set_tooltip_text(help, def->help ? def->help : "");
      cfg_editor_binding_t *binding = NULL;
      GtkWidget *editor = cfg_editor_widget(def, value ? value : def->val, &binding);
      if (!editor) continue;
      gtk_widget_set_hexpand(editor, TRUE);
      gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), editor, 1, row, 1, 1);
      gtk_grid_attach(GTK_GRID(grid), help, 2, row, 1, 1);
      row++;
   }
   free(defs);
   gtk_container_add(GTK_CONTAINER(scroll), grid);
   return scroll;
}

typedef struct {
   GtkWidget *window;
   gui_window_t *window_t;
   GtkTextBuffer *buffer;
   gchar *filepath;
   gboolean modified;
} EditorContext;

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
   if (!user_data) {
      return;
   }

   ( (EditorContext *)user_data )->modified = TRUE;
}

static void apply_config(const char *filename) {
   if (!filename) {
      return;
   }

   cfg_reload(filename);
}

static void destroy_editor(EditorContext *ctx) {
   if (!ctx) {
      return;
   }

   /*
    * Remove the window from the GUI window registry before destroying the actual GTK
    * window.
    */
   if (ctx->window_t) {
      gui_forget_window(ctx->window_t, "editcfg");
      ctx->window_t = NULL;
   }

   if (ctx->window) {
      gtk_widget_destroy(ctx->window);
      ctx->window = NULL;
   }

   g_free(ctx->filepath);
   g_free(ctx);
}

static bool save_editor(EditorContext *ctx, const char *filename) {
   if (!ctx || !filename) {
      return false;
   }

   GtkTextIter start, end;

   gtk_text_buffer_get_bounds(ctx->buffer, &start, &end);

   gchar *text =
      gtk_text_buffer_get_text(ctx->buffer, &start, &end, FALSE);

   if (!text) {
      Log(LOG_CRIT, "config", "Unable to get editor contents for %s", filename);

      return false;
   }

   FILE *fp = fopen(filename, "w");

   if (!fp) {
      Log( LOG_CRIT, "config", "Unable to open %s for writing: %s", filename, strerror(errno) );
      g_free(text);

      return false;
   }

   size_t len = strlen(text);
   size_t written = fwrite(text, 1, len, fp);

   if (written != len) {
      Log( LOG_CRIT, "config", "Failed writing %s: %s", filename, strerror(errno) );
      fclose(fp);
      g_free(text);

      return false;
   }

   if (fclose(fp) != 0) {
      Log( LOG_CRIT, "config", "Failed closing %s: %s", filename, strerror(errno) );
      g_free(text);

      return false;
   }

   g_free(text);

   ctx->modified = FALSE;

   Log(LOG_DEBUG, "config", "Edits saved for %s", filename);

   return true;
}

static void on_save_clicked(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }

   EditorContext *ctx = user_data;

   if ( !save_editor(ctx, ctx->filepath) ) {
      return;
   }

   GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(ctx->window), GTK_DIALOG_MODAL,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Reload config from \"%s\"?", ctx->filepath);

   if (gtk_dialog_run( GTK_DIALOG(confirm) ) == GTK_RESPONSE_YES) {
      apply_config(ctx->filepath);
   }

   gtk_widget_destroy(confirm);

   destroy_editor(ctx);
}

static void on_save_other_clicked(GtkButton *btn, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (!ctx) {
      return;
   }

   GtkWidget *dialog = gtk_file_chooser_dialog_new("Save As", GTK_WINDOW(ctx->window), GTK_FILE_CHOOSER_ACTION_SAVE,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);

   gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
   gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "config.cfg");

   if (gtk_dialog_run( GTK_DIALOG(dialog) ) == GTK_RESPONSE_ACCEPT) {
      char *filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );

      if (filename) {
         if ( save_editor(ctx, filename) ) {
            /*
             * Save As becomes the new file being edited.
             */
            g_free(ctx->filepath);
            ctx->filepath = g_strdup(filename);

            gtk_window_set_title(GTK_WINDOW(ctx->window), ctx->filepath);

            GtkWidget *confirm =
               gtk_message_dialog_new(GTK_WINDOW(ctx->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                  GTK_BUTTONS_YES_NO, "Apply new config from \"%s\"?", filename);

            if (gtk_dialog_run( GTK_DIALOG(confirm) ) ==
                GTK_RESPONSE_YES) {
               apply_config(filename);
            }

            gtk_widget_destroy(confirm);
         }

         g_free(filename);
      }
   }

   gtk_widget_destroy(dialog);
}

static void on_discard_clicked(GtkButton *btn, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (!ctx) {
      return;
   }

   if (ctx->modified) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
         GTK_BUTTONS_YES_NO, "You have unsaved changes. Discard them?");

      gboolean cancel = gtk_dialog_run( GTK_DIALOG(dialog) ) != GTK_RESPONSE_YES;

      gtk_widget_destroy(dialog);

      if (cancel) {
         return;
      }
   }

   Log(LOG_DEBUG, "config", "Edit config closed without saving for %s", ctx->filepath);

   destroy_editor(ctx);
}

static gboolean on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (!ctx) {
      return FALSE;
   }

   if (ctx->modified) {
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
         GTK_BUTTONS_YES_NO, "You have unsaved changes. Close anyway?");

      gboolean cancel = gtk_dialog_run( GTK_DIALOG(dialog) ) != GTK_RESPONSE_YES;

      gtk_widget_destroy(dialog);

      if (cancel) {
         return TRUE;
      }
   }

   Log(LOG_DEBUG, "config", "Edit config closed for %s", ctx->filepath);

   /*
    * We handle destruction ourselves so that the window registry and EditorContext are
    * cleaned up together.
    */
   destroy_editor(ctx);

   return TRUE;
}

static void on_reload_config_button(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }

   cfg_reload((const char *)user_data);
}

void gui_edit_config(const char *filepath) {
   if (!filepath) {
      return;
   }

   /*
    * Don't allow multiple config editors.
    */
   gui_window_t *win = gui_find_window(NULL, "editcfg");

   if (win) {
      GtkWidget *cfgedit_window = win->gtk_win;

      if (cfgedit_window) {
         gtk_window_present( GTK_WINDOW(cfgedit_window) );

         return;
      }

      /*
       * Defensive cleanup for a stale registry entry.
       */
      gui_forget_window(win, "editcfg");
   }

   Log(LOG_DEBUG, "gtk.editcfg", "Opening %s for editing", filepath);

   EditorContext *ctx = g_malloc0( sizeof(EditorContext) );
   ctx->filepath = g_strdup(filepath);
   ctx->modified = FALSE;

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   ctx->window = window;
   ctx->window_t = ui_new_window(window, "editcfg");

   gtk_window_set_title(GTK_WINDOW(window), filepath);
   gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);

   GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
   gtk_container_add(GTK_CONTAINER(window), vbox);

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

   GtkWidget *textview = gtk_text_view_new();
   gtk_container_add(GTK_CONTAINER(scrolled), textview);

   ctx->buffer = gtk_text_view_get_buffer( GTK_TEXT_VIEW(textview) );
   g_signal_connect(ctx->buffer, "changed", G_CALLBACK(on_buffer_changed), ctx);

   /*
    * Load the existing configuration.
    */
   FILE *fp = fopen(filepath, "r");

   if (fp) {
      if (fseek(fp, 0, SEEK_END) != 0) {
         Log( LOG_CRIT, "config.edit", "fseek() failed for %s: %s", filepath, strerror(errno) );

         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      long len = ftell(fp);

      if (len < 0) {
         Log( LOG_CRIT, "config.edit", "ftell() failed for %s: %s", filepath, strerror(errno) );

         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      rewind(fp);

      char *buf = malloc( (size_t)len + 1 );

      if (!buf) {
         fprintf(stderr, "OOM in gui_edit_config!\n");
         Log(LOG_CRIT, "config.edit", "OOM reading %s", filepath);
//         abort();
         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      size_t nread = fread(buf, 1, (size_t)len, fp);

      if (nread != (size_t)len) {
         if ( ferror(fp) ) {
            Log( LOG_CRIT, "config.edit", "fread() failed for %s: %s", filepath, strerror(errno) );
         } else {
            Log(LOG_CRIT, "config.edit", "Unexpected EOF reading %s "
               "(%zu/%ld bytes)", filepath, nread, len);
         }

         free(buf);
         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      buf[nread] = '\0';

      /*
       * Config files are limited to a reasonable size, so the gint conversion is safe here.
       */
      gtk_text_buffer_set_text(ctx->buffer, buf, (gint)nread);

      free(buf);
      fclose(fp);

      /*
       * gtk_text_buffer_set_text() emits "changed". Loading the file doesn't count as an edit.
       */
      ctx->modified = FALSE;
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
extern const char *config_file;

static void on_edit_config_button(GtkButton *button, gpointer user_data) {
   (void)button;
   if (user_data != NULL) {
      gui_edit_config(user_data);
   } else {
      gui_edit_config(config_file);
   }
}

static void on_fullscreen_button(GtkButton *button, gpointer user_data) {
   (void)button;
   gui_fullscreen_toggle();
}

static void on_save_config_button(GtkButton *button, gpointer user_data) {
   (void)user_data;
   static char save_path[PATH_MAX];
   const char *home = getenv("HOME");
   snprintf(save_path, sizeof(save_path), "%s/.config/rrclient.cfg",
      (home && *home) ? home : ".");

   GtkWindow *parent = NULL;
   GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(button));
   if (toplevel && GTK_IS_WINDOW(toplevel)) parent = GTK_WINDOW(toplevel);
   char message[PATH_MAX + 128];
   snprintf(message, sizeof(message),
      "Save configuration to \"%s\"?\nThe existing file will be backed up.",
      save_path);
   if (!ui_confirm_dialog(parent, message)) return;

   if (cfg_save(cfg, save_path)) {
      Log(LOG_INFO, "gtk.config", "Configuration saved to %s", save_path);
   }
}

GtkWidget *init_config_tab(void) {
   GtkWidget *nw = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

   GtkWidget *cfg_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(cfg_tab_label), "(<u>2</u>) Config");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, cfg_tab_label);

   GtkWidget *config_label = gtk_label_new("Please be sure to click SAVE CONFIG when done...");
   gtk_box_pack_start(GTK_BOX(nw), config_label, FALSE, FALSE, 12);

   GtkWidget *cfg_panel = cfg_editor_panel();
   gtk_widget_set_vexpand(cfg_panel, TRUE);
   gtk_box_pack_start(GTK_BOX(nw), cfg_panel, TRUE, TRUE, 0);

   GtkWidget *button_grid = gtk_grid_new();
   gtk_grid_set_column_spacing(GTK_GRID(button_grid), 8);
   gtk_grid_set_row_spacing(GTK_GRID(button_grid), 4);
   gtk_container_set_border_width(GTK_CONTAINER(button_grid), 6);
   gtk_grid_set_column_homogeneous(GTK_GRID(button_grid), TRUE);
   gtk_widget_set_halign(button_grid, GTK_ALIGN_START);
   gtk_widget_set_size_request(button_grid, 380, -1);
   gtk_box_pack_start(GTK_BOX(nw), button_grid, FALSE, FALSE, 0);

   GtkWidget *left_buttons = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   GtkWidget *right_buttons = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
   gtk_widget_set_size_request(left_buttons, 180, -1);
   gtk_widget_set_size_request(right_buttons, 180, -1);
   gtk_grid_attach(GTK_GRID(button_grid), left_buttons, 0, 0, 1, 1);
   gtk_grid_attach(GTK_GRID(button_grid), right_buttons, 1, 0, 1, 1);

   GtkWidget *btn_savecfg = gtk_button_new_with_label("Save Config");
   g_signal_connect(btn_savecfg, "clicked", G_CALLBACK(on_save_config_button), NULL);
   gtk_box_pack_start(GTK_BOX(left_buttons), btn_savecfg, FALSE, FALSE, 0);

   GtkWidget *btn_fullscreen = gtk_button_new_with_label("Toggle Fullscreen");
   g_signal_connect(btn_fullscreen, "clicked", G_CALLBACK(on_fullscreen_button), NULL);
   gtk_box_pack_start(GTK_BOX(left_buttons), btn_fullscreen, FALSE, FALSE, 0);

   GtkWidget *btn_cfgedit = gtk_button_new_with_label("Edit Config");
   g_signal_connect(btn_cfgedit, "clicked", G_CALLBACK(on_edit_config_button), (gpointer)config_file);
   gtk_box_pack_start(GTK_BOX(left_buttons), btn_cfgedit, FALSE, FALSE, 0);

   GtkWidget *btn_reloadcfg = gtk_button_new_with_label("Reload Config");
   g_signal_connect(btn_reloadcfg, "clicked", G_CALLBACK(on_reload_config_button), (gpointer)config_file);
   gtk_box_pack_start(GTK_BOX(right_buttons), btn_reloadcfg, FALSE, FALSE, 0);

   toggle_userlist_button = gtk_button_new_with_label("Toggle Userlist");
   gtk_box_pack_start(GTK_BOX(right_buttons), toggle_userlist_button, FALSE, FALSE, 0);
   g_signal_connect(toggle_userlist_button, "clicked", G_CALLBACK(on_toggle_userlist_clicked), NULL);

   GtkWidget *actions[] = { btn_savecfg, btn_fullscreen, btn_cfgedit,
      btn_reloadcfg, toggle_userlist_button };
   for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
      gtk_widget_set_size_request(actions[i], 170, -1);
      gtk_widget_set_halign(actions[i], GTK_ALIGN_CENTER);
   }

   return nw;
}
