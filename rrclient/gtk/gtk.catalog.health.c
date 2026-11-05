/* Cluster health from received catalog snapshots; GTK owner thread only. */
#include "gtk.catalog.internal.h"
#include <string.h>
#include <stdio.h>

/* Cluster Health is a local index over already received inventory, not a second subscription or a claim that linked-node metrics were measured locally. */
/* Thresholds are deliberately conservative and only use interval measurements. Missing/disabled counters are unknown, never treated as a healthy sample. */
static double resource_health_number(const char *description, const char *label) {
   const char *p = description ? strstr(description, label) : NULL;

   if (!p) {
      return -1;
   }
   p += strlen(label);
   char *tail = NULL;
   double value = g_ascii_strtod(p, &tail);

   return (tail == p || (!g_ascii_isdigit(*p) && *p != '.')) ? -1 : value;
}
static void resource_health_raise(unsigned *severity, GString *reasons, unsigned level, const char *reason) {
   if (level > *severity) {
      *severity = level;
   }

   if (reasons->len) {
      g_string_append(reasons, "; ");
   }
   g_string_append(reasons, reason);
}
static gboolean resource_health_group(GtkTreeIter *node, GtkTreeIter *found, unsigned *total, unsigned *bad, unsigned *severity, GString *reasons) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, node) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *name = NULL, *kind = NULL;
      gtk_tree_model_get(model, &child, NAME, &name, KIND, &kind, -1);
      gboolean group = !g_strcmp0(kind, "group") && !g_strcmp0(name, "Statistics / Performance");
      g_free(name);
      g_free(kind);

      if (!group) {
         continue;
      }
      *found = child;
      GtkTreeIter stat;

      for (gboolean have = gtk_tree_model_iter_children(model, &stat, &child) ; have ; have = gtk_tree_model_iter_next(model, &stat)) {
         char *state = NULL, *stat_kind = NULL, *stat_name = NULL, *details = NULL;
         gtk_tree_model_get(model, &stat, KIND, &stat_kind, STATE, &state, NAME, &stat_name, DETAILS, &details, -1);

         if (!g_strcmp0(stat_kind, "performance-stat")) {
            ++*total;

            if (!g_strcmp0(state, "Critical") || !g_strcmp0(state, "Warning")) {
               unsigned level = !g_strcmp0(state, "Critical") ? 2 : 1;

               if (stat_name && g_str_has_prefix(stat_name, "Neighbor ") && details) {
                  /* Include the actual link diagnosis in the Cluster Health reason, rather than only the generic performance row name. */
                  const char *condition = strstr(details, "Latency condition: ");
                  const char *flaps = strstr(details, "Unexpected disconnects (5m): ");
                  const char *rtt = strstr(details, "Last RTT: ");
                  char diagnosis[256] = "";
                  long long value = -1;
                  unsigned count = 0;

                  if (condition && strstr(condition, "degraded") == condition + strlen("Latency condition: ")) {
                     g_strlcat(diagnosis, "latency degraded; ", sizeof(diagnosis));
                  }

                  if (flaps && sscanf(flaps, "Unexpected disconnects (5m): %u", &count) == 1 && count) {
                     char text[64];
                     g_snprintf(text, sizeof(text), "%u unexpected disconnects (5m); ", count);
                     g_strlcat(diagnosis, text, sizeof(diagnosis));
                  }

                  if (rtt && sscanf(rtt, "Last RTT: %lld", &value) == 1 && value >= 0) {
                     char text[64];
                     g_snprintf(text, sizeof(text), "RTT %lld ms", value);
                     g_strlcat(diagnosis, text, sizeof(diagnosis));
                  }
                  char *reason = g_strdup_printf("%s: %s", stat_name, *diagnosis ? diagnosis : state);
                  resource_health_raise(severity, reasons, level, reason);
                  g_free(reason);
               } else {
                  resource_health_raise(severity, reasons, level, stat_name ? stat_name : state);
               }
            }

            if (state && (strstr(state, "Unavailable") || strstr(state, "Failed") || strstr(state, "offline"))) {
               ++*bad;
               resource_health_raise(severity, reasons, 1, stat_name ? stat_name : "Unavailable counter");
            } else if (details && stat_name && strstr(details, "Last interval:")) {
               double value = -1;

               if (!strcmp(stat_name, "Event handlers")) {
                  const char *deferred = strstr(details, "deferred completions (");
                  const char *wait = strstr(details, "queue wait ");

                  if (deferred) {
                     value = resource_health_number(deferred, "deferred completions (");

                     if (value >= 50000) {
                        resource_health_raise(severity, reasons, 2, "Event callbacks critically slow");
                     } else if (value >= 10000) {
                        resource_health_raise(severity, reasons, 1, "Event callbacks slow");
                     }
                  }

                  if (wait) {
                     value = resource_health_number(wait, "queue wait ");

                     if (value >= 100000) {
                        resource_health_raise(severity, reasons, 2, "Event queue wait critical");
                     } else if (value >= 20000) {
                        resource_health_raise(severity, reasons, 1, "Event queue wait elevated");
                     }
                  }
               } else if (!strcmp(stat_name, "Catalog synchronization")) {
                  value = resource_health_number(details, " timeouts ");

                  if (value >= 5) {
                     resource_health_raise(severity, reasons, 2, "Catalog ACK timeouts critical");
                  } else if (value >= 1) {
                     resource_health_raise(severity, reasons, 1, "Catalog ACK timeouts");
                  }
               }
            }
         }
         g_free(state);
         g_free(stat_kind);
         g_free(stat_name);
         g_free(details);
      }

      return TRUE;
   }

   return FALSE;
}

