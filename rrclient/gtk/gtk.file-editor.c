#include <rrclient/config.layers.h>
//
// rrclient/gtk/gtk.editcfg.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// SPDX-License-Identifier: GPL-2.0-only
//
#define __RRCLI 1
#include <rrclient/database.h>
#include <libfwdspmgr/pipeline.recipe.h>
#include <rrclient/backup.h>
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
#include <gio/gio.h>
#include <librustyaxe/core.h>
#include <librustyaxe/config.h>
#include <librustyaxe/json.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/gtk/gtk.core.h>
#include "gtk.floating-panel.h"
#include "gtk.audio-routing.h"
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
   } else {
      rrclient_config_mark_dirty();
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

      /* Pipeline definitions are managed exclusively by Edit Pipelines. */
      if (g_str_has_prefix(def->key, "pipeline:")) {
         continue;
      }
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
   GtkWidget *textview;
   gchar *filepath;
   gboolean modified;
   gboolean config_edit;
   bool (*remote_save)(const char *, void *);
   void *remote_data;
   GDestroyNotify remote_destroy;
   GDestroyNotify close_notify;
   gpointer close_data;
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
   if (ctx->close_notify) {
      ctx->close_notify(ctx->close_data);
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
   rr_floating_panel_present(panel);
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
   (void)user_data;
   GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(btn));
   GtkWindow *parent = GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL;
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Reload client configuration", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
      GTK_RESPONSE_CANCEL, "Reload _rrclient.cfg", 1, "Reload _DB config", 2, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *explanation = gtk_label_new("rrclient.cfg supplies bootstrap defaults. Saved client.db config values\n"
      "normally override them when the client starts.\n\n"
      "Reload rrclient.cfg: use the system/user config files without DB\n"
      "configuration overrides (does not delete saved values).\n\n"
      "Reload DB config: rebuild from rrclient.cfg, then apply saved\n"
      "configuration overrides.\n\n"
      "Server profiles and themes are loaded from their separate DB tables\n" "in either case. For startup recovery use --ignore-db-config.");
   gtk_label_set_xalign(GTK_LABEL(explanation), 0);
   gtk_container_set_border_width(GTK_CONTAINER(area), 12);
   gtk_box_pack_start(GTK_BOX(area), explanation, TRUE, TRUE, 0);
   gtk_widget_show_all(area);
   gint choice = gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);

   if (choice == 1 || choice == 2) {
      rrclient_config_reload_request_t request = {
         .from_db = choice == 2
      };
      rr_event_message_t message = {
         .name = RRCLIENT_CONFIG_RELOAD_REQUEST,
         .payload = &request, .length = sizeof(request)
      };
      event_emit_message(&message);
   }
}

void gui_file_editor_open(const char *filepath) {
   if (!filepath) {
      return;
   }

   if (active_editor) {
      rr_floating_panel_present(active_editor->panel);

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
   rr_floating_panel_set_floating_only(ctx->panel);
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
   ctx->textview = textview;
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
   rr_floating_panel_present(ctx->panel);
}

/* Attach a close hook only to a newly opened editor, never another file. */
bool gui_file_editor_open_notify(const char *filepath,GDestroyNotify notify,gpointer data) {
   if (active_editor) {
      rr_floating_panel_present(active_editor->panel);
      return false;
   }
   gui_file_editor_open(filepath);
   if (!active_editor) {
      return false;
   }
   active_editor->close_notify=notify;
   active_editor->close_data=data;
   return true;
}

void gui_file_editor_open_at(const char *filepath,unsigned line,unsigned column) {
   if(!filepath)return;
   gui_file_editor_open(filepath);
   /* Don't jump in an unrelated editor if a different file is being edited. */
   if(!active_editor || !active_editor->textview ||
      g_strcmp0(active_editor->filepath,filepath))return;
   if(!line)return;
   GtkTextBuffer *buffer=active_editor->buffer;
   gint last=gtk_text_buffer_get_line_count(buffer);
   GtkTextIter iter;
   gtk_text_buffer_get_iter_at_line(buffer,&iter,MIN((gint)line-1,MAX(0,last-1)));
   gint chars=gtk_text_iter_get_chars_in_line(&iter);
   if(column>1)gtk_text_iter_forward_chars(&iter,MIN((gint)column-1,chars));
   gtk_text_buffer_place_cursor(buffer,&iter);
   gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(active_editor->textview),&iter,
      0.25,FALSE,0,0);
   gtk_widget_grab_focus(active_editor->textview);
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

static void on_fullscreen_button(GtkButton *button, gpointer user_data) {
   (void)button;
   gui_fullscreen_toggle();
}

static GtkWidget *config_action_button(const gchar *label, const gchar *icon, const gchar *tooltip) {
   GtkWidget *button = gtk_button_new_with_label(label);
   gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON));
   gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
   gtk_widget_set_tooltip_text(button, tooltip);

   return button;
}

gboolean gtk_pipeline_editor_apply_output(GtkWidget *parent, const gchar *output, gchar **error_message) {
   dict *edited = json2dict(output);

   if (!edited) {
      *error_message = g_strdup("The pipeline editor returned invalid JSON.");

      return FALSE;
   }
   int count = dict_get_int(edited, "pipelines.count", -1);

   if (count < 0 || count > 512) {
      *error_message = g_strdup("The pipeline editor returned an invalid pipeline count.");
      dict_free(edited);

      return FALSE;
   }
   GHashTable *ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   dict *updates = dict_new();
   gboolean ok = ids != NULL && updates != NULL;

   if (!ok) {
      *error_message = g_strdup("Cannot allocate pipeline configuration updates.");
   }

   for (int i = 0 ; ok && i < count ; i++) {
      gchar *id_key = g_strdup_printf("pipelines.%d.id", i);
      gchar *launch_key = g_strdup_printf("pipelines.%d.launch", i);
      const gchar *id = dict_get(edited, id_key, NULL);
      const gchar *launch = dict_get(edited, launch_key, NULL);
      gboolean valid = id && *id && launch && *launch;

      for (const gchar *p = id ; valid && *p ; p++) {
         if (g_ascii_isspace(*p) || *p == '=' || *p == '[' || *p == ']' || *p == '\n' || *p == '\r') {
            valid = FALSE;
         }
      }

      if (!valid || g_hash_table_contains(ids, id)) {
         *error_message = g_strdup_printf("The pipeline editor returned an invalid or duplicate pipeline at entry %d.", i);
         ok = FALSE;
      } else {
         g_hash_table_add(ids, g_strdup(id));
         gchar *config_key = g_strdup_printf("pipeline:%s", id);

         if (!config_key || dict_add(updates, config_key, launch)) {
            *error_message = g_strdup_printf("Could not stage pipeline '%s' for client configuration.", id);
            ok = FALSE;
         }
         g_free(config_key);
      }
      g_free(id_key);
      g_free(launch_key);
   }

   if (ok && dict_merge(cfg, updates)) {
      *error_message = g_strdup("Could not apply pipeline updates to client configuration.");
      ok = FALSE;
   }

   if (ok) {
      rrclient_config_mark_dirty();
      GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
         "Pipelines updated. Click Save Config to persist the changes.");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
   }

   if (ids) {
      g_hash_table_unref(ids);
   }
   dict_free(updates);
   dict_free(edited);

   return ok;
}

