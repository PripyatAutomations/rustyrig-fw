#include <rrclient/config.layers.h>
//
// rrclient/gtk/gtk.editcfg.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#define __RRCLI 1
#include <rrclient/database.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <librustyaxe/config.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.floating-panel.h"
extern GtkWidget *main_window;

extern dict *cfg;
extern GtkWidget *main_notebook;
extern dict *cfg_load(const char *path);

GtkWidget *config_tab = NULL;
extern defconfig_t defcfg[];

typedef struct cfg_editor_binding {
   const char *key;
   defconfig_type_t type;
   GtkWidget *widget;
} cfg_editor_binding_t;

static bool cfg_editor_numeric_candidate(GtkEditable *editable, const char *insert, gint position, gint selection_start, gint selection_end, defconfig_type_t
   type) {
   const char *current = gtk_entry_get_text(GTK_ENTRY(editable));
   size_t current_len = strlen(current);
   size_t insert_len = strlen(insert);
   size_t prefix_len = (size_t)(selection_start < 0 ? position : selection_start);
   size_t suffix_start = (size_t)(selection_end < 0 ? position : selection_end);

   if (prefix_len > current_len) {
      prefix_len = current_len;
   }

   if (suffix_start > current_len) {
      suffix_start = current_len;
   }

   if (suffix_start < prefix_len) {
      suffix_start = prefix_len;
   }

   char *candidate = calloc(1, prefix_len + insert_len + (current_len - suffix_start) + 1);

   if (!candidate) {
      return false;
   }
   memcpy(candidate, current, prefix_len);
   memcpy(candidate + prefix_len, insert, insert_len);
   memcpy(candidate + prefix_len + insert_len, current + suffix_start, current_len - suffix_start);

   bool valid = true;
   unsigned dots = 0;

   for (size_t i = 0 ; candidate[i] ; i++) {
      if (candidate[i] >= '0' && candidate[i] <= '9') {
         continue;
      }

      if (candidate[i] == '-' && type != DEFCONFIG_UINT && i == 0) {
         continue;
      }

      if (candidate[i] == '.' && type == DEFCONFIG_FLOAT && dots++ == 0) {
         continue;
      }
      valid = false;
      break;
   }

   free(candidate);

   return valid;
}

static void cfg_editor_numeric_insert(GtkEditable *editable, gchar *insert, gint length, gint *position, gpointer user_data) {
   (void)length;
   cfg_editor_binding_t *binding = (cfg_editor_binding_t *) user_data;

   if (!binding || !insert || !position) {
      return;
   }
   gint selection_start = -1, selection_end = -1;
   gtk_editable_get_selection_bounds(editable, &selection_start, &selection_end);

   if (!cfg_editor_numeric_candidate(editable, insert, *position, selection_start, selection_end, binding->type)) {
      g_signal_stop_emission_by_name(editable, "insert-text");
   }
}

static void cfg_editor_changed(GtkWidget *widget, gpointer user_data) {
   cfg_editor_binding_t *binding = (cfg_editor_binding_t *) user_data;

   if (!binding || !binding->key) {
      return;
   }
   char value[512] = "";

   switch (binding->type) {
      case DEFCONFIG_BOOL: {
         snprintf(value, sizeof(value), "%s", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? "true" : "false");
         break;
      }
      case DEFCONFIG_INT:
      case DEFCONFIG_UINT:
      case DEFCONFIG_FLOAT: {
         snprintf(value, sizeof(value), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
         break;
      }
      default: {
         if (GTK_IS_COMBO_BOX_TEXT(widget)) {
            const char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));
            snprintf(value, sizeof(value), "%s", text ? text : "");
            g_free((gpointer) text);
         } else {
            snprintf(value, sizeof(value), "%s", gtk_entry_get_text(GTK_ENTRY(widget)));
         }
         break;
      }
   }

   if (!cfg_set_value(binding->key, value)) {
      Log(LOG_WARN, "gtk.config", "Rejected value for %s", binding->key);
   }
}

