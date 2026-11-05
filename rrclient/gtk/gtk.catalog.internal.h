#ifndef RRCLIENT_GTK_CATALOG_INTERNAL_H
#define RRCLIENT_GTK_CATALOG_INTERNAL_H
/* Private implementation state shared by catalog modules. GTK owner thread only. */
#include <gtk/gtk.h>
#include <stdint.h>
#include <stdbool.h>
#include <rrclient/objects.h>

enum {
   NAME, KIND, STATE, OURS, DETAILS, UUID, ROOM, SERVICE, PROFILE, BINDING, SERVER_STATE, ACTION, JOINED, COORDINATES, GENERATION,
   CLIENT_SNAPSHOT, COLUMNS
};
enum {
   ROOM_BUTTON, SUB_BUTTON, SERIAL_BUTTON, SERVER_BUTTON, TERMINAL_BUTTON, BUTTONS
};
extern void gtk_terminal_open_endpoint(const char *name);
extern void gtk_terminal_open_endpoint_labeled(const char *name, const char *display_name);
typedef struct {
   char *profile, *domain, *direct_uuid, *request, *selection, *subscription;
   GPtrArray *inventory, *pending_inventory; /* complete and in-flight wire rows */
   GtkTreeIter domain_root, root, parents[5];
   GHashTable *irc_rows, *expanded;
   bool valid[5], loading, online, loaded, local_site_seen, root_valid;
   uint64_t requested_at;
   guint generation;
} resource_branch;
typedef struct {
   GtkWidget *page, *panel, *tree, *status, *details, *search, *buttons[BUTTONS];
   GtkTreeModelFilter *filter;
   GHashTable *enabled_kinds;
   GPtrArray *type_checks;
   GtkWidget *type_toggle_all;
   gboolean type_bulk_update;
   GHashTable *zebra_positions;
   gboolean zebra_dirty, zebra_rebuilding;
   guint zebra_idle;
   GtkTreeStore *store;
   GtkTreeIter local_root, serial_root, network_root, health_root;
   GHashTable *branches, *domains, *local_rows, *health_events, *health_last, *property_snapshots;
   guint timer;
   gulong initial_split_handler;
   double scroll_y;
   bool scroll_saved;
} resource_browser;

extern resource_browser *browser;
void resource_set_string(GtkTreeIter *iter, int column, const char *value);
char *resource_display_utf8(const char *value);
char *resource_location_details(const char *coords, const char *grid, gboolean compact);
gint resource_tree_compare(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *, gpointer);
void resource_local_configure(GtkButton *, gpointer);
void resource_files(GtkButton *, gpointer);
void resource_zebra_structure_changed(gpointer);
void resource_zebra_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
GtkWindow *resource_parent(void);
void resource_present(void);
void resource_initial_split(GtkWidget *, GtkAllocation *, gpointer);
gboolean resource_search_equal(GtkTreeModel *, gint, const gchar *, GtkTreeIter *, gpointer);
void resource_search_changed(GtkEditable *, gpointer);
gboolean resource_type_visible(GtkTreeModel *, GtkTreeIter *, gpointer);
void resource_type_sync_all(void);
void resource_type_toggle_all(GtkToggleButton *, gpointer);
void resource_type_changed(GtkToggleButton *, gpointer);
void resource_icon_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
void resource_cat_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);

GtkTreePath *resource_view_path(GtkTreePath *child_path);
gboolean resource_find_uuid(GtkTreeIter *root, const char *uuid, GtkTreeIter *found);
void resource_health_queue_free(gpointer value);
void resource_health_update(void);
void resource_health_jump(GtkTreeModel *model, GtkTreeIter *item);
void resource_local_update(void);
void resource_domain_assign(resource_branch *branch, const char *identity);
void resource_domain_rebuild(const char *domain);
void resource_expansion_save(resource_branch *branch);
void resource_expansion_restore(resource_branch *branch);
gboolean resource_restore_scroll(gpointer unused);
void resource_update_branch(resource_branch *branch);
void resource_row(const char *event, const char *text, rrconn_t *connection, void *user);

void resource_property_result(const char *event, const char *payload, rrconn_t *connection, void *user);
const dict *resource_property_cached(const char *profile, const char *uuid, const char *property, const char *request_id);
char *resource_property_request(const char *profile, const char *uuid, const char *property);
void resource_rig_control_dialog(GtkTreeModel *model, GtkTreeIter *item, const char *profile, const char *name, const char *property);
void resource_menu_open(GtkTreeModel *model, GtkTreeIter *item, const char *profile, const char *title, const char *id);

#endif