static void on_save_config_button(GtkButton *button, gpointer user_data) {
   (void)user_data;
   GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));

   if (!rrclient_config_save()) {
      GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Unable to save configuration to client.db");
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);

      return;
   }
   Log(LOG_INFO, "gtk.config", "Configuration saved to client.db");
}

/* Theme controls edit the active configuration, not a second shadow copy. */
typedef struct theme_field {
   const defconfig_t *definition;
   GtkWidget *value;
} theme_field_t;

static void theme_field_free(gpointer data) {
   g_free(data);
}

static void theme_field_changed(GtkEditable *editable, gpointer data) {
   theme_field_t *field = data;

   if (g_object_get_data(G_OBJECT(editable), "theme-refresh")) {
      return;
   }
   const char *value = gtk_entry_get_text(GTK_ENTRY(editable));

   if (!cfg_set_value(field->definition->key, value)) {
      return;
   }
   const char *selected = cfg_get("theme");

   if (selected && *selected && strcmp(selected, "Stock")) {
      char *key = g_strdup_printf("theme:%s.%s", selected, field->definition->key + strlen("theme."));
      dict_add(cfg, key, value);
      rrclient_database_theme_set(selected, field->definition->key + strlen("theme."), value);
      g_free(key);
   }
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
      snprintf(encoded, sizeof(encoded), "\004%02X%02X%02X", (unsigned)(chosen.red * 255.0 + .5), (unsigned)(chosen.green * 255.0 + .5), (unsigned)(chosen.blue
         * 255.0 + .5));
      gtk_entry_set_text(GTK_ENTRY(field->value), encoded);
   }
   gtk_widget_destroy(dialog);
}

static void theme_selected(GtkComboBox *combo, gpointer data) {
   GtkWidget *list = GTK_WIDGET(data);
   const char *name = gtk_combo_box_get_active_id(combo);

   if (!name || !cfg_set_value("theme", name)) {
      return;
   }
   rrclient_theme_apply();
   rrclient_config_mark_dirty();
   GList *rows = gtk_container_get_children(GTK_CONTAINER(list));

   for (GList *row = rows ; row ; row = row->next) {
      theme_field_t *field = g_object_get_data(G_OBJECT(row->data), "theme-field");

      if (!field) {
         continue;
      }
      const char *val = cfg_get(field->definition->key);
      g_object_set_data(G_OBJECT(field->value), "theme-refresh", GINT_TO_POINTER(1));
      gtk_entry_set_text(GTK_ENTRY(field->value), val ? val : "");
      g_object_set_data(G_OBJECT(field->value), "theme-refresh", NULL);
   }

   g_list_free(rows);
}

/* Save the visible active colors under an editable named theme. A changed name creates a new named theme rather than silently deleting the previous one. */
static void theme_save_clicked(GtkButton *button, gpointer user_data) {
   GtkWidget *chooser = GTK_WIDGET(user_data);
   const char *current = gtk_combo_box_get_active_id(GTK_COMBO_BOX(chooser));
   GtkWidget *top = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Save Theme", GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
   gtk_entry_set_text(GTK_ENTRY(entry), current ? current : "Stock");
   gtk_widget_set_margin_start(entry, 12);
   gtk_widget_set_margin_end(entry, 12);
   gtk_widget_set_margin_top(entry, 12);
   gtk_widget_set_margin_bottom(entry, 12);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry);
   gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
   gtk_widget_show(entry);

   if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      const char *entered = gtk_entry_get_text(GTK_ENTRY(entry));
      char *name = g_strdup(entered);
      g_strstrip(name);

      if (!*name || !strcmp(name, "Stock") || strchr(name, '.') ||
         strchr(name, ':') || strchr(name, '[') || strchr(name, ']') || strchr(name, '\n') || strchr(name, '\r')) {
         GtkWidget *error = gtk_message_dialog_new(GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
            "Enter a theme name other than Stock, without dots, colons, brackets or line breaks.");
         gtk_dialog_run(GTK_DIALOG(error));
         gtk_widget_destroy(error);
      } else {
         gboolean ok = TRUE;

         for (const defconfig_t *def = defcfg ; def->key ; ++def) {
            if (!g_str_has_prefix(def->key, "theme.")) {
               continue;
            }
            const char *value = cfg_get(def->key);
            char *key = g_strdup_printf("theme:%s.%s", name, def->key + 6);

            if (dict_add(cfg, key, value ? value : "") != 0 || !rrclient_database_theme_set(name, def->key + 6, value ? value : "")) {
               ok = FALSE;
            }
            g_free(key);
         }

         if (ok) {
            cfg_set_value("theme", name);
            rrclient_config_mark_dirty();
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(chooser), name, name);
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(chooser), name);
         } else {
            GtkWidget *error = gtk_message_dialog_new(GTK_IS_WINDOW(top) ? GTK_WINDOW(top) : NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
               "Unable to save theme to the database.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
         }
      }
      g_free(name);
   }
   gtk_widget_destroy(dialog);
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
      if (!g_str_has_prefix(key, "theme:")) {
         continue;
      }
      const char *start = key + strlen("theme:");
      const char *dot = strchr(start, '.');

      if (!dot || dot == start) {
         continue;
      }
      char *name = g_strndup(start, dot - start);

      if (g_hash_table_add(seen, name)) {
         gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(chooser), name, name);
      }
   }
   g_hash_table_destroy(seen);
   const char *selected = cfg_get("theme");

   if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(chooser), selected ? selected : "Stock")) {
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(chooser), "Stock");
   }
   gtk_box_pack_start(GTK_BOX(chooser_row), chooser, FALSE, FALSE, 0);
   GtkWidget *save_theme = gtk_button_new_with_label("Save Theme…");
   g_signal_connect(save_theme, "clicked", G_CALLBACK(theme_save_clicked), chooser);
   gtk_box_pack_start(GTK_BOX(chooser_row), save_theme, FALSE, FALSE, 0);
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_box_pack_start(GTK_BOX(container), scroll, TRUE, TRUE, 0);
   GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_add(GTK_CONTAINER(scroll), list);
   g_signal_connect(chooser, "changed", G_CALLBACK(theme_selected), list);

   for (const defconfig_t *def = defcfg ; def->key ; ++def) {
      if (!g_str_has_prefix(def->key, "theme.")) {
         continue;
      }
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

/* Backup requests are handled on the core event domain, never on GTK's thread. Neither the password nor decrypted database crosses back to GTK. */
static void gtk_backup_feedback(const rr_event_message_t *message, void *user) {
   (void)user;

   if (!message || !message->payload || message->length != sizeof(rrclient_backup_result_t)) {
      return;
   }
   const rrclient_backup_result_t *result = message->payload;
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL, result->ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
      "%s", result->message);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}

