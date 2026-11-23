// SPDX-License-Identifier: GPL-2.0-only
/* Keyboard-mode modem pages reuse the normal Terminal notebook, not the
 * message-oriented decoder composer. All TX actions go to the client core. */
#include "gtk.modem-terminal.h"
#include <librustyaxe/core.h>
#include <string.h>
typedef struct {
   unsigned slot;
   GtkWidget *page,*view,*entry,*tx,*abort,*status,*timer;
   gboolean requested, ending, enabled;
   gint64 tx_started_us, tx_release_at_us, tx_timeout_at_us;
} keyboard_t;
static GtkWidget *pages;
static keyboard_t *slots[RR_LOCAL_MODEM_MAX];
static rr_event_token_t rx_token;
static guint tick_id;
static void request(unsigned slot,rr_local_modem_action_t action,const char *text) {
   rr_local_modem_request_t req={.slot=slot,.action=action};
   if(text)g_strlcpy(req.text,text,sizeof(req.text));
   rr_local_modem_request(&req);
}
static void update_timer(keyboard_t *k) {
   if(!k||!k->timer)return;
   if(k->tx_started_us<=0) {
      gtk_label_set_text(GTK_LABEL(k->timer),"RX 00:00");
      gtk_widget_set_tooltip_text(k->timer,"Receive; PTT not asserted");
      return;
   }
   gint64 now=g_get_monotonic_time();
   gint64 sec=MAX((gint64)0,(now-k->tx_started_us)/G_USEC_PER_SEC);
   gchar buf[40];
   g_snprintf(buf,sizeof(buf),"TX %02"G_GINT64_FORMAT":%02"G_GINT64_FORMAT,
              sec/60,sec%60);
   /* The worker announces TX_DONE only once its text queue and final
    * waveform have drained. The core then schedules a 750 ms PTT hold.
    * Highlight that actual countdown, or the final 15s of the finite
    * TX watchdog, not merely an empty GTK text field. */
   gboolean unkey=k->tx_release_at_us>0;
   gboolean watchdog=k->tx_timeout_at_us>0 &&
                      k->tx_timeout_at_us-now<=15*G_USEC_PER_SEC;
   if(unkey||watchdog) {
      gchar *markup=g_markup_printf_escaped(
         "<span foreground='#ff4848' weight='bold'>%s</span>",buf);
      gtk_label_set_markup(GTK_LABEL(k->timer),markup);
      g_free(markup);
      gint64 remaining=unkey?k->tx_release_at_us-now:k->tx_timeout_at_us-now;
      gchar *tip=g_strdup_printf(unkey ?
         "TX queue drained; PTT unkeys in %.1f s" :
         "PTT safety timeout in %.1f s",
         MAX(0.,remaining/(double)G_USEC_PER_SEC));
      gtk_widget_set_tooltip_text(k->timer,tip);
      g_free(tip);
   } else {
      gtk_label_set_text(GTK_LABEL(k->timer),buf);
      gtk_widget_set_tooltip_text(k->timer,"PTT keyed; elapsed transmission time");
   }
}
static gboolean tick(gpointer unused) {
   (void)unused;
   gboolean any=FALSE;
   for(unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++)
      if(slots[i]) {
         any=TRUE;
         update_timer(slots[i]);
      }
   static unsigned quarter;
   if(any && (++quarter%4)==0) {
      rr_local_modem_request_t req={.action=RR_LOCAL_MODEM_SNAPSHOT};
      rr_local_modem_request(&req);
   }
   return G_SOURCE_CONTINUE;
}
static void append(keyboard_t *k,const char *buf,unsigned len) {
   if(!k||!k->view||!len)return;
   GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(k->view));
   GtkTextIter end;
   gtk_text_buffer_get_end_iter(b,&end);
   gtk_text_buffer_insert(b,&end,buf,(gint)len);
   gtk_text_buffer_get_end_iter(b,&end);
   gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(k->view),&end,0,FALSE,0,0);
}
static void rx_event(const rr_event_message_t *msg,void *unused) {
   (void)unused;
   if(!msg||!msg->payload||msg->length!=sizeof(rr_local_modem_stream_rx_t))return;
   const rr_local_modem_stream_rx_t *rx=msg->payload;
   if(rx->slot<1||rx->slot>RR_LOCAL_MODEM_MAX||rx->length>sizeof(rx->bytes))return;
   append(slots[rx->slot-1],rx->bytes,rx->length);
}
static void destroyed(GtkWidget *widget,gpointer user) {
   (void)widget;
   keyboard_t *k=user;
   if(k->requested) request(k->slot,RR_LOCAL_MODEM_STREAM_ABORT,NULL);
   slots[k->slot-1]=NULL;
   g_free(k);
}
static void close_clicked(GtkButton *button,gpointer user) {
   (void)button;
   keyboard_t *k=user;
   gtk_widget_destroy(k->page);
}
static void tx_clicked(GtkButton *button,gpointer user) {
   (void)button;
   keyboard_t *k=user;
   if(!k->enabled||k->ending)return;
   if(!k->requested) {
      request(k->slot,RR_LOCAL_MODEM_STREAM_BEGIN,NULL);
      k->requested=TRUE;
      gtk_button_set_label(GTK_BUTTON(k->tx),"RX (drain)");
      gtk_label_set_text(GTK_LABEL(k->status),"Requesting PTT");
      gtk_widget_grab_focus(k->entry);
   } else {
      request(k->slot,RR_LOCAL_MODEM_STREAM_END,NULL);
      k->ending=TRUE;
      gtk_widget_set_sensitive(k->tx,FALSE);
      gtk_widget_set_sensitive(k->entry,FALSE);
      gtk_label_set_text(GTK_LABEL(k->status),"Draining TX queue");
   }
}
static void abort_clicked(GtkButton *button,gpointer user) {
   (void)button;
   keyboard_t *k=user;
   request(k->slot,RR_LOCAL_MODEM_STREAM_ABORT,NULL);
   k->requested=k->ending=FALSE;
   k->tx_started_us=k->tx_release_at_us=k->tx_timeout_at_us=0;
   gtk_widget_set_sensitive(k->entry,FALSE);
   gtk_widget_set_sensitive(k->tx,k->enabled);
   gtk_button_set_label(GTK_BUTTON(k->tx),"TX");
   gtk_label_set_text(GTK_LABEL(k->status),"TX abort requested");
   update_timer(k);
}
static void insert_text(GtkEditable *editable,gchar *text,gint length,
                        gint *position,gpointer user) {
   (void)position;
   keyboard_t *k=user;
   g_signal_stop_emission_by_name(editable,"insert-text");
   if(!k->requested||k->ending||length<=0)return;
   for(gint at=0;at<length;) {
      gint n=MIN(200,length-at);
      if(memchr(text+at,0,(size_t)n))break;
      char chunk[256];
      memcpy(chunk,text+at,(size_t)n);chunk[n]=0;
      request(k->slot,RR_LOCAL_MODEM_STREAM_DATA,chunk);
      append(k,chunk,(unsigned)n);
      at+=n;
   }
}
static gboolean key(GtkWidget *widget,GdkEventKey *event,gpointer user) {
   (void)widget;
   keyboard_t *k=user;
   if(event->type!=GDK_KEY_PRESS||!k->requested||k->ending)return FALSE;
   if(event->keyval==GDK_KEY_Return||event->keyval==GDK_KEY_KP_Enter) {
      request(k->slot,RR_LOCAL_MODEM_STREAM_DATA,"\n");
      append(k,"\n",1);
      return TRUE;
   }
   return FALSE;
}
static keyboard_t *create(unsigned slot) {
   keyboard_t *k=g_new0(keyboard_t,1);
   k->slot=slot;
   k->page=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
   GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
   k->tx=gtk_button_new_with_label("TX");
   k->abort=gtk_button_new_with_label("Abort");
   k->status=gtk_label_new("RX");
   k->timer=gtk_label_new("RX 00:00");
   gtk_label_set_xalign(GTK_LABEL(k->status),0);
   gtk_widget_set_hexpand(k->status,TRUE);
   gtk_label_set_xalign(GTK_LABEL(k->timer),1);
   gtk_style_context_add_class(gtk_widget_get_style_context(k->timer),"monospace");
   gtk_box_pack_start(GTK_BOX(bar),k->tx,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(bar),k->abort,FALSE,FALSE,0);
   gtk_box_pack_start(GTK_BOX(bar),k->status,TRUE,TRUE,0);
   gtk_box_pack_end(GTK_BOX(bar),k->timer,FALSE,FALSE,6);
   gtk_box_pack_start(GTK_BOX(k->page),bar,FALSE,FALSE,0);
   GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);
   k->view=gtk_text_view_new();
   gtk_text_view_set_editable(GTK_TEXT_VIEW(k->view),FALSE);
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(k->view),FALSE);
   gtk_text_view_set_monospace(GTK_TEXT_VIEW(k->view),TRUE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(k->view),GTK_WRAP_CHAR);
   gtk_container_add(GTK_CONTAINER(scroll),k->view);
   gtk_box_pack_start(GTK_BOX(k->page),scroll,TRUE,TRUE,0);
   k->entry=gtk_entry_new();
   gtk_entry_set_placeholder_text(GTK_ENTRY(k->entry),"Live keyboard text (TX to transmit)");
   gtk_box_pack_start(GTK_BOX(k->page),k->entry,FALSE,FALSE,0);
   gtk_widget_set_sensitive(k->entry,FALSE);
   g_signal_connect(k->entry,"insert-text",G_CALLBACK(insert_text),k);
   g_signal_connect(k->entry,"key-press-event",G_CALLBACK(key),k);
   g_signal_connect(k->tx,"clicked",G_CALLBACK(tx_clicked),k);
   g_signal_connect(k->abort,"clicked",G_CALLBACK(abort_clicked),k);
   g_signal_connect(k->page,"destroy",G_CALLBACK(destroyed),k);
   GtkWidget *tab=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
   gchar *name=g_strdup_printf("psk-%u",slot);
   gtk_box_pack_start(GTK_BOX(tab),gtk_label_new(name),FALSE,FALSE,0);
   g_free(name);
   GtkWidget *close=gtk_button_new_from_icon_name("window-close",GTK_ICON_SIZE_MENU);
   g_signal_connect(close,"clicked",G_CALLBACK(close_clicked),k);
   gtk_box_pack_start(GTK_BOX(tab),close,FALSE,FALSE,0);
   slots[slot-1]=k;
   gtk_notebook_append_page(GTK_NOTEBOOK(pages),k->page,tab);
   gtk_widget_show_all(tab);
   gtk_widget_show_all(k->page);
   return k;
}
void rr_gtk_modem_terminal_sync(const rr_local_modem_entry_t *entry) {
   if(!pages||!entry||entry->slot<1||entry->slot>RR_LOCAL_MODEM_MAX)return;
   keyboard_t *k=slots[entry->slot-1];
   /* PSK currently advertises the keyboard stream; other engines may
    * reuse this terminal when they implement the same contract. */
   if(!entry->running||strcmp(entry->engine,"psk")) {
      if(k)gtk_widget_destroy(k->page);
      return;
   }
   if(!k)k=create(entry->slot);
   k->enabled=entry->tx_enabled;
   k->tx_started_us=entry->tx_started_us;
   k->tx_release_at_us=entry->tx_release_at_us;
   k->tx_timeout_at_us=entry->tx_timeout_at_us;
   if(!entry->stream_active&&!entry->tx_on) {
      k->requested=k->ending=FALSE;
      gtk_button_set_label(GTK_BUTTON(k->tx),"TX");
   }
   gtk_widget_set_sensitive(k->tx,k->enabled&&!k->ending);
   gtk_widget_set_sensitive(k->entry,k->enabled&&k->requested&&!k->ending);
   gtk_widget_set_sensitive(k->abort,k->enabled&&
      (k->requested||entry->stream_active||entry->tx_on));
   if(entry->tx_on)
      gtk_label_set_text(GTK_LABEL(k->status),k->ending?"Draining TX":"Transmitting");
   else if(!k->requested)
      gtk_label_set_text(GTK_LABEL(k->status),k->enabled?"RX — ready":"RX — TX disabled");
   update_timer(k);
}
void rr_gtk_modem_terminal_init(GtkWidget *notebook) {
   pages=notebook;
   rx_token=event_on_message_domain_token(RR_LOCAL_MODEM_STREAM_RX,rx_event,NULL,"gtk");
   if(!tick_id)tick_id=g_timeout_add(250,tick,NULL);
}
void rr_gtk_modem_terminal_shutdown(void) {
   if(tick_id)g_source_remove(tick_id);
   tick_id=0;
   if(rx_token)event_off_token(rx_token);
   rx_token=0;
   for(unsigned i=0;i<RR_LOCAL_MODEM_MAX;i++)
      if(slots[i])gtk_widget_destroy(slots[i]->page);
   pages=NULL;
}