static GtkWidget *cfg_editor_widget(const defconfig_t *def, const char *value, cfg_editor_binding_t **binding_out) {
   GtkWidget *widget = NULL;

   if (def->type == DEFCONFIG_BOOL && !strcmp(def->key, "media.direct.prefer")) {
      widget = gtk_check_button_new_with_label("Prefer direct RTP audio");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), value && (!strcasecmp(value, "true") || !strcasecmp(value, "yes") || !strcmp(value, "1")));
   } else if (def->type == DEFCONFIG_BOOL) {
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      GtkWidget *on = gtk_radio_button_new_with_label(NULL, "true");
      GtkWidget *off = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(on), "false");
      bool enabled = value && (!strcasecmp(value, "true") || !strcasecmp(value, "yes") || !strcmp(value, "1"));
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled ? on : off), TRUE);
      gtk_box_pack_start(GTK_BOX(box), on, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), off, FALSE, FALSE, 0);
      widget = box;
   } else if (def->type == DEFCONFIG_ENUM && def->choices && *def->choices) {
      widget = gtk_combo_box_text_new();
      char *choices = strdup(def->choices), *save = NULL;
      int active = 0, selected = -1;

      for (char *p = strtok_r(choices, "|", &save) ; p ; p = strtok_r(NULL, "|", &save), active++) {
         gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), p);

         if (value && !strcasecmp(p, value)) {
            selected = active;
         }
      }

      free(choices);
      gtk_combo_box_set_active(GTK_COMBO_BOX(widget), selected >= 0 ? selected : 0);
   } else {
      widget = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(widget), value ? value : "");
      gtk_entry_set_visibility(GTK_ENTRY(widget), def->type != DEFCONFIG_PASSWORD);
      g_signal_connect(widget, "changed", G_CALLBACK(cfg_editor_changed), NULL);
   }

   if (!widget) {
      return NULL;
   }
   cfg_editor_binding_t *binding = calloc(1, sizeof(*binding));

   if (!binding) {
      gtk_widget_destroy(widget);

      return NULL;
   }
   binding->key = def->key;
   binding->type = def->type;
   binding->widget = widget;
   g_object_set_data_full(G_OBJECT(widget), "rr-cfg-binding", binding, free);

   if (GTK_IS_CHECK_BUTTON(widget)) {
      g_signal_connect(widget, "toggled", G_CALLBACK(cfg_editor_changed), binding);
   } else if (def->type == DEFCONFIG_BOOL) {
      GList *children = gtk_container_get_children(GTK_CONTAINER(widget));

      for (GList *it = children ; it ; it = it->next) {
         g_signal_connect(it->data, "toggled", G_CALLBACK(cfg_editor_changed), binding);
      }

      g_list_free(children);
   } else if (def->type == DEFCONFIG_ENUM && def->choices && *def->choices) {
      g_signal_connect(widget, "changed", G_CALLBACK(cfg_editor_changed), binding);
   } else {
      if (def->type == DEFCONFIG_INT || def->type == DEFCONFIG_UINT || def->type == DEFCONFIG_FLOAT) {
         g_signal_connect(widget, "insert-text", G_CALLBACK(cfg_editor_numeric_insert), binding);
      }
      g_signal_connect(widget, "changed", G_CALLBACK(cfg_editor_changed), binding);
   }

   if (binding_out) {
      *binding_out = binding;
   }

   return widget;
}

static int cfg_editor_defconfig_compare(const void *left, const void *right) {
   const defconfig_t *a = *(const defconfig_t * const *)left;
   const defconfig_t *b = *(const defconfig_t * const *)right;
   bool a_section = strchr(a->key, ':') != NULL;
   bool b_section = strchr(b->key, ':') != NULL;

   if (a_section != b_section) {
      return a_section ? 1 : -1;
   }

   return strcmp(a->key, b->key);
}

/* Native GtkListBox filtering/sorting keeps editor widgets in their rows. Cached casefolded text avoids repeatedly converting hundreds of keys. */
typedef struct {
   GtkWidget *list;
   char *query;
   char mode;
   guint pending;
} cfg_list_state_t;
/* The Config editor uses GtkListBoxRow, not cell renderers. Style the visible rows after each filter or sort so the stripes stay alternating. */
static void cfg_list_stripe(GtkListBox *list) {
   GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
   guint index = 0;

   for (GList *l = rows ; l ; l = l->next) {
      GtkWidget *row = l->data;
      GtkStyleContext *style = gtk_widget_get_style_context(row);
      gtk_style_context_remove_class(style, "rr-zebra-odd");

      if (gtk_widget_get_child_visible(row) && (index++ & 1)) {
         gtk_style_context_add_class(style, "rr-zebra-odd");
      }
   }

   g_list_free(rows);
}
static void cfg_list_apply_stripe(GtkListBox *list) {
   gtk_widget_queue_draw(GTK_WIDGET(list));
   cfg_list_stripe(list);
}