static void gtk_backup_alert(const char *message) {
   GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE, "%s", message);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}

/* Compact directory/file picker for .rrbk files, sized to fit small screens.
 * Directory entry supports arbitrary paths; file entry supports pasted paths. */
typedef struct {
   GtkWidget *dialog, *folder, *filename, *list;
} backup_pick_t;
static void backup_pick_scan(backup_pick_t *p) {
   GList *rows=gtk_container_get_children(GTK_CONTAINER(p->list));
   for(GList *it=rows;it;it=it->next)gtk_widget_destroy(GTK_WIDGET(it->data));
   g_list_free(rows);
   const char *folder=gtk_entry_get_text(GTK_ENTRY(p->folder));
   GDir *dir=g_dir_open(folder,0,NULL);
   if(!dir)return;
   const char *name;
   while((name=g_dir_read_name(dir))) {
      if(!strcmp(name,".")||!strcmp(name,".."))continue;
      char *path=g_build_filename(folder,name,NULL);
      bool is_dir=g_file_test(path,G_FILE_TEST_IS_DIR);
      if(is_dir||(g_str_has_suffix(name,".rrbk")&&g_file_test(path,G_FILE_TEST_IS_REGULAR))) {
         GtkWidget *row=gtk_list_box_row_new();
         char *label=g_strdup_printf("%s %s",is_dir?"[DIR]":"     ",name);
         GtkWidget *text=gtk_label_new(label);
         gtk_label_set_xalign(GTK_LABEL(text),0);
         gtk_widget_set_margin_start(text,6);
         gtk_container_add(GTK_CONTAINER(row),text);
         g_object_set_data_full(G_OBJECT(row),"file-path",g_strdup(path),g_free);
         g_object_set_data(G_OBJECT(row),"is-dir",GINT_TO_POINTER(is_dir));
         gtk_container_add(GTK_CONTAINER(p->list),row);
         g_free(label);
      }
      g_free(path);
   }
   g_dir_close(dir);
   gtk_widget_show_all(p->list);
}
static void backup_pick_go(GtkWidget *entry,gpointer data) {
   (void)entry;
   backup_pick_scan(data);
}
static void backup_pick_up(GtkButton *button,gpointer data) {
   (void)button;
   backup_pick_t *p=data;
   char *parent=g_path_get_dirname(gtk_entry_get_text(GTK_ENTRY(p->folder)));
   gtk_entry_set_text(GTK_ENTRY(p->folder),parent);
   gtk_entry_set_text(GTK_ENTRY(p->filename),"");
   backup_pick_scan(p);
   g_free(parent);
}
static void backup_pick_select(GtkListBox *list,GtkListBoxRow *row,gpointer data) {
   (void)list;
   backup_pick_t *p=data;
   if(row&&!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),"is-dir")))
      gtk_entry_set_text(GTK_ENTRY(p->filename),
         g_object_get_data(G_OBJECT(row),"file-path"));
}
static void backup_pick_activate(GtkListBox *list,GtkListBoxRow *row,gpointer data) {
   (void)list;
   backup_pick_t *p=data;
   const char *path=g_object_get_data(G_OBJECT(row),"file-path");
   if(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row),"is-dir"))) {
      gtk_entry_set_text(GTK_ENTRY(p->folder),path);
      gtk_entry_set_text(GTK_ENTRY(p->filename),"");
      backup_pick_scan(p);
   } else {
      gtk_entry_set_text(GTK_ENTRY(p->filename),path);
      gtk_dialog_response(GTK_DIALOG(p->dialog),GTK_RESPONSE_ACCEPT);
   }
}
static char *backup_choose_import_file(void) {
   backup_pick_t p={0};
   p.dialog=gtk_dialog_new_with_buttons("Select Encrypted RustyRig Backup",
      GTK_WINDOW(main_window),GTK_DIALOG_MODAL,"_Cancel",GTK_RESPONSE_CANCEL,
      "_Open",GTK_RESPONSE_ACCEPT,NULL);
   GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(p.dialog));
   gtk_container_set_border_width(GTK_CONTAINER(box),8);
   GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);
   gtk_box_pack_start(GTK_BOX(row),gtk_label_new("Folder:"),FALSE,FALSE,0);
   p.folder=gtk_entry_new();
   const char *downloads=g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
   gtk_entry_set_text(GTK_ENTRY(p.folder),downloads?downloads:g_get_home_dir());
   gtk_box_pack_start(GTK_BOX(row),p.folder,TRUE,TRUE,0);
   GtkWidget *go=gtk_button_new_with_label("Go");
   gtk_box_pack_start(GTK_BOX(row),go,FALSE,FALSE,0);
   GtkWidget *up=gtk_button_new_with_label("Up");
   gtk_box_pack_start(GTK_BOX(row),up,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,4);
   GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
      GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
   gtk_widget_set_vexpand(scroll,TRUE);
   p.list=gtk_list_box_new();
   gtk_container_add(GTK_CONTAINER(scroll),p.list);
   gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,3);
   p.filename=gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(p.filename),"Select or paste an absolute .rrbk path");
   gtk_box_pack_start(GTK_BOX(box),p.filename,FALSE,FALSE,3);
   g_signal_connect(p.folder,"activate",G_CALLBACK(backup_pick_go),&p);
   g_signal_connect(go,"clicked",G_CALLBACK(backup_pick_go),&p);
   g_signal_connect(up,"clicked",G_CALLBACK(backup_pick_up),&p);
   g_signal_connect(p.list,"row-selected",G_CALLBACK(backup_pick_select),&p);
   g_signal_connect(p.list,"row-activated",G_CALLBACK(backup_pick_activate),&p);
   GdkDisplay *display=gtk_widget_get_display(p.dialog);
   GdkMonitor *monitor=display?gdk_display_get_primary_monitor(display):NULL;
   GdkRectangle rect={0,0,640,480};
   if(monitor)gdk_monitor_get_workarea(monitor,&rect);
   gtk_window_set_default_size(GTK_WINDOW(p.dialog),MIN(520,MAX(290,rect.width-50)),
      MIN(410,MAX(220,rect.height-90)));
   backup_pick_scan(&p);
   gtk_widget_show_all(p.dialog);
   char *path=NULL;
   while(gtk_dialog_run(GTK_DIALOG(p.dialog))==GTK_RESPONSE_ACCEPT) {
      const char *name=gtk_entry_get_text(GTK_ENTRY(p.filename));
      if(g_path_is_absolute(name)&&g_str_has_suffix(name,".rrbk")&&
         g_file_test(name,G_FILE_TEST_IS_REGULAR)) {
         path=g_strdup(name);
         break;
      }
      gtk_backup_alert("Select an existing .rrbk file or enter its full path.");
   }
   gtk_widget_destroy(p.dialog);
   return path;
}

