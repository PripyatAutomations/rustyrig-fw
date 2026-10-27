/* Server-owned SDR directory snapshot client. Shared by GTK/TUI; JSON
 * requests are suitable for the browser WebUI as well. No GTK dependencies. */
#include <glib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/ui.h>
#include <rrclient/receiver.directory.h>
#include <rrclient/receiver.catalog.h>
#define CATALOG_MAX (8u*1024u*1024u)
#define CATALOG_CHUNK 12000u
typedef struct {
   rrconn_t *connection;
   GPtrArray *chunks;
   guint total, received, requested_page;
   char digest[65], token[65];
   guint retry;
   guint64 generation;
   GString *body;
} catalog_download_t;
static catalog_download_t transfer;
static rr_event_token_t catalog_token;
static guint timer_id;
static unsigned serial;
static void free_chunk(gpointer chunk) {
   if (chunk) {
      g_bytes_unref(chunk);
   }
}
static void reset(void) {
   if (transfer.chunks) {
      g_ptr_array_free(transfer.chunks, TRUE);
   }
   memset(&transfer, 0, sizeof(transfer));
}
static bool send_request(rrconn_t *connection, const char *command, guint page) {
   if (!connection || !connection->is_ws || !connection->authenticated) {
      return false;
   }
   dict *d=dict_new();
   if(!d) {
      return false;
   }
   dict_add(d,"msg.type","receiver-directory");
   dict_add(d,"directory.cmd",command);
   dict_add(d,"directory.request",transfer.token);
   if(!strcmp(command,"list")) {
      dict_add_uint(d,"directory.page",page);
   }
   bool sent=ws_send_dict(NULL,connection,d,WEBSOCKET_OP_TEXT);
   dict_free(d);
   return sent;
}
static gboolean expire(gpointer unused) {
   (void)unused; timer_id=0;
   if (!transfer.connection) {
      return G_SOURCE_REMOVE;
   }
   if (++transfer.retry <= 5) {
      /* Server may be fetching its initial cache asynchronously. */
      if (send_request(transfer.connection,"list",transfer.requested_page)) {
         timer_id=g_timeout_add_seconds(3,expire,NULL);
         return G_SOURCE_REMOVE;
      }
   }
   Log(LOG_WARN,"receiver.directory","Server catalog unavailable; keeping local cached receivers");
   reset();
   return G_SOURCE_REMOVE;
}
static void arm(void) {
   if(timer_id) {
      g_source_remove(timer_id);
   }
   timer_id=g_timeout_add_seconds(3,expire,NULL);
}
bool rrclient_receiver_catalog_request(bool force) {
   (void)force; /* Server refresh is governed by its shared TTL. */
   rrconn_t *connection=rrclient_active_connection();
   if(!connection || !connection->is_ws || !connection->authenticated) {
      return false;
   }
   if (timer_id) {
      g_source_remove(timer_id);
      timer_id=0;
   }
   reset();
   transfer.connection=connection;
   snprintf(transfer.token,sizeof(transfer.token),"client-%u",++serial);
   if (!send_request(connection,"list",0)) {
      reset();
      return false;
   }
   arm();
   return true;
}
static void received(const char *event,const char *data,rrconn_t *connection,void *unused) {
   (void)event;(void)unused;
   if(!data || !transfer.connection || transfer.connection!=connection) {
      return;
   }
   dict *d=json2dict(data);
   if(!d) {
      return;
   }
   const char *token=dict_get(d,"directory.request","");
   const char *command=dict_get(d,"directory.cmd","");
   if(strcmp(token,transfer.token)) {
      goto done;
   }
   if(!strcmp(command,"status")) {
      const char *digest=dict_get(d,"directory.digest","");
      if(strlen(digest)!=64)goto done; /* Empty cache; server still fetching. */
      if(transfer.total==0) {
         snprintf(transfer.digest,sizeof(transfer.digest),"%s",digest);
         transfer.generation=dict_get_ullong(d,"directory.generation",0);
      }
   } else if(!strcmp(command,"chunk")) {
      const char *hex=dict_get(d,"directory.digest","");
      const char *encoded=dict_get(d,"directory.data","");
      const char *encoding=dict_get(d,"directory.encoding","");
      guint count=dict_get_uint(d,"directory.total",0);
      guint seq=dict_get_uint(d,"directory.sequence",G_MAXUINT);
      guint64 generation=dict_get_ullong(d,"directory.generation",0);
      if(strcmp(encoding,"base64") || !count || count>1024 ||
         seq>=count || !encoded || strlen(encoded)>4*((CATALOG_CHUNK+2)/3)+4 ||
         (transfer.total && (transfer.total!=count || transfer.generation!=generation)) ||
         (transfer.digest[0] && strcmp(transfer.digest,hex))) {
         goto done;
      }
      if(strlen(hex)!=64 || (transfer.total && transfer.requested_page*8>seq)) {
         goto done;
      }
      if(!transfer.chunks) {
         transfer.total=count;
         transfer.generation=generation;
         snprintf(transfer.digest,sizeof(transfer.digest),"%s",hex);
         transfer.chunks=g_ptr_array_new_with_free_func(free_chunk);
         g_ptr_array_set_size(transfer.chunks,count);
      }
      if(g_ptr_array_index(transfer.chunks,seq)) {
         goto done;
      }
      gsize bytes=0;
      guchar *decoded=g_base64_decode(encoded,&bytes);
      if (!decoded || !bytes || bytes>CATALOG_CHUNK) {
         g_free(decoded);
         goto done;
      }
      if (transfer.received*CATALOG_CHUNK+bytes>CATALOG_MAX) {
         g_free(decoded);
         goto done;
      }
      g_ptr_array_index(transfer.chunks,seq)=g_bytes_new_take(decoded,bytes);
      /* GPtrArray free func is overridden below before completion. */
      transfer.received++;
      if (timer_id) {
         g_source_remove(timer_id);
         timer_id=0;
      }
      if(transfer.received==transfer.total) {
         GString *body=g_string_sized_new(MIN(CATALOG_MAX,(size_t)transfer.total*CATALOG_CHUNK));
         for(guint i=0;i<transfer.total;i++){
            GBytes *chunk=g_ptr_array_index(transfer.chunks,i);
            gsize len=0;const void *mem=chunk?g_bytes_get_data(chunk,&len):NULL;
            if (!mem || body->len+len>CATALOG_MAX) {
               g_string_free(body,TRUE);
               reset();
               goto done;
            }
            g_string_append_len(body,mem,len);
         }
         char *sum=g_compute_checksum_for_string(G_CHECKSUM_SHA256,body->str,body->len);
         if (sum && !strcmp(sum, transfer.digest) &&
             receiver_directory_import_snapshot(body->str, body->len, "rrserver")) {
            Log(LOG_INFO, "receiver.directory",
               "Applied %u verified chunks of server SDR catalog (generation %llu)",
               transfer.total, (unsigned long long)transfer.generation);
         } else {
            Log(LOG_WARN, "receiver.directory",
               "Rejected incomplete or invalid server directory digest");
         }
         g_free(sum);g_string_free(body,TRUE);reset();
      } else if(transfer.received%8==0) {
         transfer.requested_page=transfer.received/8;
         if(send_request(connection,"list",transfer.requested_page)) {
            arm();
         }
      } else arm();
   }
done:
   dict_free(d);
}
static void disconnected(const char *event,const char *data,rrconn_t *connection,void *unused) {
   (void)event;(void)data;(void)unused;
   if(transfer.connection==connection) {
      if (timer_id) {
         g_source_remove(timer_id);
         timer_id=0;
      }
      reset();
   }
}
void rrclient_receiver_catalog_init(void) {
   if(catalog_token) {
      return;
   }
   catalog_token=event_on_token("ws.msg.receiver-directory",received,NULL);
   event_on("disconnected",disconnected,NULL);
}
void rrclient_receiver_catalog_shutdown(void) {
   if (timer_id) {
      g_source_remove(timer_id);
      timer_id=0;
   }
   reset();
   if (catalog_token) {
      event_off_token(catalog_token);
      catalog_token=0;
   }
}
