#ifndef RRCLIENT_GTK_ADMIN_CELLS_H
#define RRCLIENT_GTK_ADMIN_CELLS_H
#include <gtk/gtk.h>
typedef struct {
   const char *name;
   const char *fields[10];
   GtkListStore *store;
   GtkWidget *tree, *older, *status;
   char profile[128], next[32];
} admin_table_t;


void admin_uuid_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
gboolean admin_uuid_menu(GtkWidget *, GdkEventButton *, gpointer);
void admin_column_wrap_width(GObject *, GParamSpec *, gpointer);
void admin_user_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
void admin_session_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
void admin_numeric_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
void admin_log_stripe_cell(GtkTreeViewColumn *, GtkCellRenderer *, GtkTreeModel *, GtkTreeIter *, gpointer);
char *admin_display_utf8(const char *value, const char *field);
#endif