static void gtk_backup_action(GtkButton *unused, gpointer user_data) {
   (void)unused;
   bool importing = GPOINTER_TO_INT(user_data) != 0;
   /* Import is a file operation first: choose a real OS file before asking for secrets or replacing any database state. */
   char *import_path = NULL;

   if (importing) {
      import_path = backup_choose_import_file();

      if (!import_path) {
         return;
      }
   }
   GtkWidget *dialog = gtk_dialog_new_with_buttons(importing ? "Import Encrypted Backup" : "Backup RustyRig", GTK_WINDOW(main_window), GTK_DIALOG_MODAL,
      "_Cancel", GTK_RESPONSE_CANCEL, importing ? "_Import" : "_Continue", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *choice = gtk_combo_box_text_new();

   if (!importing) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choice), "Database (SQL) only");
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choice), "Database (SQL) + all private identities");
      gtk_combo_box_set_active(GTK_COMBO_BOX(choice), 1);
      gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Backup contents:"), FALSE, FALSE, 5);
      gtk_box_pack_start(GTK_BOX(box), choice, FALSE, FALSE, 5);
   } else {
      GtkWidget *warning =
         gtk_label_new("Import replaces the client database. Existing private key files will never be overwritten. Restart RustyRig after importing.");
      gtk_label_set_line_wrap(GTK_LABEL(warning), TRUE);
      gtk_box_pack_start(GTK_BOX(box), warning, FALSE, FALSE, 5);
   }
   GtkWidget *password = gtk_entry_new();
   gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
   gtk_entry_set_input_purpose(GTK_ENTRY(password), GTK_INPUT_PURPOSE_PASSWORD);
   gtk_entry_set_placeholder_text(GTK_ENTRY(password), "Backup password (required)");
   gtk_box_pack_start(GTK_BOX(box), password, FALSE, FALSE, 5);
   GtkWidget *confirm = NULL;

   if (!importing) {
      confirm = gtk_entry_new();
      gtk_entry_set_visibility(GTK_ENTRY(confirm), FALSE);
      gtk_entry_set_input_purpose(GTK_ENTRY(confirm), GTK_INPUT_PURPOSE_PASSWORD);
      gtk_entry_set_placeholder_text(GTK_ENTRY(confirm), "Confirm backup password");
      gtk_box_pack_start(GTK_BOX(box), confirm, FALSE, FALSE, 5);
   }
   gtk_widget_show_all(dialog);
   int response = gtk_dialog_run(GTK_DIALOG(dialog));
   rrclient_backup_request_t request = {
      0
   };
   bool ready = false;

   if (response == GTK_RESPONSE_ACCEPT) {
      const char *value = gtk_entry_get_text(GTK_ENTRY(password));
      const char *again = confirm ? gtk_entry_get_text(GTK_ENTRY(confirm)) : value;

      if (!value[0] || strcmp(value, again) || strlen(value) >= sizeof(request.password)) {
         gtk_backup_alert("Enter a matching nonempty password.");
      } else {
         g_strlcpy(request.password, value, sizeof(request.password));
         request.action = importing ? RR_BACKUP_IMPORT : (gtk_combo_box_get_active(GTK_COMBO_BOX(choice)) == 0 ? RR_BACKUP_SQL : RR_BACKUP_ALL);
         ready = true;
      }
   }
   gtk_entry_set_text(GTK_ENTRY(password), "");

   if (confirm) {
      gtk_entry_set_text(GTK_ENTRY(confirm), "");
   }
   gtk_widget_destroy(dialog);

   if (!ready) {
      g_free(import_path);

      return;
   }

   if (importing && strlen(import_path) < sizeof(request.path)) {
      g_strlcpy(request.path, import_path, sizeof(request.path));
      GtkWidget *verify = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
         "Replace the current RustyRig client database with this backup? " "Existing private keys are not overwritten. Restart afterwards.");
      bool approved = gtk_dialog_run(GTK_DIALOG(verify)) == GTK_RESPONSE_YES;
      gtk_widget_destroy(verify);

      if (approved) {
         rr_event_message_t message = {
            .name = RRCLIENT_BACKUP_REQUEST,
            .payload = &request, .length = sizeof(request)
         };
         event_emit_message(&message);
      }
      goto backup_finished;
   }

   if (importing) {
      gtk_backup_alert("Backup filename is too long.");
      goto backup_finished;
   }
   GtkFileChooserAction action = importing ? GTK_FILE_CHOOSER_ACTION_OPEN : GTK_FILE_CHOOSER_ACTION_SAVE;
   GtkWidget *chooser = gtk_file_chooser_dialog_new(importing ? "Select Encrypted RustyRig Backup" : "Save Encrypted RustyRig Backup", GTK_WINDOW(main_window),
      action, "_Cancel", GTK_RESPONSE_CANCEL, importing ? "_Open" : "_Save", GTK_RESPONSE_ACCEPT, NULL);

   if (!importing) {
      gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "rustyrig-backup.rrbk");
      /* Existing files are refused by the core; no accidental overwrite. */
   }

   if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
      char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

      if (filename && strlen(filename) < sizeof(request.path)) {
         g_strlcpy(request.path, filename, sizeof(request.path));
         bool approved = true;

         if (importing) {
            GtkWidget *verify = gtk_message_dialog_new(GTK_WINDOW(main_window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
               "Replace the current RustyRig client database with this backup? " "Connected sessions may need to be closed; restart immediately afterwards.");
            approved = gtk_dialog_run(GTK_DIALOG(verify)) == GTK_RESPONSE_YES;
            gtk_widget_destroy(verify);
         }

         if (approved) {
            rr_event_message_t message = {
               .name = RRCLIENT_BACKUP_REQUEST,
               .payload = &request, .length = sizeof(request)
            };
            event_emit_message(&message);
         }
      } else {
         gtk_backup_alert("Backup filename is too long.");
      }
      g_free(filename);
   }
   gtk_widget_destroy(chooser);
