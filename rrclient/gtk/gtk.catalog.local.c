/* Local serial, PTY and WSJT-X catalog projection; GTK owner thread only. */
#include "gtk.catalog.internal.h"
#include "gtk.serial.h"
#include <rrclient/wsjtx.h>
#include <rrclient/sercom.h>
#include <librustyaxe/core.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>

/* Local serial endpoints remain visible even when a server/resource disappears. */
struct local_scan {
   GHashTable *seen;
};
static void resource_local_named(const char *name, struct local_scan *scan) {
   if (!strcmp(name, "wsjtx")) {
      return;
   }
   rr_sercom_info_t info;
   bool attached = rr_sercom_info(name, &info);

   if (!attached) {
      char *key = g_strdup_printf("serial.%s", name);
      const char *service = cfg_get(key);
      g_free(key);
      key = g_strdup_printf("serial:%s.path", name);
      const char *path = cfg_get(key);
      g_free(key);
      info = (rr_sercom_info_t) {
         name, service ? service : "none", "", path ? path : "Automatic PTY path", "Configured"
      };
   }
   g_hash_table_add(scan->seen, g_strdup(name));
   GtkTreeRowReference *reference = g_hash_table_lookup(browser->local_rows, name);
   GtkTreeIter iter;

   if (reference) {
      GtkTreePath *path = gtk_tree_row_reference_get_path(reference);

      if (!path) {
         return;
      }
      gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path);
      gtk_tree_path_free(path);
   } else {
      gtk_tree_store_append(browser->store, &iter, &browser->serial_root);
      GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
      reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), path);
      gtk_tree_path_free(path);
      g_hash_table_insert(browser->local_rows, g_strdup(name), reference);
      gtk_tree_store_set(browser->store, &iter, NAME, name, KIND, attached ? "attachment" : "local-port", BINDING, attached ? name : "", -1);
   }
   char *details = g_strdup_printf("%s → %s · %s", info.path, info.service, *info.profile ? info.profile : "follows active server");
   resource_set_string(&iter, DETAILS, details);
   resource_set_string(&iter, STATE, info.state);
   resource_set_string(&iter, KIND, attached ? "attachment" : "local-port");
   resource_set_string(&iter, BINDING, attached ? name : "");
   resource_set_string(&iter, OURS, !attached ? "Not connected" :
      !strcmp(info.state, "Attached") || !strcmp(info.state, "Connected") ? "Attached locally" : "Not connected");
   resource_set_string(&iter, PROFILE, info.profile);
   resource_set_string(&iter, SERVICE, info.service);
   g_free(details);
}

/* Physical serial devices are discovered independently of configured PTYs. Do not open devices just to enumerate them. */
static void resource_local_device(const char *name, const char *path, struct local_scan *scan) {
   if (g_hash_table_contains(scan->seen, name)) {
      return;
   }
   g_hash_table_add(scan->seen, g_strdup(name));
   GtkTreeRowReference *reference = g_hash_table_lookup(browser->local_rows, name);
   GtkTreeIter iter;

   if (reference) {
      GtkTreePath *tree_path = gtk_tree_row_reference_get_path(reference);

      if (!tree_path) {
         return;
      }
      gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, tree_path);
      gtk_tree_path_free(tree_path);
   } else {
      gtk_tree_store_append(browser->store, &iter, &browser->serial_root);
      GtkTreePath *tree_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &iter);
      reference = gtk_tree_row_reference_new(GTK_TREE_MODEL(browser->store), tree_path);
      gtk_tree_path_free(tree_path);
      g_hash_table_insert(browser->local_rows, g_strdup(name), reference);
   }
   resource_set_string(&iter, NAME, name);
   resource_set_string(&iter, KIND, "device");
   resource_set_string(&iter, STATE, "Available");
   resource_set_string(&iter, DETAILS, path);
   resource_set_string(&iter, BINDING, "");
   resource_set_string(&iter, SERVICE, "local.raw");
   resource_set_string(&iter, PROFILE, "");
   resource_set_string(&iter, OURS, "Physical serial device");
}
static bool resource_serial_device_name(const char *name) {
   static const char *prefixes[] = {
      "ttyS", "ttyUSB", "ttyACM", "ttyAMA", "ttyTHS", "ttyXRUSB", "rfcomm"
   };

   for (guint i = 0 ; i < G_N_ELEMENTS(prefixes) ; i++) {
      if (g_str_has_prefix(name, prefixes[i])) {
         return true;
      }
   }

   return false;
}

/* Legacy serial8250 registers placeholder ttyS ports on many PCs.
 * /proc/tty/driver/serial identifies these as uart:unknown; don't hide configured endpoints, only raw auto-discovered device nodes. */