static int cfg_sort_column = 0;
static gboolean cfg_sort_descending = FALSE;
static gboolean cfg_list_filter(GtkListBoxRow *row, gpointer data) {
   cfg_list_state_t *state = data;

   if (!state->query || !*state->query) {
      return TRUE;
   }
   const char *key = g_object_get_data(G_OBJECT(row), "cfg-key-fold");
   const char *help = g_object_get_data(G_OBJECT(row), "cfg-help-fold");

   if (state->mode == '^') {
      return key && g_str_has_prefix(key, state->query);
   }

   return (key && strstr(key, state->query)) || (state->mode != '=' && help && strstr(help, state->query));
}
static gint cfg_list_compare(GtkListBoxRow *left, GtkListBoxRow *right, gpointer unused) {
   (void)unused;
   const defconfig_t *a = g_object_get_data(G_OBJECT(left), "rr-cfg-definition");
   const defconfig_t *b = g_object_get_data(G_OBJECT(right), "rr-cfg-definition");

   if (!a || !b) {
      return 0;
   }
   const char *av = cfg_sort_column == 2 ? (a->help ? a->help : "") : cfg_sort_column == 1 ? cfg_get(a->key) : a->key;
   const char *bv = cfg_sort_column == 2 ? (b->help ? b->help : "") : cfg_sort_column == 1 ? cfg_get(b->key) : b->key;
   gint cmp = g_utf8_collate(av ? av : "", bv ? bv : "");

   if (!cmp) {
      cmp = g_strcmp0(a->key, b->key);
   }

   return cfg_sort_descending ? -cmp : cmp;
}
static void cfg_list_sort_clicked(GtkButton *button, gpointer list) {
   int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "cfg-sort-column"));

   if (column == cfg_sort_column) {
      cfg_sort_descending = !cfg_sort_descending;
   } else {
      cfg_sort_column = column;
      cfg_sort_descending = FALSE;
   }
   gtk_list_box_invalidate_sort(GTK_LIST_BOX(list));
   cfg_list_apply_stripe(GTK_LIST_BOX(list));
}
static gboolean cfg_list_filter_deferred(gpointer data) {
   cfg_list_state_t *state = data;
   state->pending = 0;
   gtk_list_box_invalidate_filter(GTK_LIST_BOX(state->list));
   cfg_list_apply_stripe(GTK_LIST_BOX(state->list));

   return G_SOURCE_REMOVE;
}
static void cfg_list_state_free(gpointer data) {
   cfg_list_state_t *state = data;

   if (state->pending) {
      g_source_remove(state->pending);
   }
   g_free(state->query);
   g_free(state);
}
static void cfg_editor_filter_changed(GtkEditable *search, gpointer user_data) {
   cfg_list_state_t *state = user_data;
   const char *query = gtk_entry_get_text(GTK_ENTRY(search));
   state->mode = (*query == '=' || *query == '^') ? *query++ : '\0';
   g_free(state->query);
   state->query = g_utf8_casefold(query, -1);

   if (state->pending) {
      g_source_remove(state->pending);
   }
   state->pending = g_timeout_add(100, cfg_list_filter_deferred, state);
}
static GtkWidget *cfg_editor_panel(GtkWidget **list_out) {
   GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(header), 3);
   const char *headings[] = {
      "Setting", "Value", "Description"
   };
   GtkWidget *list = gtk_list_box_new();
   *list_out = list;
   gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
   GtkCssProvider *stripe = gtk_css_provider_new();
   gtk_css_provider_load_from_data(stripe, "listbox row.rr-zebra-odd { background-color: alpha(currentColor, 0.085); }", -1, NULL);
   gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(list), GTK_STYLE_PROVIDER(stripe), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
   g_object_unref(stripe);
   gtk_list_box_set_sort_func(GTK_LIST_BOX(list), cfg_list_compare, NULL, NULL);
   cfg_list_state_t *state = g_new0(cfg_list_state_t, 1);
   state->list = list;
   g_object_set_data_full(G_OBJECT(container), "cfg-list-state", state, cfg_list_state_free);
   gtk_list_box_set_filter_func(GTK_LIST_BOX(list), cfg_list_filter, state, NULL);

   for (int col = 0 ; col < 3 ; col++) {
      GtkWidget *button = gtk_button_new_with_label(headings[col]);
      g_object_set_data(G_OBJECT(button), "cfg-sort-column", GINT_TO_POINTER(col));

      if (col == 0) {
         gtk_widget_set_size_request(button, 220, -1);
      } else if (col == 1) {
         gtk_widget_set_size_request(button, 250, -1);
      } else {
         gtk_widget_set_hexpand(button, TRUE);
      }
      g_signal_connect(button, "clicked", G_CALLBACK(cfg_list_sort_clicked), list);
      gtk_box_pack_start(GTK_BOX(header), button, col == 2, col == 2, 0);
   }

   gtk_box_pack_start(GTK_BOX(container), header, FALSE, FALSE, 0);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
   gtk_widget_set_vexpand(scroll, TRUE);
   gtk_container_add(GTK_CONTAINER(scroll), list);
   gtk_box_pack_start(GTK_BOX(container), scroll, TRUE, TRUE, 0);

   for (size_t i = 0 ; defcfg[i].key ; i++) {
      const defconfig_t *def = &defcfg[i];
      const char *value = cfg_get(def->key);
      cfg_editor_binding_t *binding = NULL;
      GtkWidget *editor = cfg_editor_widget(def, value ? value : def->val, &binding);

      if (!editor) {
         continue;
      }
      GtkWidget *row = gtk_list_box_row_new();
      GtkWidget *cells = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
      gtk_container_set_border_width(GTK_CONTAINER(cells), 3);
      GtkWidget *label = gtk_label_new(def->key);
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
      gtk_widget_set_size_request(label, 220, -1);
      gtk_widget_set_hexpand(label, FALSE);
      gtk_widget_set_tooltip_text(label, def->key);
      gtk_widget_set_tooltip_text(label, def->help ? def->help : "");
      gtk_box_pack_start(GTK_BOX(cells), label, FALSE, FALSE, 0);
      gtk_widget_set_size_request(editor, 250, -1);
      gtk_widget_set_hexpand(editor, FALSE);
      gtk_box_pack_start(GTK_BOX(cells), editor, FALSE, FALSE, 0);
      GtkWidget *help = gtk_label_new(def->help ? def->help : "");
      gtk_label_set_xalign(GTK_LABEL(help), 0);
      gtk_label_set_ellipsize(GTK_LABEL(help), PANGO_ELLIPSIZE_END);
      gtk_widget_set_tooltip_text(help, def->help ? def->help : "");
      gtk_widget_set_hexpand(help, TRUE);
      gtk_box_pack_start(GTK_BOX(cells), help, TRUE, TRUE, 0);
      gtk_container_add(GTK_CONTAINER(row), cells);
      g_object_set_data(G_OBJECT(row), "rr-cfg-definition", (gpointer) def);
      g_object_set_data_full(G_OBJECT(row), "cfg-key-fold", g_utf8_casefold(def->key, -1), g_free);
      g_object_set_data_full(G_OBJECT(row), "cfg-help-fold", g_utf8_casefold(def->help ? def->help : "", -1), g_free);
      gtk_container_add(GTK_CONTAINER(list), row);
   }

   cfg_list_apply_stripe(GTK_LIST_BOX(list));

   return container;
}