backup_finished:
   ;
   g_free(import_path);
   volatile char *wipe = request.password;

   for (size_t i = 0 ; i < sizeof(request.password) ; i++) {
      wipe[i] = 0;
   }
}

/* Client-local callsign watch list; SQLite owns the persisted rules. No decoder or QSO-log matching occurs until contact logging is added. */
enum {
   WATCH_ID, WATCH_CALL, WATCH_MODE, WATCH_BAND, WATCH_AUTO, WATCH_WORKED, WATCH_TYPE, WATCH_CQ, WATCH_COLUMNS
};
static void watched_global_cq_changed(GtkToggleButton *button,gpointer unused) {
   (void)unused;
   rrclient_database_setting_set("watch.cq-only",
      gtk_toggle_button_get_active(button)?"true":"false");
}
static void watched_calls_row(const rrclient_watched_call_t *entry, void *user) {
   GtkListStore *store = user;
   GtkTreeIter row;
   gtk_list_store_append(store, &row);
   gtk_list_store_set(store, &row, WATCH_ID, entry->id, WATCH_CALL, entry->callsign, WATCH_MODE, entry->mode, WATCH_BAND, entry->band, WATCH_AUTO, entry->
      auto_mark_worked, WATCH_WORKED, entry->worked, WATCH_TYPE, entry->match_type, WATCH_CQ, entry->cq_only, -1);
}

static void watched_calls_reload(GtkWidget *tree) {
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)));
   gtk_list_store_clear(store);
   rrclient_database_watched_calls_foreach(watched_calls_row, store);
}

static void watched_calls_error(GtkWidget *parent, const char *message) {
   GtkWidget *dlg = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
   gtk_dialog_run(GTK_DIALOG(dlg));
   gtk_widget_destroy(dlg);
}

static gboolean watched_calls_selected(GtkWidget *tree, GtkTreeIter *row) {
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));

   return gtk_tree_selection_get_selected(selection, NULL, row);
}

static GtkWidget *watched_calls_option(const char *const *items, const char *chosen) {
   GtkWidget *combo = gtk_combo_box_text_new_with_entry();

   for (unsigned i = 0 ; items[i] ; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), items[i]);
   }

   gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

   if (chosen && *chosen) {
      gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(combo))), chosen);
   }

   return combo;
}

static void watched_calls_edit(GtkWidget *button, gpointer user) {
   GtkWidget *tree = user;
   gboolean editing = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "watch-edit")) != 0;
   GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree)));
   GtkTreeIter selected;

   if (editing && !watched_calls_selected(tree, &selected)) {
      return;
   }
   gint id = 0;
   gchar *old_call = NULL, *old_mode = NULL, *old_band = NULL,*old_type=NULL;
   gboolean old_auto = FALSE, old_worked = FALSE,old_cq=FALSE;

   if (editing) {
      gtk_tree_model_get(GTK_TREE_MODEL(store), &selected, WATCH_ID, &id, WATCH_CALL, &old_call, WATCH_MODE, &old_mode, WATCH_BAND, &old_band, WATCH_AUTO, &
         old_auto, WATCH_WORKED, &old_worked, WATCH_TYPE,&old_type,WATCH_CQ,&old_cq,-1);
   }
   GtkWidget *parent = gtk_widget_get_toplevel(tree);
   GtkWidget *dialog = gtk_dialog_new_with_buttons(editing ? "Edit Watched Call" : "Watch Callsign", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL,
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *grid = gtk_grid_new();
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   gtk_box_pack_start(GTK_BOX(area), grid, TRUE, TRUE, 0);
   GtkWidget *call = gtk_entry_new();
   gtk_entry_set_max_length(GTK_ENTRY(call), 256);
   gtk_entry_set_placeholder_text(GTK_ENTRY(call), "K1ABC, VP9*, ^VP9.*$ or Bermuda");

   if (old_call) {
      gtk_entry_set_text(GTK_ENTRY(call), old_call);
   }
   static const char *const modes[] = {
      "Any", "FT8", "FT4", "PSK31", "Olivia", "RTTY",
      "CW", "SSB", "USB", "LSB", "AM", "FM", "JS8", NULL
   };
   static const char *const bands[] = {
      "Any", "2200m", "630m", "160m", "80m", "60m", "40m",
      "30m", "20m", "17m", "15m", "12m", "10m", "6m",
      "2m", "1.25m", "70cm", "33cm", "23cm", NULL
   };
   GtkWidget *mode = watched_calls_option(modes, old_mode);
   GtkWidget *band = watched_calls_option(bands, old_band);
   GtkWidget *auto_mark = gtk_check_button_new_with_label("Mark worked when a matching QSO is logged (future)");
   GtkWidget *worked = gtk_check_button_new_with_label("Already worked");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_mark), old_auto);
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(worked), old_worked);
   GtkWidget *fields[] = {
      call, mode, band
   };
   const char *titles[] = {
      "Callsign", "Mode", "Band"
   };

   for (unsigned i = 0 ; i < 3 ; i++) {
      GtkWidget *label = gtk_label_new(titles[i]);
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      gtk_widget_set_hexpand(fields[i], TRUE);
      gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
   }

   GtkWidget *type=gtk_combo_box_text_new();
   const char *types[]={"Exact","Wildcard","Regex","Country","Continent","CQ zone","ITU zone","Grid"};
   for(unsigned i=0;i<G_N_ELEMENTS(types);i++)
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(type),types[i],types[i]);
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(type),old_type?old_type:"Exact");
   GtkWidget *cq=gtk_check_button_new_with_label("CQ only");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cq),old_cq);
   gtk_grid_attach(GTK_GRID(grid),gtk_label_new("Match type"),0,3,1,1);
   gtk_grid_attach(GTK_GRID(grid),type,1,3,1,1);
   gtk_grid_attach(GTK_GRID(grid),cq,1,4,1,1);
   gtk_grid_attach(GTK_GRID(grid),auto_mark,1,5,1,1);
   gtk_grid_attach(GTK_GRID(grid), worked, 1, 6, 1, 1);
   gtk_widget_show_all(dialog);
   while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
      gchar *raw = g_strdup(gtk_entry_get_text(GTK_ENTRY(call)));
      g_strstrip(raw);
      const char *kind=gtk_combo_box_get_active_id(GTK_COMBO_BOX(type));
      gchar *normalized=g_strdup(raw);
      g_free(raw);
      const gchar *m=gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(mode))));
      const gchar *b=gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(band))));
      gboolean valid=*normalized&&*m&&*b&&kind;
      if(valid&&!strcmp(kind,"Regex")) {
         GRegex *regex=g_regex_new(normalized,G_REGEX_CASELESS,0,NULL);
         valid=regex!=NULL;
         if(regex)g_regex_unref(regex);
      } else if(valid&&!strcmp(kind,"Exact")) {
         gboolean digit=FALSE;
         for(const char *p=normalized;*p;p++) {
            digit|=g_ascii_isdigit(*p);
            if(!g_ascii_isalnum(*p)&&*p!='/')valid=FALSE;
         }
         valid=valid&&digit&&strlen(normalized)<=24;
      }
      if(!valid) {
         watched_calls_error(GTK_WIDGET(dialog),"Enter a valid match, mode and band (regex must compile).");
      } else if(rrclient_database_watched_call_save(id,normalized,m,b,
         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(auto_mark)),
         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(worked)),kind,
         gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cq)))) {
         g_free(normalized);
         watched_calls_reload(tree);
         break;
      } else {
         watched_calls_error(GTK_WIDGET(dialog), "Unable to save the watch. Check for a duplicate callsign/mode/band combination or an SQLite error.");
      }
      g_free(normalized);
   }
   gtk_widget_destroy(dialog);
   g_free(old_call);
   g_free(old_mode);
   g_free(old_band);
   g_free(old_type);
}

