/* GTK client and host logs share one structured LogView. TUI logging remains separate and unchanged. */
#include <gtk/gtk.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librustyaxe/logger.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/gtk/gtk.core.h>
#include <rrclient/gtk/gtk.logview.h>

extern GtkWidget *main_notebook;
extern bool dying;
static GtkLogView *shared_log;

/* Logger and wire callbacks can originate on the core thread. Never
 * touch GtkTextBuffer outside the GTK owner. Snapshot the entry first. */
typedef struct {
   gboolean host;
   gchar *source, *subsys, *message;
   logpriority_t level;
} GtkQueuedLog;
static GThread *gtk_log_owner;
static gboolean gtk_queued_log_deliver(gpointer data) {
   GtkQueuedLog *entry = data;
   if (!dying && shared_log)
      gtk_logview_append(shared_log, entry->host, entry->source,
                         entry->level, entry->subsys, entry->message);
   return G_SOURCE_REMOVE;
}
static void gtk_queued_log_free(gpointer data) {
   GtkQueuedLog *entry = data;
   g_free(entry->source);
   g_free(entry->subsys);
   g_free(entry->message);
   g_free(entry);
}
static void gtk_log_submit(gboolean host, const char *source,
                           logpriority_t level, const char *subsys,
                           const char *message) {
   GtkQueuedLog *entry = g_new0(GtkQueuedLog, 1);
   entry->host = host;
   entry->source = g_strdup(source ? source : "");
   entry->subsys = g_strdup(subsys ? subsys : "");
   entry->message = g_strdup(message ? message : "");
   entry->level = level;
   if (g_thread_self() == gtk_log_owner) {
      gtk_queued_log_deliver(entry);
      gtk_queued_log_free(entry);
      return;
   }
   GSource *source_idle = g_idle_source_new();
   g_source_set_callback(source_idle, gtk_queued_log_deliver,
                         entry, gtk_queued_log_free);
   g_source_attach(source_idle, g_main_context_default());
   g_source_unref(source_idle);
}

static void host_log_frame_handler(const char *event, const void *data, size_t len, rrconn_t *connection, void *user) {
   (void)event;
   (void)user;

   if (dying || !shared_log || !data || len < RR_LOGFRAME_HDR_LEN) {
      return;
   }
   const uint8_t *bytes = data;
   logpriority_t level = (logpriority_t) bytes[0];
   char subsys[17];
   memcpy(subsys, bytes + 1, 16);
   subsys[16] = '\0';

   if (level == LOG_CRAZY || debug_filter(subsys, level)) {
      return;
   }
   const char *payload = (const char *)bytes + RR_LOGFRAME_HDR_LEN;
   size_t max = len - RR_LOGFRAME_HDR_LEN;
   gchar *message = g_strndup(payload, strnlen(payload, max));
   const char *source = connection && connection->server && connection->server->network[0] ?
      connection->server->network : connection && connection->hostname[0] ? connection->hostname : "host";
   gtk_log_submit(TRUE, source, level, subsys, message);
   g_free(message);
}

bool log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   /* Keep event-bus trace traffic off the GUI delivery queue. */
   if (priority == LOG_CRAZY || dying || !shared_log || !fmt) {
      return true;
   }
   /* The callback is registered with LOG_CALLBACK_FILTERED. */
   va_list sizing;
   va_copy(sizing, ap);
   int length = vsnprintf(NULL, 0, fmt, sizing);
   va_end(sizing);

   if (length < 0) {
      return true;
   }
   char *message = g_malloc((size_t)length + 1);
   vsnprintf(message, (size_t)length + 1, fmt, ap);
   gtk_log_submit(FALSE, "Client", priority, subsys, message);
   g_free(message);

   return false;
}
bool log_print(logpriority_t priority, const char *subsys, const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   bool result = log_print_va(priority, subsys, fmt, ap);
   va_end(ap);

   return result;
}
bool syslog_clear(void) {
   gtk_logview_clear(shared_log);

   return false;
}
GtkWidget *init_log_tab(void) {
   if (shared_log) {
      return gtk_logview_widget(shared_log);
   }
   gtk_log_owner = g_thread_self();
   shared_log = gtk_logview_new(3000);
   GtkWidget *page = gtk_logview_widget(shared_log);
   GtkWidget *label = gtk_label_new(NULL);
   gtk_label_set_markup(GTK_LABEL(label), "(<u>3</u>) Log");
   gtk_notebook_append_page(GTK_NOTEBOOK(main_notebook), page, label);
   event_on_binary("media.frame.log", host_log_frame_handler, NULL);

   return page;
}

/* Historical entry point: do not create an additional GTK tab. */
GtkWidget *init_host_log_tab(void) {
   return init_log_tab();
}