/* Bounded in-memory history of state transitions in this client session. */
static void resource_health_queue_free(gpointer value) {
   g_queue_free_full(value, g_free);
}
static void resource_health_history(const char *key, const char *status, const char *reason, GtkTreeIter *node) {
   const char *previous = g_hash_table_lookup(browser->health_last, key);
   GQueue *events = g_hash_table_lookup(browser->health_events, key);

   if (!events) {
      events = g_queue_new();
      g_hash_table_insert(browser->health_events, g_strdup(key), events);
   }
   gboolean changed = g_strcmp0(previous, status) != 0;

   if (changed) {
      GDateTime *now = g_date_time_new_now_local();
      char *stamp = g_date_time_format(now, "%Y-%m-%d %H:%M:%S");
      char *entry = g_strdup_printf("%s  %s%s%s", stamp, status, reason && *reason ? ": " : "", reason && *reason ? reason : "");
      g_queue_push_head(events, entry);
      while (g_queue_get_length(events) > 16) {
         g_free(g_queue_pop_tail(events));
      }
      g_hash_table_replace(browser->health_last, g_strdup(key), g_strdup(status));
      g_free(stamp);
      g_date_time_unref(now);
   }
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter group;
   gboolean exists = gtk_tree_model_iter_children(model, &group, node);

   if (!exists) {
      gtk_tree_store_append(browser->store, &group, node);
   }

   if (exists && !changed) {
      return;
   }
   gtk_tree_store_set(browser->store, &group, NAME, "Health History", KIND, "group", STATE, "Session history", -1);
   GtkTreeIter row;
   while (gtk_tree_model_iter_children(model, &row, &group)) {
      gtk_tree_store_remove(browser->store, &row);
   }

   for (GList *it = events->head ; it ; it = it->next) {
      gtk_tree_store_append(browser->store, &row, &group);
      gtk_tree_store_set(browser->store, &row, NAME, it->data, KIND, "health-event", DETAILS, it->data, -1);
   }
}

/* Present persisted observations already received in the server's performance inventory. Each link report is owned by the reporting (direct) server. */
/* Only timestamped history records belong in this subtree. Inventory descriptions can carry serialized metadata (:access, :description, etc.);
 * don't turn those into fake health events. ISO-8601 UTC timestamps sort lexically, so sorting descending also merges multiple neighbors correctly. */
static gint resource_health_event_desc(gconstpointer a, gconstpointer b) {
   const char *left = *(char *const *)a;
   const char *right = *(char *const *)b;

   return g_strcmp0(right, left);
}