static void watched_calls_remove(GtkButton *button, gpointer user) {
   (void)button;
   GtkWidget *tree = user;
   GtkTreeIter row;

   if (!watched_calls_selected(tree, &row)) {
      return;
   }
   GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree));
   gint id = 0;
   gchar *call = NULL;
   gtk_tree_model_get(model, &row, WATCH_ID, &id, WATCH_CALL, &call, -1);
   GtkWidget *parent = gtk_widget_get_toplevel(tree);
   GtkWidget *dlg = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "Remove watch for %s?", call ? call : "");
   gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Cancel", GTK_RESPONSE_CANCEL, "_Remove", GTK_RESPONSE_ACCEPT, NULL);

   if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
      if (rrclient_database_watched_call_delete(id)) {
         watched_calls_reload(tree);
      } else {
         watched_calls_error(parent, "Unable to remove watched call.");
      }
   }
   gtk_widget_destroy(dlg);
   g_free(call);
}

static void watched_calls_selection_changed(GtkTreeSelection *selection, gpointer user) {
   GtkWidget **buttons = user;
   gboolean chosen = gtk_tree_selection_count_selected_rows(selection) > 0;
   gtk_widget_set_sensitive(buttons[0], chosen);
   gtk_widget_set_sensitive(buttons[1], chosen);
}

static void watched_calls_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer button) {
   (void)view;
   (void)path;
   (void)column;
   watched_calls_edit(GTK_WIDGET(button), g_object_get_data(G_OBJECT(button), "watch-tree"));
}

static GtkWidget *watched_calls_panel(void) {
   GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(panel), 10);
   GtkWidget *note = gtk_label_new("Watch callsigns on specific modes/bands or Any. Automatic worked status will be enabled when QSO logging is implemented.");
   gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
   gtk_widget_set_halign(note, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(panel), note, FALSE, FALSE, 0);
   GtkListStore *store = gtk_list_store_new(WATCH_COLUMNS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,G_TYPE_STRING,G_TYPE_BOOLEAN);
   GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
   g_object_unref(store);
   const char *headers[] = {
      "Match", "Mode", "Band", "Type", "CQ only", "Auto-mark worked", "Worked"
   };
   const int columns[] = {
      WATCH_CALL, WATCH_MODE, WATCH_BAND, WATCH_TYPE,WATCH_CQ,WATCH_AUTO, WATCH_WORKED
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(columns) ; i++) {
      GtkCellRenderer *renderer = i < 4 ? gtk_cell_renderer_text_new() : gtk_cell_renderer_toggle_new();
      GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(headers[i], renderer, i < 4 ? "text" : "active", columns[i], NULL);
      gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);
   }

   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_container_add(GTK_CONTAINER(scroll), tree);
   gtk_box_pack_start(GTK_BOX(panel), scroll, TRUE, TRUE, 0);
   GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   gtk_box_pack_start(GTK_BOX(panel), toolbar, FALSE, FALSE, 0);
   GtkWidget *add = gtk_button_new_with_label("Add…");
   GtkWidget *edit = gtk_button_new_with_label("Edit…");
   GtkWidget *remove = gtk_button_new_with_label("Remove");
   gtk_box_pack_start(GTK_BOX(toolbar), add, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), edit, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(toolbar), remove, FALSE, FALSE, 0);
   GtkWidget *global_cq=gtk_check_button_new_with_label("Global CQ only");
   char *saved_cq=rrclient_database_setting_get("watch.cq-only");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(global_cq),!g_strcmp0(saved_cq,"true"));
   g_free(saved_cq);
   g_signal_connect(global_cq,"toggled",G_CALLBACK(watched_global_cq_changed),NULL);
   gtk_box_pack_end(GTK_BOX(toolbar),global_cq,FALSE,FALSE,0);

   g_object_set_data(G_OBJECT(edit), "watch-edit", GINT_TO_POINTER(1));
   g_object_set_data(G_OBJECT(edit), "watch-tree", tree);
   g_signal_connect(add, "clicked", G_CALLBACK(watched_calls_edit), tree);
   g_signal_connect(edit, "clicked", G_CALLBACK(watched_calls_edit), tree);
   g_signal_connect(remove, "clicked", G_CALLBACK(watched_calls_remove), tree);
   g_signal_connect(tree, "row-activated", G_CALLBACK(watched_calls_row_activated), edit);
   GtkWidget **buttons = g_new(GtkWidget *, 2);
   buttons[0] = edit;
   buttons[1] = remove;
   g_object_set_data_full(G_OBJECT(panel), "watch-buttons", buttons, g_free);
   GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
   g_signal_connect(selection, "changed", G_CALLBACK(watched_calls_selection_changed), buttons);
   watched_calls_selection_changed(selection, buttons);
   watched_calls_reload(tree);

   return panel;
}