typedef struct {
   GtkWidget *window;
   RRFloatPanel *panel, *confirmation;
   GtkTextBuffer *buffer;
   gchar *filepath;
   gboolean modified;
   gboolean config_edit;
   bool (*remote_save)(const char *, void *);
   void *remote_data;
   GDestroyNotify remote_destroy;
   GtkWidget *save_as, *save_button;
   bool remote_pending, destroying;
   rr_event_token_t remote_token;
} EditorContext;
static EditorContext *active_editor;

static void on_buffer_changed(GtkTextBuffer *buffer, gpointer user_data) {
   if (!user_data) {
      return;
   }

   ((EditorContext *) user_data)->modified = TRUE;
}

static void apply_config(const char *filename) {
   if (!filename) {
      return;
   }

   rrclient_config_reload(filename);
}

static void destroy_editor(EditorContext *ctx) {
   if (!ctx) {
      return;
   }

   ctx->destroying = true;

   if (ctx->confirmation) {
      RRFloatPanel *p = ctx->confirmation;
      ctx->confirmation = NULL;
      rr_floating_panel_destroy(p);
   }

   if (active_editor == ctx) {
      active_editor = NULL;
   }

   if (ctx->panel) {
      RRFloatPanel *panel = ctx->panel;
      ctx->panel = NULL;
      ctx->window = NULL;
      rr_floating_panel_destroy(panel);
   }

   if (ctx->buffer) {
      g_signal_handlers_disconnect_by_data(ctx->buffer, ctx);
      g_object_unref(ctx->buffer);
      ctx->buffer = NULL;
   }

   if (ctx->remote_token) {
      event_off_token(ctx->remote_token);
   }

   if (ctx->remote_save) {
      unlink(ctx->filepath);
   }

   if (ctx->remote_destroy) {
      ctx->remote_destroy(ctx->remote_data);
   }
   g_free(ctx->filepath);
   g_free(ctx);
}

/* External window destruction must also release the remote event listener. */
static void editor_destroyed(GtkWidget *window, gpointer data) {
   EditorContext *ctx = data;
   ctx->window = NULL;

   if (!ctx->destroying) {
      destroy_editor(ctx);
   }
}
void gtk_file_editor_shutdown(void) {
   if (active_editor) {
      destroy_editor(active_editor);
   }
}

static bool save_editor(EditorContext *ctx, const char *filename) {
   if (!ctx || !filename) {
      return false;
   }

   GtkTextIter start, end;

   gtk_text_buffer_get_bounds(ctx->buffer, &start, &end);

   gchar *text = gtk_text_buffer_get_text(ctx->buffer, &start, &end, FALSE);

   if (!text) {
      Log(LOG_CRIT, "config", "Unable to get editor contents for %s", filename);

      return false;
   }

   FILE *fp = fopen(filename, "w");

   if (!fp) {
      Log(LOG_CRIT, "config", "Unable to open %s for writing: %s", filename, strerror(errno));
      g_free(text);

      return false;
   }

   size_t len = strlen(text);
   size_t written = fwrite(text, 1, len, fp);

   if (written != len) {
      Log(LOG_CRIT, "config", "Failed writing %s: %s", filename, strerror(errno));
      fclose(fp);
      g_free(text);

      return false;
   }

   if (fclose(fp) != 0) {
      Log(LOG_CRIT, "config", "Failed closing %s: %s", filename, strerror(errno));
      g_free(text);

      return false;
   }

   g_free(text);

   ctx->modified = FALSE;

   Log(LOG_DEBUG, "config", "Edits saved for %s", filename);

   return true;
}