static gboolean resource_health_named_child(GtkTreeIter *parent, const char *name, GtkTreeIter *out);
static void resource_health_link_events(GtkTreeIter *performance, GtkTreeIter *node) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GPtrArray *events = g_ptr_array_new_with_free_func(g_free);
   GtkTreeIter stat;

   for (gboolean ok = gtk_tree_model_iter_children(model, &stat, performance) ; ok ; ok = gtk_tree_model_iter_next(model, &stat)) {
      char *name = NULL, *details = NULL;
      gtk_tree_model_get(model, &stat, NAME, &name, DETAILS, &details, -1);

      if (name && g_str_has_prefix(name, "Neighbor ") && g_str_has_suffix(name, " recent events") && details) {
         gchar **lines = g_strsplit(details, "\n", -1);

         for (guint i = 0 ; lines[i] ; ++i) {
            const char *line = lines[i];

            /* SQLite exporter emits YYYY-MM-DD HH:MM:SSZ; accept T too. */
            if (strlen(line) < 20 ||
               !g_ascii_isdigit(line[0]) || !g_ascii_isdigit(line[1]) ||
               !g_ascii_isdigit(line[2]) || !g_ascii_isdigit(line[3]) ||
               line[4] != '-' || line[7] != '-' || (line[10] != ' ' && line[10] != 'T') || line[13] != ':' || line[16] != ':') {
               continue;
            }
            g_ptr_array_add(events, g_strdup(line));
         }

         g_strfreev(lines);
      }
      g_free(name);
      g_free(details);
   }

   g_ptr_array_sort(events, resource_health_event_desc);
   GString *report = g_string_new("");

   for (guint i = 0 ; i < events->len ; ++i) {
      const char *line = g_ptr_array_index(events, i);
      g_string_append_printf(report, "%s\n", line);
   }

   GtkTreeIter group;
   gboolean exists = resource_health_named_child(node, "Server Link Events", &group);

   if (!exists && !events->len) {
      g_string_free(report, TRUE);
      g_ptr_array_free(events, TRUE);

      return;
   }

   if (!exists) {
      gtk_tree_store_insert(browser->store, &group, node, 1);
   }
   char *previous = NULL;
   gtk_tree_model_get(model, &group, DETAILS, &previous, -1);
   gboolean changed = g_strcmp0(previous, report->str) != 0;
   g_free(previous);

   if (changed || !exists) {
      gtk_tree_store_set(browser->store, &group, NAME, "Server Link Events", KIND, "group", STATE, "SQLite history", DETAILS, report->str, -1);
      GtkTreeIter child;
      while (gtk_tree_model_iter_children(model, &child, &group)) {
         gtk_tree_store_remove(browser->store, &child);
      }

      for (guint i = 0 ; i < events->len ; ++i) {
         const char *line = g_ptr_array_index(events, i);
         gtk_tree_store_append(browser->store, &child, &group);
         gtk_tree_store_set(browser->store, &child, NAME, line, KIND, "health-event", DETAILS, line, -1);
      }
   }
   g_string_free(report, TRUE);
   g_ptr_array_free(events, TRUE);
}

/* Mirror inventory performance rows rather than moving them: the ordinary explorer and Cluster Health remain independent views of the same data. Reuse existing
 * child rows to retain their expansion and selection state. */