/* Simple client sound device settings; the configured pipeline remains authoritative until the operator explicitly selects Generated. */
typedef struct {
   GtkWidget *mode, *capture, *playback, *capture_rate, *playback_rate;
} client_audio_setup_t;
static void client_audio_setup_free(gpointer data) {
   g_free(data);
}

static const char *client_audio_rate_value(GtkWidget *combo) {
   const char *rate = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));

   return rate ? rate : "auto";
}

static void client_audio_setup_preview(GtkButton *button, gpointer user) {
   client_audio_setup_t *ui = user;
   const char *cap = client_audio_rate_value(ui->capture_rate);
   const char *play = client_audio_rate_value(ui->playback_rate);
   unsigned capture_hz = !strcmp(cap, "44100") ? 44100 : 48000;
   unsigned playback_hz = !strcmp(play, "44100") ? 44100 : 48000;
   rr_pipeline_recipe_t in = {
      .endpoint = RR_PIPELINE_CAPTURE_MONO, .device_rate_hz = capture_hz,
      .pcm_rate_hz = 16000,
      .device = gtk_entry_get_text(GTK_ENTRY(ui->capture))
   };
   rr_pipeline_recipe_t out = {
      .endpoint = RR_PIPELINE_PLAYBACK_STEREO, .device_rate_hz = playback_hz,
      .pcm_rate_hz = playback_hz,
      .device = gtk_entry_get_text(GTK_ENTRY(ui->playback))
   };
   char *src = rr_pipeline_recipe_compile(&in);
   char *sink = rr_pipeline_recipe_compile(&out);
   GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Generated pipeline preview", GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL |
      GTK_DIALOG_DESTROY_WITH_PARENT, "_Close", GTK_RESPONSE_CLOSE, NULL);
   gtk_window_set_default_size(GTK_WINDOW(dialog), 690, 390);
   GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_widget_set_vexpand(scroll, TRUE);
   GtkWidget *view = gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
   char *preview = src && sink ?
      g_strdup_printf("Microphone:\n%s\n\nSpeakers:\n%s\n\n"
      "These pipelines run only inside fwdsp after restarting rrclient.", src, sink) : g_strdup("Invalid audio device name or rate. No changes were applied.");
   gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), preview, -1);
   gtk_container_add(GTK_CONTAINER(scroll), view);
   gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 6);
   gtk_widget_show_all(dialog);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   g_free(src);
   g_free(sink);
   g_free(preview);
}

static void client_audio_setup_apply(GtkButton *button, gpointer user) {
   client_audio_setup_t *ui = user;
   const char *mode = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->mode));
   const char *cap = gtk_entry_get_text(GTK_ENTRY(ui->capture));
   const char *play = gtk_entry_get_text(GTK_ENTRY(ui->playback));

   if (!mode) {
      return;
   }
   rr_pipeline_recipe_t in = {
      RR_PIPELINE_CAPTURE_MONO, 48000, 16000, cap
   };
   rr_pipeline_recipe_t out = {
      RR_PIPELINE_PLAYBACK_STEREO, 48000, 48000, play
   };
   char *a = rr_pipeline_recipe_compile(&in);
   char *b = rr_pipeline_recipe_compile(&out);

   if (!a || !b) {
      GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
      GtkWidget *warning = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Invalid device name.");
      gtk_dialog_run(GTK_DIALOG(warning));
      gtk_widget_destroy(warning);
   } else {
      cfg_set_value("audio.pipeline-mode", mode);
      cfg_set_value("audio.capture-device", cap);
      cfg_set_value("audio.playback-device", play);
      cfg_set_value("audio.capture-rate", client_audio_rate_value(ui->capture_rate));
      cfg_set_value("audio.playback-rate", client_audio_rate_value(ui->playback_rate));
      rrclient_config_mark_dirty();
      GtkWidget *parent = gtk_widget_get_toplevel(GTK_WIDGET(button));
      GtkWidget *note = gtk_message_dialog_new(GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent) : NULL, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
         GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Settings staged. Click Save Config to persist, then restart rrclient. "
         "Configured pipelines remain supported and are the default.");
      gtk_dialog_run(GTK_DIALOG(note));
      gtk_widget_destroy(note);
   }
   g_free(a);
   g_free(b);
}

static GtkWidget *client_audio_setup_panel(void) {
   GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(page), 12);
   GtkWidget *info = gtk_label_new("Choose sound devices without writing GStreamer. Configured pipelines "
      "remain the default; generated endpoints are opt-in. Changes take effect " "after saving configuration and restarting rrclient.");
   gtk_label_set_xalign(GTK_LABEL(info), 0);
   gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
   gtk_box_pack_start(GTK_BOX(page), info, FALSE, FALSE, 0);
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
   gtk_box_pack_start(GTK_BOX(page), grid, FALSE, FALSE, 0);
   client_audio_setup_t *ui = g_new0(client_audio_setup_t, 1);
   g_object_set_data_full(G_OBJECT(page), "rr-audio-setup", ui, client_audio_setup_free);
   ui->mode = gtk_combo_box_text_new();
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->mode), "configured", "Configured pipelines");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui->mode), "generated", "Generated device pipelines");
   const char *mode = cfg_get("audio.pipeline-mode");
   gtk_combo_box_set_active_id(GTK_COMBO_BOX(ui->mode), mode && !strcmp(mode, "generated") ? "generated" : "configured");
   ui->capture = gtk_entry_new();
   ui->playback = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(ui->capture), cfg_get("audio.capture-device") ? cfg_get("audio.capture-device") : "default");
   gtk_entry_set_text(GTK_ENTRY(ui->playback), cfg_get("audio.playback-device") ? cfg_get("audio.playback-device") : "default");
   gtk_entry_set_placeholder_text(GTK_ENTRY(ui->capture), "default");
   gtk_entry_set_placeholder_text(GTK_ENTRY(ui->playback), "default");
   ui->capture_rate = gtk_combo_box_text_new();
   ui->playback_rate = gtk_combo_box_text_new();
   GtkWidget *rates[] = {
      ui->capture_rate, ui->playback_rate
   };
   const char *keys[] = {
      "audio.capture-rate", "audio.playback-rate"
   };

   for (int i = 0 ; i < 2 ; i++) {
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rates[i]), "auto", "Auto (48 kHz)");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rates[i]), "44100", "44.1 kHz");
      gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(rates[i]), "48000", "48 kHz");
      const char *value = cfg_get(keys[i]);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(rates[i]), value && (!strcmp(value, "44100") || !strcmp(value, "48000")) ? value : "auto");
   }

   GtkWidget *values[] = {
      ui->mode, ui->capture, ui->playback,
      ui->capture_rate, ui->playback_rate
   };
   const char *names[] = {
      "Pipeline mode", "Microphone device", "Speaker device",
      "Microphone rate", "Speaker rate"
   };

   for (int i = 0 ; i < 5 ; i++) {
      GtkWidget *label = gtk_label_new(names[i]);
      gtk_widget_set_halign(label, GTK_ALIGN_END);
      gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
      gtk_widget_set_hexpand(values[i], TRUE);
      gtk_grid_attach(GTK_GRID(grid), values[i], 1, i, 1, 1);
   }

   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
   GtkWidget *preview = gtk_button_new_with_label("Preview generated pipelines");
   GtkWidget *apply = gtk_button_new_with_label("Apply Settings");
   gtk_box_pack_start(GTK_BOX(buttons), preview, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(buttons), apply, FALSE, FALSE, 0);
   g_signal_connect(preview, "clicked", G_CALLBACK(client_audio_setup_preview), ui);
   g_signal_connect(apply, "clicked", G_CALLBACK(client_audio_setup_apply), ui);
   gtk_box_pack_start(GTK_BOX(page), buttons, FALSE, FALSE, 4);

   return page;
}