typedef enum {
   FILE_DISCARD, FILE_RELOAD, FILE_APPLY
} FileConfirmAction;
static void file_confirm_response(RRFloatPanel *panel, gint response, gpointer data) {
   EditorContext *ctx = data;
   FileConfirmAction action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(rr_floating_panel_widget(panel)), "action"));
   ctx->confirmation = NULL;
   rr_floating_panel_destroy(panel);

   if (action == FILE_DISCARD && response != GTK_RESPONSE_YES) {
      return;
   }

   if (response == GTK_RESPONSE_YES && action != FILE_DISCARD) {
      apply_config(ctx->filepath);
   }

   if (action != FILE_APPLY) {
      destroy_editor(ctx);
   }
}
static void file_editor_confirm(EditorContext *ctx, FileConfirmAction action, const char *message) {
   if (!ctx || ctx->destroying || ctx->confirmation) {
      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }
   RRFloatPanel *panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "file-editor-confirm", "File Editor — Confirmation");

   if (!panel) {
      return;
   }
   ctx->confirmation = panel;
   g_object_set_data(G_OBJECT(rr_floating_panel_widget(panel)), "action", GINT_TO_POINTER(action));
   GtkWidget *body = rr_floating_panel_body(panel);
   GtkWidget *label = gtk_label_new(message);
   gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
   gtk_label_set_xalign(GTK_LABEL(label), 0);
   gtk_container_set_border_width(GTK_CONTAINER(body), 12);
   gtk_box_pack_start(GTK_BOX(body), label, FALSE, FALSE, 8);
   GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_box_pack_start(GTK_BOX(body), actions, FALSE, FALSE, 4);
   rr_floating_panel_add_response(panel, actions, action == FILE_DISCARD ? "Discard" : "Yes", GTK_RESPONSE_YES);
   rr_floating_panel_add_response(panel, actions, "Cancel", GTK_RESPONSE_CANCEL);
   rr_floating_panel_set_response_callback(panel, file_confirm_response, ctx);
   rr_floating_panel_set_modal(panel, TRUE);
   rr_floating_panel_show(panel);
}

static void on_save_clicked(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }

   EditorContext *ctx = user_data;

   if (ctx->remote_pending) {
      return;
   }

   if (!save_editor(ctx, ctx->filepath)) {
      return;
   }

   if (ctx->remote_save) {
      ctx->remote_pending = ctx->remote_save(ctx->filepath, ctx->remote_data);
      ctx->modified = !ctx->remote_pending;
      gtk_widget_set_sensitive(ctx->save_button, !ctx->remote_pending);

      return;
   }

   if (!ctx->config_edit) {
      destroy_editor(ctx);

      return;
   }
   char *message = g_strdup_printf("Reload configuration from \"%s\"?", ctx->filepath);
   file_editor_confirm(ctx, FILE_RELOAD, message);
   g_free(message);
}

static void on_save_other_clicked(GtkButton *btn, gpointer user_data) {
   EditorContext *ctx = user_data;

   if (!ctx) {
      return;
   }

   GtkWidget *dialog = gtk_file_chooser_dialog_new("Save As", GTK_WINDOW(main_window), GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL, "_Save",
      GTK_RESPONSE_ACCEPT, NULL);

   gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);
   gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "config.cfg");

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

      if (filename) {
         if (save_editor(ctx, filename)) {
            /*
             * Save As becomes the new file being edited.
             */
            g_free(ctx->filepath);
            ctx->filepath = g_strdup(filename);

            char *new_title = g_strdup_printf("File Editor — %s", ctx->filepath);
            rr_floating_panel_set_title(ctx->panel, new_title);
            g_free(new_title);

            if (ctx->config_edit) {
               char *message = g_strdup_printf("Apply configuration from \"%s\"?", filename);
               file_editor_confirm(ctx, FILE_APPLY, message);
               g_free(message);
            }

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
      file_editor_confirm(ctx, FILE_DISCARD, "You have unsaved changes. Discard them?");

      return;
   }

   Log(LOG_DEBUG, "config", "Edit config closed without saving for %s", ctx->filepath);

   destroy_editor(ctx);
}

static void file_editor_close(gpointer data) {
   on_discard_clicked(NULL, data);
}

static void on_reload_config_button(GtkButton *btn, gpointer user_data) {
   if (!user_data) {
      return;
   }

   rrclient_config_reload((const char *)user_data);
}