static gboolean resource_health_named_child(GtkTreeIter *parent, const char *name, GtkTreeIter *out) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);

   for (gboolean ok = gtk_tree_model_iter_children(model, out, parent) ; ok ; ok = gtk_tree_model_iter_next(model, out)) {
      char *current = NULL;
      gtk_tree_model_get(model, out, NAME, &current, -1);
      gboolean match = !g_strcmp0(current, name);
      g_free(current);

      if (match) {
         return TRUE;
      }
   }

   return FALSE;
}
static void resource_health_performance(GtkTreeIter *source, gboolean available, GtkTreeIter *node) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter mirror;
   gboolean exists = resource_health_named_child(node, "Statistics / Performance", &mirror);

   if (!available) {
      if (exists) {
         gtk_tree_store_remove(browser->store, &mirror);
      }

      return;
   }

   if (!exists) {
      gtk_tree_store_append(browser->store, &mirror, node);
      gtk_tree_store_set(browser->store, &mirror, NAME, "Statistics / Performance", KIND, "group", -1);
   }
   GtkTreeIter src;
   unsigned index = 0;

   for (gboolean ok = gtk_tree_model_iter_children(model, &src, source) ; ok ; ok = gtk_tree_model_iter_next(model, &src)) {
      char *name = NULL, *kind = NULL, *state = NULL, *details = NULL;
      gtk_tree_model_get(model, &src, NAME, &name, KIND, &kind, STATE, &state, DETAILS, &details, -1);
      GtkTreeIter dst;

      if (!gtk_tree_model_iter_nth_child(model, &dst, &mirror, index)) {
         gtk_tree_store_append(browser->store, &dst, &mirror);
      }
      resource_set_string(&dst, NAME, name);
      resource_set_string(&dst, KIND, kind);
      resource_set_string(&dst, STATE, state);
      resource_set_string(&dst, DETAILS, details);
      /* Mirrored diagnostics are read-only: no commands or stale remote resource bindings should be inherited from source rows. */
      ++index;
      g_free(name);
      g_free(kind);
      g_free(state);
      g_free(details);
   }

   GtkTreeIter stale;
   while (gtk_tree_model_iter_nth_child(model, &stale, &mirror, index)) {
      gtk_tree_store_remove(browser->store, &stale);
   }
}
static void resource_health_node(resource_branch *branch, GtkTreeIter *node, gboolean direct, GHashTable *seen, unsigned *reporting, unsigned *warnings) {
   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   char *name = NULL, *uuid = NULL;
   gtk_tree_model_get(model, node, NAME, &name, UUID, &uuid, -1);
   GtkTreeIter group;
   unsigned total = 0, bad = 0, severity = 0;
   GString *reasons = g_string_new("");
   gboolean available = resource_health_group(node, &group, &total, &bad, &severity, reasons);

   if (direct || available) {
      /* A node identity is scoped to its mesh domain, never globally by
       * display name: two domains may contain identically named stations. */
      char *key = g_strdup_printf("node|%s|%s",
         branch->domain ? branch->domain : branch->profile,
         uuid && *uuid ? uuid : name && *name ? name : branch->profile);

      if (g_hash_table_contains(seen, key)) {
         g_free(key);
         goto health_children;
      }
      g_hash_table_add(seen, g_strdup(key));
      GtkTreeIter item, child;
      gboolean found = FALSE;

      for (gboolean ok = gtk_tree_model_iter_children(model, &child, &browser->health_root) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
         char *binding = NULL;
         gtk_tree_model_get(model, &child, BINDING, &binding, -1);
         found = !g_strcmp0(key, binding);
         g_free(binding);

         if (found) {
            item = child;
            break;
         }
      }

      if (!found) {
         gtk_tree_store_append(browser->store, &item, &browser->health_root);
         gtk_tree_store_set(browser->store, &item, KIND, "cluster-node", PROFILE, branch->profile, BINDING, key, ACTION, direct ? "root" : "site", UUID, uuid ?
            uuid : "", -1);
      }
      const char *status = !branch->online ? "Offline" :
         !available || !total ? (branch->loading && !branch->loaded ? "Refreshing" : "No telemetry") :
         severity == 2 ? "Critical" : severity == 1 ? "Warning" : "Reporting";

      if (branch->online && total && !severity) {
         ++*reporting;
      }

      if (!branch->online || severity) {
         ++*warnings;
      }
      char *details = g_strdup_printf("%s via %s. %u performance entries, %u unavailable. %s%sDouble-click to open Statistics / Performance.", direct ?
         "Direct connection" : "Linked node", branch->profile, total, bad, reasons->len ? reasons->str : "No threshold violations reported. ", reasons->len ?
         ". " : "");
      resource_set_string(&item, NAME, name ? name : branch->profile);
      resource_set_string(&item, STATE, status);
      resource_set_string(&item, DETAILS, details);
      resource_health_history(key, status, reasons->str, &item);

      if (direct && available) {
         resource_health_link_events(&group, &item);
      }
      resource_health_performance(&group, available, &item);
      g_free(details);
      g_free(key);
   }
   g_string_free(reasons, TRUE);
health_children:
   /* Include linked sites, but not rigs, VFOs or unrelated inventory groups. */
   GtkTreeIter child;

   for (gboolean ok = gtk_tree_model_iter_children(model, &child, node) ; ok ; ok = gtk_tree_model_iter_next(model, &child)) {
      char *kind = NULL;
      gtk_tree_model_get(model, &child, KIND, &kind, -1);

      if (!g_strcmp0(kind, "site")) {
         resource_health_node(branch, &child, FALSE, seen, reporting, warnings);
      }
      g_free(kind);
   }

   g_free(name);
   g_free(uuid);
}
void resource_health_update(void) {
   if (!browser) {
      return;
   }
   GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
   unsigned reporting = 0, warnings = 0, nodes = 0;
   GHashTableIter branches;
   gpointer value;
   g_hash_table_iter_init(&branches, browser->branches);

   /* Direct nodes win over relayed copies regardless of hash iteration order. */
   for (unsigned pass = 0 ; pass < 2 ; ++pass) {
      g_hash_table_iter_init(&branches, browser->branches);
      while (g_hash_table_iter_next(&branches, NULL, &value)) {
         resource_branch *branch = value;

         if ((pass == 0) != branch->online) {
            continue;
         }

         /* Connection state is supplied by the core status snapshot. */
         if (branch->root_valid && branch->online) {
            resource_health_node(branch, &branch->root, TRUE, seen, &reporting, &warnings);
            GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
            GtkTreeIter node;
            for (gboolean ok = gtk_tree_model_iter_children(model, &node, &branch->domain_root);
                 ok; ok = gtk_tree_model_iter_next(model, &node)) {
               char *kind = NULL, *uuid = NULL, *direct_uuid = NULL;
               gtk_tree_model_get(model, &node, KIND, &kind, UUID, &uuid, -1);
               gtk_tree_model_get(model, &branch->root, UUID, &direct_uuid, -1);
               if (!g_strcmp0(kind, "site") && uuid && *uuid && g_strcmp0(uuid, direct_uuid))
                  resource_health_node(branch, &node, FALSE, seen, &reporting, &warnings);
               g_free(kind);
               g_free(uuid);
               g_free(direct_uuid);
            }
         }
      }
   }

   GtkTreeModel *model = GTK_TREE_MODEL(browser->store);
   GtkTreeIter child;
   gboolean ok = gtk_tree_model_iter_children(model, &child, &browser->health_root);
   while (ok) {
      char *key = NULL;
      gtk_tree_model_get(model, &child, BINDING, &key, -1);

      if (!key || !g_hash_table_contains(seen, key)) {
         ok = gtk_tree_store_remove(browser->store, &child);
      } else {
         ++nodes;
         ok = gtk_tree_model_iter_next(model, &child);
      }
      g_free(key);
   }
   char *summary =
      g_strdup_printf("%u nodes, %u reporting, %u with offline/unavailable metrics. Unknown telemetry is not considered healthy.", nodes, reporting, warnings);
   resource_set_string(&browser->health_root, DETAILS, summary);
   resource_set_string(&browser->health_root, STATE, warnings ? "Warning" : nodes && nodes == reporting ? "Reporting" : "Incomplete");
   g_free(summary);
   g_hash_table_destroy(seen);
}
void resource_health_jump(GtkTreeModel *model, GtkTreeIter *item) {
   char *profile = NULL, *uuid = NULL, *action = NULL;
   gtk_tree_model_get(model, item, PROFILE, &profile, UUID, &uuid, ACTION, &action, -1);
   resource_branch *branch = profile ? g_hash_table_lookup(browser->branches, profile) : NULL;

   if (branch) {
      GtkTreeIter node = branch->root, group;

      if (g_strcmp0(action, "root") && (!uuid || !*uuid || !resource_find_uuid(&branch->domain_root, uuid, &node))) {
         gtk_label_set_text(GTK_LABEL(browser->status), "Cannot locate this linked node in the current inventory.");
      } else {
         unsigned total = 0, bad = 0, severity = 0;
         GString *reasons = g_string_new("");
         gboolean found = resource_health_group(&node, &group, &total, &bad, &severity, reasons);
         g_string_free(reasons, TRUE);

         if (found) {
            GtkTreePath *child_path = gtk_tree_model_get_path(GTK_TREE_MODEL(browser->store), &group);
            GtkTreePath *path = resource_view_path(child_path);

            if (path) {
               gtk_tree_view_expand_to_path(GTK_TREE_VIEW(browser->tree), path);
               gtk_tree_view_set_cursor(GTK_TREE_VIEW(browser->tree), path, NULL, FALSE);
               gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(browser->tree), path, NULL, TRUE, 0.4, 0);
               gtk_tree_path_free(path);
            }
            gtk_tree_path_free(child_path);
         } else {
            gtk_label_set_text(GTK_LABEL(browser->status), "No Statistics / Performance subtree is available for this node.");
         }
      }
   }
   g_free(profile);
   g_free(uuid);
   g_free(action);
}