GtkWidget *init_config_tab(void) {
   GtkWidget *nw = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

   GtkWidget *settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
   GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(header), 6);
   gtk_box_pack_start(GTK_BOX(settings), header, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(header), gtk_label_new("Filter:"), FALSE, FALSE, 0);
   GtkWidget *search = gtk_search_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(search), 20);
   gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Filter config… (=key, ^prefix)");
   gtk_widget_set_tooltip_text(search,
      "Filter keys and descriptions (case-insensitive). =audio: keys containing audio; ^debug: keys starting with debug. Clear to show all fields.");
   gtk_box_pack_start(GTK_BOX(header), search, FALSE, FALSE, 0);
   GtkWidget *config_label = gtk_label_new("Changes are saved to client.db when you click Save Config.");
   gtk_box_pack_start(GTK_BOX(header), config_label, FALSE, FALSE, 0);

   GtkWidget *cfg_list;
   GtkWidget *cfg_panel = cfg_editor_panel(&cfg_list);
   cfg_list_state_t *filter_state = g_object_get_data(G_OBJECT(cfg_panel), "cfg-list-state");
   g_signal_connect(search, "changed", G_CALLBACK(cfg_editor_filter_changed), filter_state);
   gtk_widget_set_vexpand(cfg_panel, TRUE);
   gtk_box_pack_start(GTK_BOX(settings), cfg_panel, TRUE, TRUE, 0);
   GtkWidget *sections = gtk_notebook_new();
   gtk_widget_set_vexpand(sections, TRUE);
   gtk_box_pack_start(GTK_BOX(nw), sections, TRUE, TRUE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(sections), settings, gtk_label_new("Settings"));
   gtk_notebook_append_page(GTK_NOTEBOOK(sections), cfg_theme_panel(), gtk_label_new("Theme"));
   gtk_notebook_append_page(GTK_NOTEBOOK(sections), client_audio_setup_panel(), gtk_label_new("Audio Devices"));
   gtk_notebook_append_page(GTK_NOTEBOOK(sections), watched_calls_panel(), gtk_label_new("Watched Calls"));

   GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
   gtk_container_set_border_width(GTK_CONTAINER(buttons), 6);
   gtk_widget_set_halign(buttons, GTK_ALIGN_START);
   gtk_box_pack_start(GTK_BOX(nw), buttons, FALSE, FALSE, 0);

   GtkWidget *btn_savecfg = config_action_button("Save Config", "document-save-symbolic", "Save client configuration to client.db");
   g_signal_connect(btn_savecfg, "clicked", G_CALLBACK(on_save_config_button), NULL);
   gtk_box_pack_start(GTK_BOX(buttons), btn_savecfg, FALSE, FALSE, 0);

   GtkWidget *btn_reloadcfg =
      config_action_button("Reload Config", "view-refresh-symbolic", "Choose whether to reload file bootstrap or saved database configuration");
   g_signal_connect(btn_reloadcfg, "clicked", G_CALLBACK(on_reload_config_button), (gpointer) config_file);
   gtk_box_pack_start(GTK_BOX(buttons), btn_reloadcfg, FALSE, FALSE, 0);

   event_on_message_domain_token(RRCLIENT_BACKUP_RESULT, gtk_backup_feedback, NULL, "gtk");
   GtkWidget *btn_backup = config_action_button("Backup…", "document-save-as-symbolic", "Export encrypted SQL or SQL + private identity keys");
   g_signal_connect(btn_backup, "clicked", G_CALLBACK(gtk_backup_action), GINT_TO_POINTER(0));
   gtk_box_pack_start(GTK_BOX(buttons), btn_backup, FALSE, FALSE, 0);
   GtkWidget *btn_import =
      config_action_button("Import Backup…", "document-open-symbolic", "Restore a password-protected RustyRig backup; restart afterwards");
   g_signal_connect(btn_import, "clicked", G_CALLBACK(gtk_backup_action), GINT_TO_POINTER(1));
   gtk_box_pack_start(GTK_BOX(buttons), btn_import, FALSE, FALSE, 0);

   GtkWidget *btn_certificates = gtk_certificates_button_new();
   gtk_box_pack_start(GTK_BOX(buttons), btn_certificates, FALSE, FALSE, 0);

   GtkWidget *btn_fullscreen = config_action_button("Toggle Fullscreen", "view-fullscreen-symbolic", "Toggle fullscreen mode");
   g_signal_connect(btn_fullscreen, "clicked", G_CALLBACK(on_fullscreen_button), NULL);
   gtk_box_pack_start(GTK_BOX(buttons), btn_fullscreen, FALSE, FALSE, 0);

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
   ctx->remote_token = event_on_domain_token("client.files.complete", remote_edit_complete, ctx, "gtk");
   gtk_widget_hide(ctx->save_as);
   char *remote_title = g_strdup_printf("File Editor — %s", title && *title ? title : "Remote file");
   rr_floating_panel_set_title(ctx->panel, remote_title);
   g_free(remote_title);

   return true;
}