void gui_file_editor_open(const char *filepath) {
   if (!filepath) {
      return;
   }

   if (active_editor) {
      rr_floating_panel_restore(active_editor->panel);
      rr_floating_panel_show(active_editor->panel);

      return;
   }
   GtkWidget *root = main_window ? gtk_bin_get_child(GTK_BIN(main_window)) : NULL;

   if (!GTK_IS_OVERLAY(root) || !GTK_IS_NOTEBOOK(main_notebook)) {
      return;
   }

   Log(LOG_DEBUG, "gtk.file-editor", "Opening %s for editing", filepath);

   EditorContext *ctx = g_malloc0(sizeof(EditorContext));
   active_editor = ctx;
   ctx->filepath = g_strdup(filepath);
   ctx->modified = FALSE;

   ctx->panel = rr_floating_panel_new(GTK_OVERLAY(root), GTK_NOTEBOOK(main_notebook), "file-editor", "File Editor");

   if (!ctx->panel) {
      active_editor = NULL;
      g_free(ctx->filepath);
      g_free(ctx);

      return;
   }
   GtkWidget *window = rr_floating_panel_widget(ctx->panel);
   ctx->window = window;
   g_object_set_data(G_OBJECT(window), "rr-editor-context", ctx);
   g_signal_connect(window, "destroy", G_CALLBACK(editor_destroyed), ctx);
   rr_floating_panel_set_close_callback(ctx->panel, file_editor_close, ctx);
   char *panel_title = g_strdup_printf("File Editor — %s", filepath);
   rr_floating_panel_set_title(ctx->panel, panel_title);
   g_free(panel_title);
   GtkWidget *vbox = rr_floating_panel_body(ctx->panel);
   gtk_widget_set_size_request(vbox, 640, 480);

   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);

   GtkWidget *textview = gtk_text_view_new();
   gtk_container_add(GTK_CONTAINER(scrolled), textview);

   ctx->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
   g_object_ref(ctx->buffer);
   g_signal_connect(ctx->buffer, "changed", G_CALLBACK(on_buffer_changed), ctx);

   /*
    * Load the existing configuration.
    */
   FILE *fp = fopen(filepath, "r");

   if (fp) {
      if (fseek(fp, 0, SEEK_END) != 0) {
         Log(LOG_CRIT, "config.edit", "fseek() failed for %s: %s", filepath, strerror(errno));

         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      long len = ftell(fp);

      if (len < 0) {
         Log(LOG_CRIT, "config.edit", "ftell() failed for %s: %s", filepath, strerror(errno));

         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      rewind(fp);

      char *buf = malloc((size_t)len + 1);

      if (!buf) {
         fprintf(stderr, "OOM in gui_file_editor_open!\n");
         Log(LOG_CRIT, "config.edit", "OOM reading %s", filepath);
//         abort();
         fclose(fp);
         destroy_editor(ctx);

         return;
      }

      size_t nread = fread(buf, 1, (size_t)len, fp);

      if (nread != (size_t)len) {
         if (ferror(fp)) {
            Log(LOG_CRIT, "config.edit", "fread() failed for %s: %s", filepath, strerror(errno));
         } else {
            Log(LOG_CRIT, "config.edit", "Unexpected EOF reading %s " "(%zu/%ld bytes)", filepath, nread, len);
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
      gtk_text_buffer_set_text(ctx->buffer, buf, (gint) nread);

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
   ctx->save_button = btn_save;
   GtkWidget *btn_save_as = gtk_button_new_with_label("Save As");
   ctx->save_as = btn_save_as;
   GtkWidget *btn_discard = gtk_button_new_with_label("Discard");

   gtk_box_pack_end(GTK_BOX(hbox), btn_discard, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(hbox), btn_save_as, FALSE, FALSE, 0);
   gtk_box_pack_end(GTK_BOX(hbox), btn_save, FALSE, FALSE, 0);

   g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), ctx);
   g_signal_connect(btn_save_as, "clicked", G_CALLBACK(on_save_other_clicked), ctx);
   g_signal_connect(btn_discard, "clicked", G_CALLBACK(on_discard_clicked), ctx);
   rr_floating_panel_show(ctx->panel);
}

void gui_file_editor_open_config(const char *filepath) {
   if (active_editor) {
      gui_file_editor_open(filepath);

      return;
   }
   gui_file_editor_open(filepath);

   if (active_editor) {
      active_editor->config_edit = TRUE;
   }
}

/////////////////////////////
// Config tab in tab strip //
/////////////////////////////
extern const char *config_file;

static void on_edit_config_button(GtkButton *button, gpointer user_data) {
   (void)button;

   if (user_data != NULL) {
      gui_file_editor_open_config(user_data);
   } else {
      const char *home = g_get_home_dir();
      char *dir = g_build_filename(home, ".config", NULL);
      char *path = g_build_filename(dir, "rrclient.cfg", NULL);

      if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
         if (g_mkdir_with_parents(dir, 0700) != 0 || !rrclient_database_config_save(path)) {
            Log(LOG_WARN, "gtk.config", "Cannot initialize %s from active configuration: %s", path, strerror(errno));
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
               GTK_BUTTONS_CLOSE, "Unable to create %s from the active configuration.", path);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            g_free(path);
            g_free(dir);

            return;
         }
         chmod(path, 0600);
         Log(LOG_INFO, "gtk.config", "Created %s from active configuration", path);
      }
      gui_file_editor_open_config(path);
      g_free(path);
      g_free(dir);
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
   snprintf(save_path, sizeof(save_path), "%s/.config/rrclient.cfg", (home && *home) ? home : ".");

   GtkWindow *parent = NULL;
   GtkWidget *toplevel = gtk_widget_get_toplevel(GTK_WIDGET(button));

   if (toplevel && GTK_IS_WINDOW(toplevel)) {
      parent = GTK_WINDOW(toplevel);
   }
   char message[PATH_MAX + 128];
   snprintf(message, sizeof(message), "Save configuration to \"%s\"?\nThe existing file will be backed up.", save_path);

   if (!ui_confirm_dialog(parent, message)) {
      return;
   }

   if (rrclient_database_config_save(save_path)) {
      Log(LOG_INFO, "gtk.config", "Configuration saved to %s", save_path);
   }
}


/* Theme controls edit the active configuration, not a second shadow copy. */
typedef struct theme_field {
   const defconfig_t *definition;
   GtkWidget *value;
} theme_field_t;

static void theme_field_free(gpointer data) { g_free(data); }

static void theme_field_changed(GtkEditable *editable, gpointer data) {
   theme_field_t *field = data;
   cfg_set_value(field->definition->key, gtk_entry_get_text(GTK_ENTRY(editable)));
}

static void theme_field_reset(GtkButton *button, gpointer data) {
   (void)button;
   theme_field_t *field = data;
   gtk_entry_set_text(GTK_ENTRY(field->value), field->definition->val ? field->definition->val : "");
}

static void theme_field_pick(GtkButton *button, gpointer data) {
   theme_field_t *field = data;
   GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_color_chooser_dialog_new("Theme color", GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL);
   /* IRC hexadecimal color syntax is also accepted by the chat renderer. */
   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
      GdkRGBA chosen;
      gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog), &chosen);
      char encoded[12];
      snprintf(encoded, sizeof(encoded), "\004%02X%02X%02X",
         (unsigned)(chosen.red * 255.0 + .5), (unsigned)(chosen.green * 255.0 + .5),
         (unsigned)(chosen.blue * 255.0 + .5));
      gtk_entry_set_text(GTK_ENTRY(field->value), encoded);
   }
   gtk_widget_destroy(dialog);
}

