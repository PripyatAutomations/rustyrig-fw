// Native serial endpoint chooser and settings. Common sercom owns all I/O.
// Part of rustyrig-fw; MIT license without mongoose, GPL when built with it.
#include <stdbool.h>
#include <string.h>
#include <gtk/gtk.h>
#include <librustyaxe/core.h>
#include <rrclient/connman.h>
#include <rrclient/sercom.h>
#include "gtk.serial.h"

static bool permanent_port(const char *name) {
   return name && (!strcmp(name, "ttyCAT") || !strcmp(name, "ttyGPS") || !strcmp(name, "ttyMODEM"));
}
GList *gtk_serial_configured_names(void) {
   GList *names = NULL;
   const char *key;
   char *value;
   int rank = 0;
   while (cfg && (rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      char *name = NULL;

      /* Port settings alone are not a mapping. The default configuration contains ttyCAT0 settings for optional pinned CAT ports, but they must not appear
       * until serial.ttyCAT0 names a service. */
      if (!strncmp(key, "serial.", 7) && key[7] && !strchr(key + 7, '.')) {
         if (value && *value && strcmp(value, "none")) {
            name = g_strdup(key + 7);
         }
      }

      if (name && !g_list_find_custom(names, name, (GCompareFunc) g_strcmp0)) {
         names = g_list_prepend(names, name);
      } else {
         g_free(name);
      }
   }
   const char *permanent[] = {
      "ttyCAT", "ttyGPS", "ttyMODEM"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(permanent) ; i++) {
      if (!g_list_find_custom(names, permanent[i], (GCompareFunc) g_strcmp0)) {
         names = g_list_prepend(names, g_strdup(permanent[i]));
      }
   }

   return g_list_sort(names, (GCompareFunc) g_strcmp0);
}
static void port_names(GtkComboBoxText *combo) {
   gtk_combo_box_text_remove_all(combo);
   GList *names = gtk_serial_configured_names();

   for (GList *item = names ; item ; item = item->next) {
      gtk_combo_box_text_append_text(combo, item->data);
   }

   g_list_free_full(names, g_free);
}
static void field(GtkGrid *grid, const char *title, GtkWidget *widget, int row) {
   GtkWidget *label = gtk_label_new(title);
   gtk_widget_set_halign(label, GTK_ALIGN_START);
   gtk_grid_attach(grid, label, 0, row, 1, 1);
   gtk_grid_attach(grid, widget, 1, row, 1, 1);
}
static void message(GtkWindow *parent, const char *text) {
   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", text);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
}

/* Editable combo preserves custom rates and previously saved values. */
static const char *baud_text(GtkWidget *combo) {
   return gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(combo))));
}