static gboolean resource_serial_placeholder(const char *name) {
   if (!g_str_has_prefix(name, "ttyS")) {
      return FALSE;
   }
   const char *suffix = name + 4;

   if (!g_ascii_isdigit(*suffix)) {
      return FALSE;
   }

   for (const char *p = suffix ; *p ; ++p) {
      if (!g_ascii_isdigit(*p)) {
         return FALSE;
      }
   }

   gchar *contents = NULL;

   if (!g_file_get_contents("/proc/tty/driver/serial", &contents, NULL, NULL)) {
      return FALSE;
   }
   guint number = (guint) g_ascii_strtoull(suffix, NULL, 10);
   gboolean unknown = FALSE;
   gchar **lines = g_strsplit(contents, "\n", -1);

   for (guint i = 0 ; lines[i] ; ++i) {
      char *p = lines[i];
      while (g_ascii_isspace(*p)) {
         ++p;
      }
      char *end = NULL;
      guint64 index = g_ascii_strtoull(p, &end, 10);

      if (end != p && *end == ':' && index == number) {
         unknown = strstr(end, "uart:unknown") != NULL;
         break;
      }
   }

   g_strfreev(lines);
   g_free(contents);

   return unknown;
}
static void resource_discover_devices(struct local_scan *scan) {
   GDir *dir = g_dir_open("/dev", 0, NULL);

   if (!dir) {
      return;
   }
   const char *name;
   while ((name = g_dir_read_name(dir))) {
      if (!resource_serial_device_name(name) || g_hash_table_contains(scan->seen, name) || resource_serial_placeholder(name)) {
         continue;
      }
      char *path = g_build_filename("/dev", name, NULL);
      struct stat st;

      if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) {
         resource_local_device(name, path, scan);
      }
      g_free(path);
   }
   g_dir_close(dir);
}
static void resource_local_port(rr_serial_t *port, void *data) {
   resource_local_named(rr_serial_name(port), data);
}

/* WSJT-X is a client-local UDP service. Reuse existing rows to avoid disturbing Explorer expansion and selection every two seconds. */
typedef struct {
   GtkTreeIter root;
} wsjtx_tree_context;
static GtkTreeIter wsjtx_tree_child(GtkTreeIter *parent, const char *name, const char *kind) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, parent) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *old_name = NULL, *old_kind = NULL;
      gtk_tree_model_get(model, &child, NAME, &old_name, KIND, &old_kind, -1);
      gboolean match = !g_strcmp0(old_name, name) && !g_strcmp0(old_kind, kind);
      g_free(old_name);
      g_free(old_kind);

      if (match) {
         return child;
      }
   }

   gtk_tree_store_append(browser->store, &child, parent);
   gtk_tree_store_set(browser->store, &child, NAME, name, KIND, kind, -1);

   return child;
}
static void wsjtx_tree_field(const char *id, const char *group, const char *key, const char *value, void *data) {
   wsjtx_tree_context *ctx = data;
   GtkTreeIter peer = wsjtx_tree_child(&ctx->root, id, "network-service");
   GtkTreeIter folder = wsjtx_tree_child(&peer, group, "group");
   GtkTreeIter field = wsjtx_tree_child(&folder, key, "network-service");
   resource_set_string(&field, STATE, value);
}
void resource_local_update(void) {
   struct local_scan scan = {
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL)
   };
   rr_serial_foreach(resource_local_port, &scan);
   GList *configured = gtk_serial_configured_names();

   for (GList *item = configured ; item ; item = item->next) {
      if (!g_hash_table_contains(scan.seen, item->data)) {
         resource_local_named(item->data, &scan);
      }
   }

   g_list_free_full(configured, g_free);
   resource_discover_devices(&scan);
   {
      GtkTreeIter service;
      GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
      gboolean found = FALSE;

      for (gboolean more = gtk_tree_model_iter_children(model, &service, &browser->network_root) ; more ; more = gtk_tree_model_iter_next(model, &service)) {
         char *name = NULL;
         gtk_tree_model_get(model, &service, NAME, &name, -1);
         found = !g_strcmp0(name, "WSJTX Reporting");
         g_free(name);

         if (found) {
            break;
         }
      }

      if (!found) {
         gtk_tree_store_append(browser->store, &service, &browser->network_root);
         gtk_tree_store_set(browser->store, &service, NAME, "WSJTX Reporting", KIND, "network-service", -1);
      }
      char status[64], connection[128], details[640];
      rrclient_wsjtx_status(status, sizeof(status), connection, sizeof(connection), details, sizeof(details));
      resource_set_string(&service, STATE, status);
      resource_set_string(&service, OURS, connection);
      resource_set_string(&service, DETAILS, details);
      wsjtx_tree_context ctx = {
         .root = service
      };
      rrclient_wsjtx_foreach_field(wsjtx_tree_field, &ctx);
   }
   GHashTableIter cursor;
   gpointer name, value;
   g_hash_table_iter_init(&cursor, browser->local_rows);
   while (g_hash_table_iter_next(&cursor, &name, &value)) {
      if (g_hash_table_contains(scan.seen, name)) {
         continue;
      }
      GtkTreePath *path = gtk_tree_row_reference_get_path(value);
      GtkTreeIter iter;

      if (path && gtk_tree_model_get_iter(GTK_TREE_MODEL(browser->store), &iter, path)) {
         gtk_tree_store_remove(browser->store, &iter);
      }

      if (path) {
         gtk_tree_path_free(path);
      }
      g_hash_table_iter_remove(&cursor);
   }
   char *count = g_strdup_printf("%u serial endpoints", g_hash_table_size(scan.seen));
   resource_set_string(&browser->local_root, OURS, count);
   g_free(count);
   g_hash_table_destroy(scan.seen);
}