static void theme_selected(GtkComboBox *combo, gpointer data) {
   GtkWidget *list = GTK_WIDGET(data);
   const char *name = gtk_combo_box_get_active_id(combo);
   if (!name || !cfg_set_value("theme.active", name)) return;
   rrclient_theme_apply();
   GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
   for (GList *row = rows; row; row = row->next) {
      theme_field_t *field = g_object_get_data(G_OBJECT(row->data), "theme-field");
      if (!field) continue;
      const char *val = cfg_get(field->definition->key);
      gtk_entry_set_text(GTK_ENTRY(field->value), val ? val : "");
   }
   g_list_free(rows);
}

static GtkWidget *cfg_theme_panel(void) {
   GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(container), 8);
   GtkWidget *description = gtk_label_new("Live theme settings (stored in the active configuration). Save Config to persist changes.");
   gtk_label_set_xalign(GTK_LABEL(description), 0);
   gtk_box_pack_start(GTK_BOX(container), description, FALSE, FALSE, 0);
   GtkWidget *chooser_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_box_pack_start(GTK_BOX(container), chooser_row, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(chooser_row), gtk_label_new("Theme:"), FALSE, FALSE, 0);
   GtkWidget *chooser = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(chooser), "Stock", "Stock");
   const char *key;
   char *value;
   int index = 0;
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   while ((index = dict_enumerate(cfg, index, &key, &value)) >= 0) {
      if (!g_str_has_prefix(key, "theme:")) continue;
      const char *start = key + strlen("theme:");
      const char *dot = strchr(start, '.');
      if (!dot || dot == start) continue;
      char *name = g_strndup(start, dot - start);
      if (g_hash_table_add(seen, name))
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(chooser), name, name);
   }
   g_hash_table_destroy(seen);
   const char *selected = cfg_get("theme.active");
   if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(chooser), selected ? selected : "Stock"))
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(chooser), "Stock");
   gtk_box_pack_start(GTK_BOX(chooser_row), chooser, FALSE, FALSE, 0);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_box_pack_start(GTK_BOX(container), scroll, TRUE, TRUE, 0);
   GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_add(GTK_CONTAINER(scroll), list);
   g_signal_connect(chooser, "changed", G_CALLBACK(theme_selected), list);
   for (const defconfig_t *def = defcfg; def->key; ++def) {
      if (!g_str_has_prefix(def->key, "theme.") || !strcmp(def->key, "theme.active")) continue;
      theme_field_t *field = g_new0(theme_field_t, 1);
      field->definition = def;
      GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
      gtk_box_pack_start(GTK_BOX(list), row, FALSE, FALSE, 0);
      GtkWidget *label = gtk_label_new(def->key);
      gtk_widget_set_size_request(label, 200, -1);
      gtk_label_set_xalign(GTK_LABEL(label), 0);
      gtk_widget_set_tooltip_text(label, def->help);
      gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
      field->value = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(field->value), cfg_get(def->key) ? cfg_get(def->key) : "");
      gtk_widget_set_hexpand(field->value, TRUE);
      gtk_box_pack_start(GTK_BOX(row), field->value, TRUE, TRUE, 0);
      g_object_set_data_full(G_OBJECT(row), "theme-field", field, theme_field_free);
      GtkWidget *pick = gtk_button_new_with_label("Palette…");
      g_signal_connect(pick, "clicked", G_CALLBACK(theme_field_pick), field);
      gtk_box_pack_start(GTK_BOX(row), pick, FALSE, FALSE, 0);
      GtkWidget *reset = gtk_button_new_with_label("Reset");
      g_signal_connect(reset, "clicked", G_CALLBACK(theme_field_reset), field);
      gtk_box_pack_start(GTK_BOX(row), reset, FALSE, FALSE, 0);
      g_signal_connect(field->value, "changed", G_CALLBACK(theme_field_changed), field);
   }
   return container;
}

