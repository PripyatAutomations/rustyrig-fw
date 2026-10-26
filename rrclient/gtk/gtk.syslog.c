/* GTK client and host logs share one structured LogView.
 * TUI logging remains separate and unchanged. */
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

static void host_log_frame_handler(const char *event, const void *data,
   size_t len, rrconn_t *connection, void *user) {
   (void)event; (void)user;
   if (dying || !shared_log || !data || len < RR_LOGFRAME_HDR_LEN) return;
   const uint8_t *bytes = data;
   logpriority_t level = (logpriority_t)bytes[0];
   char subsys[17];
   memcpy(subsys, bytes + 1, 16);
   subsys[16] = '\0';
   if (debug_filter(subsys, level)) return;
   const char *payload = (const char *)bytes + RR_LOGFRAME_HDR_LEN;
   size_t max = len - RR_LOGFRAME_HDR_LEN;
   gchar *message = g_strndup(payload, strnlen(payload, max));
   const char *source = connection && connection->server && connection->server->network[0] ?
      connection->server->network : connection && connection->hostname[0] ?
      connection->hostname : "host";
   gtk_logview_append(shared_log, TRUE, source, level, subsys, message);
   g_free(message);
}

bool log_print_va(logpriority_t priority, const char *subsys, const char *fmt, va_list ap) {
   if (dying || !shared_log || !fmt) return true;
   /* The callback is registered with LOG_CALLBACK_FILTERED. */
   char message[8096];
   vsnprintf(message, sizeof(message), fmt, ap);
   gtk_logview_append(shared_log, FALSE, "Client", priority, subsys, message);
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
   if (shared_log) return gtk_logview_widget(shared_log);
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