struct port_editor {
   GtkWidget *name, *type, *path, *baud, *mode, *service;
};
static void load_port(GtkComboBox *combo, gpointer data) {
   (void)combo;
   struct port_editor *editor = data;
   const char *name = gtk_entry_get_text(GTK_ENTRY(editor->name));
   const char *fields[] = {
      "type", "path", "baud", "mode"
   };
   const char *defaults[] = {
      "pty", "", "9600", "8n1"
   };
   GtkWidget *entries[] = {
      editor->type, editor->path, editor->baud, editor->mode
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) ; i++) {
      char *key = g_strdup_printf("serial:%s.%s", name, fields[i]);
      const char *value = cfg_get(key);

      if (!value) {
         value = defaults[i];
      }

      if (!i) {
         gtk_combo_box_set_active_id(GTK_COMBO_BOX(entries[i]), value);
      } else {
         gtk_entry_set_text(GTK_ENTRY(entries[i]), value);
      }
      g_free(key);
   }

   char *key = g_strdup_printf("serial.%s", name);
   const char *service = cfg_get(key);
   gtk_entry_set_text(GTK_ENTRY(editor->service), service ? service :
      !strcmp(name, "ttyCAT") ? "rig.cat" : !strcmp(name, "ttyGPS") ? "rig.gps-out@4800" : "none");
   g_free(key);
}
static void browse_path(GtkButton *button, gpointer data) {
   struct port_editor *editor = data;
   GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
   bool file = !g_strcmp0(gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor->type)), "file");
   GtkWidget *chooser = gtk_file_chooser_dialog_new(file ? "Append capture to file" : "Select local device / PTY link", parent, file ?
      GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Select", GTK_RESPONSE_ACCEPT, NULL);

   if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
      char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
      gtk_entry_set_text(GTK_ENTRY(editor->path), path);
      g_free(path);
   }
   gtk_widget_destroy(chooser);
}
static void delete_port(GtkWindow *parent, const char *name, GtkWidget *status) {
   if (permanent_port(name)) {
      gtk_label_set_text(GTK_LABEL(status), "This built-in port cannot be removed.");

      return;
   }

   if (!name || !*name || rr_serial_find(name)) {
      gtk_label_set_text(GTK_LABEL(status), "Disconnect the port before removing its configuration.");

      return;
   }
   char *service_key = g_strdup_printf("serial.%s", name);
   const char *fields[] = {
      "type", "path", "baud", "mode", "buffer-bytes", "vfo"
   };
   bool exists = cfg_get(service_key) != NULL;

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) ; i++) {
      char *key = g_strdup_printf("serial:%s.%s", name, fields[i]);

      if (cfg_get(key)) {
         exists = true;
      }
      g_free(key);
   }

   if (!exists) {
      gtk_label_set_text(GTK_LABEL(status), "No saved configuration exists for this port.");
      g_free(service_key);

      return;
   }
   GtkWidget *confirm = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
      "Delete configuration for %s?", name);
   gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(confirm), "This removes its startup binding and local endpoint settings.");
   bool yes = gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_OK;
   gtk_widget_destroy(confirm);

   if (!yes) {
      g_free(service_key);

      return;
   }
   dict_del(cfg, service_key);

   for (unsigned i = 0 ; i < G_N_ELEMENTS(fields) ; ++i) {
      char *key = g_strdup_printf("serial:%s.%s", name, fields[i]);
      dict_del(cfg, key);
      g_free(key);
   }

   gtk_label_set_text(GTK_LABEL(status), "Port configuration removed in memory. Use /save to persist.");
   g_free(service_key);
}
static void port_info(GtkWindow *parent, const char *name) {
   rr_sercom_info_t info;
   bool attached = name && rr_sercom_info(name, &info);
   char *text = attached ? g_strdup_printf("Port: %s\nService: %s\nServer: %s\nPath: %s\nState: %s\n\n"
      "For remote serial owners, see Admin > Sessions (Serial Ports).\n" "Local PTY process/user enumeration is not available yet.", name, info.service ? info.
      service : "", info.profile
      && *info.profile ? info.profile : "Local", info.path ? info.path : "", info.state ? info.state : "") : g_strdup_printf(
      "Port: %s\nNot currently attached.\n" "No active RustyRig binding is registered for this port.", name ? name : "");
   GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", text);
   gtk_dialog_run(GTK_DIALOG(dialog));
   gtk_widget_destroy(dialog);
   g_free(text);
}
void gtk_serial_configure(GtkWindow *parent, const char *initial) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Local serial ports", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Delete…", 1002, "Port info…", 1003, "Save", GTK_RESPONSE_OK, NULL);
   GtkWidget *grid = gtk_grid_new();
   gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
   gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
   gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
   gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid);
   GtkWidget *names = gtk_combo_box_text_new_with_entry();
   port_names(GTK_COMBO_BOX_TEXT(names));
   struct port_editor editor = {
      .name = gtk_bin_get_child(GTK_BIN(names)), .type = gtk_combo_box_text_new(),
      .path = gtk_entry_new(), .baud = gtk_combo_box_text_new_with_entry(), .mode = gtk_entry_new(), .service = gtk_entry_new()
   };
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(editor.type), "pty", "Virtual serial port (PTY)");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(editor.type), "serial", "Physical serial / modem port");
   gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(editor.type), "file", "Capture file (append, output only)");
   static const char *baud_rates[] = {
      "300", "600", "1200", "2400", "4800", "9600", "14400", "19200",
      "28800", "38400", "57600", "115200", "230400", "460800",
      "500000", "576000", "921600", "1000000", "1152000",
      "1500000", "2000000", "2500000", "3000000", "3500000", "4000000"
   };

   for (unsigned i = 0 ; i < G_N_ELEMENTS(baud_rates) ; i++) {
      gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(editor.baud), baud_rates[i]);
   }

   gtk_entry_set_text(GTK_ENTRY(editor.name), initial && *initial ? initial : "ttyLOCAL0");
   load_port(NULL, &editor);
   g_signal_connect(names, "changed", G_CALLBACK(load_port), &editor);
   field(GTK_GRID(grid), "Name (select or type a new name)", names, 0);
   field(GTK_GRID(grid), "Endpoint type", editor.type, 1);
   field(GTK_GRID(grid), "Device / PTY link / capture path", editor.path, 2);
   GtkWidget *browse = gtk_button_new_with_label("Browse…");
   gtk_grid_attach(GTK_GRID(grid), browse, 2, 2, 1, 1);
   g_signal_connect(browse, "clicked", G_CALLBACK(browse_path), &editor);
   field(GTK_GRID(grid), "Baud", editor.baud, 3);
   field(GTK_GRID(grid), "Mode (e.g. 8n1)", editor.mode, 4);
   field(GTK_GRID(grid), "Startup service (none = choose later)", editor.service, 5);
   GtkWidget *remember = gtk_label_new("Changes remain in memory until /save.");
   gtk_grid_attach(GTK_GRID(grid), remember, 0, 6, 3, 1);
   GtkWidget *status = gtk_label_new("Disconnect an attached endpoint before editing. Files append received bytes only.");
   gtk_grid_attach(GTK_GRID(grid), status, 0, 7, 3, 1);
   gtk_widget_show_all(dialog);
   int response;
   while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_OK || response == 1002 || response == 1003) {
      if (response == 1003) {
         port_info(GTK_WINDOW(dialog), gtk_entry_get_text(GTK_ENTRY(editor.name)));
         continue;
      }

      if (response == 1002) {
         delete_port(GTK_WINDOW(dialog), gtk_entry_get_text(GTK_ENTRY(editor.name)), status);
         port_names(GTK_COMBO_BOX_TEXT(names));
         continue;
      }
      const char *name = gtk_entry_get_text(GTK_ENTRY(editor.name));
      bool valid = *name && strlen(name) < 64;

      for (const char *p = name ; *p ; p++) {
         if (!g_ascii_isalnum(*p) && *p != '_' && *p != '-') {
            valid = false;
         }
      }

      const char *type = gtk_combo_box_get_active_id(GTK_COMBO_BOX(editor.type));
      const char *path = gtk_entry_get_text(GTK_ENTRY(editor.path));
      rr_serial_settings_t settings = {
         .baud = 9600, .bits = 8, .parity = 'n', .stops = 1
      };
      char *spec = g_strdup_printf("port@%s,%s", baud_text(editor.baud), gtk_entry_get_text(GTK_ENTRY(editor.mode)));
      char target[16];
      valid = valid && type && rr_serial_spec_parse(spec, target, sizeof(target), &settings) && (!strcmp(type, "pty") || *path);
      g_free(spec);

      if (!valid || rr_serial_find(name)) {
         gtk_label_set_text(GTK_LABEL(status), "Check the name, baud, mode and path; disconnect this endpoint before editing.");
         continue;
      }
      dict *copy = dict_clone(cfg);

      if (!copy) {
         gtk_label_set_text(GTK_LABEL(status), "Unable to allocate configuration");
         continue;
      }
      const char *keys[] = {
         "type", "path", "baud", "mode"
      };
      const char *values[] = {
         type, path, baud_text(editor.baud), gtk_entry_get_text(GTK_ENTRY(editor.mode))
      };
      bool ok = true;

      for (unsigned i = 0 ; i < G_N_ELEMENTS(keys) ; i++) {
         char *key = g_strdup_printf("serial:%s.%s", name, keys[i]);

         /* Serial sections are extensible and registered by rr_serial_config_register(). */
         if (i == 1 && !*path) {
            dict_del(copy, key);
         } else {
            ok = dict_add(copy, key, values[i]) == 0 && ok;
         }
         g_free(key);
      }

      char *key = g_strdup_printf("serial.%s", name);
      const char *service = gtk_entry_get_text(GTK_ENTRY(editor.service));
      ok = dict_add(copy, key, *service ? service : "none") == 0 && ok;

      if (ok) {
         for (unsigned i = 0 ; i < G_N_ELEMENTS(keys) ; i++) {
            char *setting = g_strdup_printf("serial:%s.%s", name, keys[i]);

            if (i == 1 && !*path) {
               dict_del(cfg, setting);
            } else {
               dict_add(cfg, setting, values[i]);
            }
            g_free(setting);
         }

         dict_add(cfg, key, *service ? service : "none");
      }
      g_free(key);
      dict_free(copy);

      if (ok) {
         break;
      }
      gtk_label_set_text(GTK_LABEL(status), "Unable to update configuration; endpoint settings were not changed.");
   }
   gtk_widget_destroy(dialog);
}