GtkWidget *init_config_tab(void) {
   GtkWidget *nw = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

   GtkWidget *cfg_tab_label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(cfg_tab_label), "(<u>2</u>) Config");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), nw, cfg_tab_label);

   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(header), 6);
   gtk_box_pack_start(GTK_BOX(nw), header, FALSE, FALSE, 6);
   gtk_box_pack_start(GTK_BOX(header), gtk_label_new("Filter:"), FALSE, FALSE, 0);
   GtkWidget *search = gtk_search_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(search), 20);
   gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Filter config… (=key, ^prefix)");
   gtk_widget_set_tooltip_text(search,
      "Filter keys and descriptions (case-insensitive). =audio: keys containing audio; ^debug: keys starting with debug. Clear to show all fields.");
   gtk_box_pack_start(GTK_BOX(header), search, FALSE, FALSE, 0);
   GtkWidget *config_label = gtk_label_new("Please be sure to click SAVE CONFIG when done...");
   gtk_box_pack_start(GTK_BOX(header), config_label, FALSE, FALSE, 0);

   GtkWidget *cfg_list;
   GtkWidget *cfg_panel = cfg_editor_panel(&cfg_list);
   cfg_list_state_t *filter_state = g_object_get_data(G_OBJECT(cfg_panel), "cfg-list-state");
   g_signal_connect(search, "changed", G_CALLBACK(cfg_editor_filter_changed), filter_state);
   gtk_widget_set_vexpand(cfg_panel, TRUE);
   GtkWidget *sections = gtk_notebook_new();
   gtk_widget_set_vexpand(sections, TRUE);
   gtk_box_pack_start(GTK_BOX(nw), sections, TRUE, TRUE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(sections), cfg_panel, gtk_label_new("Settings"));
   gtk_notebook_append_page(GTK_NOTEBOOK(sections), cfg_theme_panel(), gtk_label_new("Theme"));

   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(buttons), 6);
   gtk_widget_set_halign(buttons, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(nw), buttons, FALSE, FALSE, 0);

   GtkWidget *btn_savecfg = gtk_button_new_with_label("Save Config");
   g_signal_connect(btn_savecfg, "clicked", G_CALLBACK(on_save_config_button), NULL);
   gtk_box_pack_start(GTK_BOX(buttons), btn_savecfg, FALSE, FALSE, 0);

   GtkWidget *btn_fullscreen = gtk_button_new_with_label("Toggle Fullscreen");
   g_signal_connect(btn_fullscreen, "clicked", G_CALLBACK(on_fullscreen_button), NULL);
   gtk_box_pack_start(GTK_BOX(buttons), btn_fullscreen, FALSE, FALSE, 0);

   GtkWidget *btn_cfgedit = gtk_button_new_with_label("Edit Config");
   g_signal_connect(btn_cfgedit, "clicked", G_CALLBACK(on_edit_config_button), (gpointer) config_file);
   gtk_box_pack_start(GTK_BOX(buttons), btn_cfgedit, FALSE, FALSE, 0);

   GtkWidget *btn_reloadcfg = gtk_button_new_with_label("Reload Config");
   g_signal_connect(btn_reloadcfg, "clicked", G_CALLBACK(on_reload_config_button), (gpointer) config_file);
   gtk_box_pack_start(GTK_BOX(buttons), btn_reloadcfg, FALSE, FALSE, 0);

   /* One empty text row separates destructive/config actions from main tabs. */
   GtkWidget *spacer = gtk_label_new(" ");
   gtk_widget_set_sensitive(spacer, FALSE);
   gtk_box_pack_start(GTK_BOX(nw), spacer, FALSE, FALSE, 0);

   return nw;
}

static void remote_edit_complete(const char *event, const char *text, rrconn_t *client, void *data) {
   EditorContext *ctx = data;
   dict *d = json2dict(text);

   if (!d) {
      return;
   }

   if (!strcmp(dict_get(d, "file.local", ""), ctx->filepath)) {
      ctx->remote_pending = false;
      gtk_widget_set_sensitive(ctx->save_button, TRUE);
      const char *error = dict_get(d, "file.error", "");

      if (*error) {
         ctx->modified = TRUE;
         GtkWidget *dialog =
            gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Remote save failed: %s",
            error);
         g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
         gtk_widget_show(dialog);
      }
   }
   dict_free(d);
}

/* Reuse the text editor, but saving publishes to the remote share and never applies that configuration to the local client. Caller owns data on failure. */
bool gui_file_editor_open_remote(const char *local, const char *title, bool (*save) (const char *, void *), void *data, GDestroyNotify destroy) {
   if (active_editor) {
      return false;
   }
   struct stat st;

   if (stat(local, &st) || !S_ISREG(st.st_mode) || st.st_size > 1048576) {
      return false;
   }
   gchar *text = NULL;
   gsize size;

   if (!g_file_get_contents(local, &text, &size, NULL)) {
      return false;
   }
   bool valid = g_utf8_validate(text, size, NULL) && !memchr(text, 0, size);
   g_free(text);

   if (!valid) {
      return false;
   }
   gui_file_editor_open(local);
   EditorContext *ctx = active_editor;

   if (!ctx) {
      return false;
   }
   ctx->remote_save = save;
   ctx->remote_data = data;
   ctx->remote_destroy = destroy;
   ctx->remote_token = event_on_token("client.files.complete", remote_edit_complete, ctx);
   gtk_widget_hide(ctx->save_as);
   char *remote_title = g_strdup_printf("File Editor — %s", title && *title ? title : "Remote file");
   rr_floating_panel_set_title(ctx->panel, remote_title);
   g_free(remote_title);

   return true;
}