struct attach_request {
   const char *name, *service;
   bool ok;
};
static void attach_port(void *data) {
   struct attach_request *request = data;
   request->ok = rr_sercom_attach(request->name, request->service, NULL);
}
char *gtk_serial_attach_named(GtkWindow *parent, const char *profile, const char *service) {
   GtkWidget *dialog = gtk_dialog_new_with_buttons("Connect serial resource", parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Cancel",
      GTK_RESPONSE_CANCEL, "Configure local ports…", 1001, "Connect", GTK_RESPONSE_ACCEPT, NULL);
   GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   char *context = g_strdup_printf("Server: %s\nResource: %s\nChoose a configured endpoint, or type a new name for a PTY.", profile, service);
   gtk_box_pack_start(GTK_BOX(box), gtk_label_new(context), FALSE, FALSE, 8);
   g_free(context);
   GtkWidget *combo = gtk_combo_box_text_new_with_entry();
   port_names(GTK_COMBO_BOX_TEXT(combo));
   GtkWidget *entry = gtk_bin_get_child(GTK_BIN(combo));
   char next_name[64];

   for (unsigned i = 0 ;; i++) {
      snprintf(next_name, sizeof(next_name), "ttyRESOURCE%u", i);

      if (!rr_serial_find(next_name)) {
         break;
      }
   }

   gtk_entry_set_text(GTK_ENTRY(entry), next_name);
   gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 8);
   gtk_widget_show_all(dialog);
   char *attached_name = NULL;
   int response;
   while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) == GTK_RESPONSE_ACCEPT || response == 1001) {
      if (response == 1001) {
         char *name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
         gtk_serial_configure(GTK_WINDOW(dialog), name);
         port_names(GTK_COMBO_BOX_TEXT(combo));
         gtk_entry_set_text(GTK_ENTRY(entry), name);
         g_free(name);
         continue;
      }
      rrconn_t *connection = rrclient_connection_find(profile);

      if (!connection || !connection->authenticated || !connection->is_ws) {
         message(GTK_WINDOW(dialog), "Connect to the resource's server first.");
         continue;
      }
      struct attach_request request = {
         .name = gtk_entry_get_text(GTK_ENTRY(entry)), .service = service
      };
      rrclient_connection_dispatch(connection, attach_port, &request);

      if (!request.ok) {
         message(GTK_WINDOW(dialog), "Unable to attach. Check whether this endpoint is already connected, its settings, path and server permissions.");
         continue;
      }
      attached_name = g_strdup(request.name);
      break;
   }
   gtk_widget_destroy(dialog);

   return attached_name;
}
bool gtk_serial_attach(GtkWindow *parent, const char *profile, const char *service) {
   char *name = gtk_serial_attach_named(parent, profile, service);
   bool attached = name != NULL;
   g_free(name);

   return attached;
}
