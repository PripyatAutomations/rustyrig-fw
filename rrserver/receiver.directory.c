/* Server-owned external SDR directory snapshot cache and on-demand protocol.
 * Never adds unconnected receivers to Resource Explorer and never probes them.
 * A connected client receives bounded chunks and verifies the digest itself.
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <rrserver/receiver.directory.h>

#define DIR_MAX_BODY (8u * 1024u * 1024u)
#define DIR_CHUNK 12000u
#define DIR_TTL (4 * 3600)
static sqlite3 *db;
static char *snapshot, *source_url, *validator, *modified;
static char digest[65];
static time_t next_fetch, fetched_at;
static guint64 generation;
static struct {
   GThread *thread;
   gint done;
   char *url, *etag, *modified, *data, *new_etag, *new_modified;
   size_t length, capacity;
   long http_status;
   CURLcode result;
   bool overflow;
} request;
static char *db_path;

static bool source_allowed(const char *url) {
   if (!url || !g_str_has_prefix(url, "https://")) return false;
   GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, NULL);
   if (!uri) return false;
   const char *host = g_uri_get_host(uri);
   bool ok = host && *host && !g_str_has_suffix(host, "kiwisdr.com") &&
      !g_str_has_suffix(host, ".kiwisdr.com") && !g_uri_get_userinfo(uri) &&
      !g_uri_get_fragment(uri);
   g_uri_unref(uri);
   return ok;
}
static void make_digest(void) {
   if (!snapshot) { digest[0] = 0; return; }
   char *hex = g_compute_checksum_for_string(G_CHECKSUM_SHA256, snapshot, -1);
   g_strlcpy(digest, hex ? hex : "", sizeof(digest));
   g_free(hex);
}
static void save_snapshot(void) {
   if (!db) return;
   sqlite3_stmt *stmt = NULL;
   if (sqlite3_prepare_v2(db,
      "INSERT OR REPLACE INTO snapshots(id,source,payload,digest,generation,fetched,next_fetch,etag,modified) VALUES(1,?,?,?,?,?,?,?,?)",
      -1, &stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_text(stmt,1,source_url ? source_url : "",-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,2,snapshot ? snapshot : "",-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,3,digest,-1,SQLITE_TRANSIENT);
      sqlite3_bind_int64(stmt,4,(sqlite3_int64)generation);
      sqlite3_bind_int64(stmt,5,fetched_at);
      sqlite3_bind_int64(stmt,6,next_fetch);
      sqlite3_bind_text(stmt,7,validator ? validator : "",-1,SQLITE_TRANSIENT);
      sqlite3_bind_text(stmt,8,modified ? modified : "",-1,SQLITE_TRANSIENT);
      if (sqlite3_step(stmt) != SQLITE_DONE)
         Log(LOG_WARN,"receiver.directory","Unable to save server directory: %s",sqlite3_errmsg(db));
   }
   sqlite3_finalize(stmt);
}
static bool body_valid(const char *data, size_t len) {
   if (!data || len < 20 || len > DIR_MAX_BODY || memchr(data, 0, len) ||
       !g_utf8_validate(data, len, NULL)) return false;
   struct mg_str json = mg_str_n(data, len);
   int size = 0, start = mg_json_get(json, "$", &size);
   if (start < 0 || size < 2 || data[start] != '{') return false;
   for (size_t i = (size_t)start + (size_t)size; i < len; i++)
      if (!g_ascii_isspace(data[i])) return false;
   double schema = 0;
   if (!mg_json_get_num(json, "$.schema", &schema) || schema != 1) return false;
   char *provider = mg_json_get_str(json, "$.provider");
   bool supported = provider ? !strcmp(provider, "kiwi") :
      !mg_json_get_tok(json, "$.provider").len;
   free(provider);
   if (!supported) return false;
   struct mg_str array = mg_json_get_tok(json, "$.receivers"), object;
   if (!array.len || array.buf[0] != '[') return false;
   size_t offset = 0, count = 0;
   while ((offset = mg_json_next(array, offset, NULL, &object))) {
      if (++count > 10000 || !object.len || object.buf[0] != '{')
         return false;
   }
   return true;
}
static size_t receive_body(char *ptr,size_t sz,size_t count,void *unused) {
   (void)unused;
   if (sz && count > G_MAXSIZE/sz) return 0;
   size_t bytes=sz*count;
   if (bytes > DIR_MAX_BODY - request.length) {request.overflow=true;return 0;}
   char *data=g_realloc(request.data,request.length+bytes+1);
   request.data=data;
   memcpy(data+request.length,ptr,bytes);
   request.length+=bytes;data[request.length]=0;
   return bytes;
}
static gpointer downloader(gpointer unused) {
   (void)unused;
   CURL *curl=curl_easy_init();
   if (curl) {
      struct curl_slist *headers=NULL;
      if (request.etag && *request.etag) {
         char *h=g_strdup_printf("If-None-Match: %s",request.etag);
         headers=curl_slist_append(headers,h);g_free(h);
      } else if(request.modified && *request.modified) {
         char *h=g_strdup_printf("If-Modified-Since: %s",request.modified);
         headers=curl_slist_append(headers,h);g_free(h);
      }
      curl_easy_setopt(curl,CURLOPT_URL,request.url);
      curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
      curl_easy_setopt(curl,CURLOPT_PROXY,"");
      curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,8L);
      curl_easy_setopt(curl,CURLOPT_TIMEOUT,25L);
      curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
      curl_easy_setopt(curl,CURLOPT_ACCEPT_ENCODING,"");
      curl_easy_setopt(curl,CURLOPT_USERAGENT,"RustyRig ReceiverDirectory");
      curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive_body);
#if LIBCURL_VERSION_NUM >= 0x075500
      curl_easy_setopt(curl,CURLOPT_PROTOCOLS_STR,"https");
#else
      curl_easy_setopt(curl,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
#endif
      curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
      request.result=curl_easy_perform(curl);
      curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&request.http_status);
      curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
   } else request.result=CURLE_FAILED_INIT;
   g_atomic_int_set(&request.done,1);
   return NULL;
}
static void complete_download(void) {
   if (!request.thread || !g_atomic_int_get(&request.done)) return;
   g_thread_join(request.thread);
   request.thread=NULL;
   time_t current=time(NULL);
   if (request.result==CURLE_OK && request.http_status==200 &&
       body_valid(request.data,request.length)) {
      g_free(snapshot);
      snapshot=g_steal_pointer(&request.data);
      g_free(source_url);source_url=g_strdup(request.url);
      fetched_at=current;
      generation++;
      make_digest();
      Log(LOG_INFO,"receiver.directory","Server directory generation %llu: %zu bytes cached",
         (unsigned long long)generation,strlen(snapshot));
   } else if (request.result!=CURLE_OK || request.http_status!=304)
      Log(LOG_WARN,"receiver.directory","Server directory update failed: curl=%d HTTP=%ld (retaining snapshot)",
         (int)request.result,request.http_status);
   next_fetch=current+DIR_TTL;
   save_snapshot();
   g_clear_pointer(&request.url,g_free);
   g_clear_pointer(&request.etag,g_free);
   g_clear_pointer(&request.modified,g_free);
   g_clear_pointer(&request.data,g_free);
   request.length=0;
}
static void start_fetch(void) {
   if (request.thread || !source_allowed(cfg_get("receiver-directory.url"))) return;
   if (!snapshot && next_fetch>time(NULL)) return;
   const char *url=cfg_get("receiver-directory.url");
   if (snapshot && source_url && strcmp(source_url,url)) {
      /* Keep the old snapshot as fallback, but never serve it as the new
       * source's current generation. */
   }
   request.url=g_strdup(url);
   request.etag=source_url && !strcmp(source_url,url) ? g_strdup(validator) : NULL;
   request.modified=source_url && !strcmp(source_url,url) ? g_strdup(modified) : NULL;
   request.result=CURLE_FAILED_INIT;
   request.http_status=0;request.overflow=false;
   g_atomic_int_set(&request.done,0);
   request.thread=g_thread_try_new("rr-directory-fetch",downloader,NULL,NULL);
   if (!request.thread) {g_clear_pointer(&request.url,g_free);}
}
static void respond(rrconn_t *client,const char *cmd,const char *nonce) {
   dict *d=dict_new();
   if (!d) return;
   dict_add(d,"msg.type","receiver-directory");
   dict_add(d,"directory.cmd",cmd);
   if (nonce && *nonce) dict_add(d,"directory.request",nonce);
   dict_add(d,"directory.digest",digest);
   dict_add(d,"directory.source",source_url ? source_url : "");
   dict_add_ullong(d,"directory.generation",generation);
   dict_add_llong(d,"directory.fetched",fetched_at);
   dict_add_llong(d,"directory.next-refresh",next_fetch);
   dict_add_bool(d,"directory.fetching",request.thread!=NULL);
   ws_send_dict(NULL,client,d,WEBSOCKET_OP_TEXT);
   dict_free(d);
}
static void request_handler(const char *event,const void *data,size_t length,rrconn_t *client,void *unused) {
   (void)event;(void)unused;
   if (!client || !client->authenticated || length!=sizeof(rr_server_io_t)) return;
   rr_server_io_t *io=(rr_server_io_t *)data;
   if (strcmp(dict_get(io->message,"msg.type",""),"receiver-directory")) return;
   io->handled=true;io->result=true;
   const char *cmd=dict_get(io->message,"directory.cmd","");
   const char *nonce=dict_get(io->message,"directory.request","");
   if (strlen(nonce)>64) return;
   complete_download();
   if (!strcmp(cmd,"status") || !strcmp(cmd,"list")) {
      if (!request.thread && time(NULL)>=next_fetch) start_fetch();
      respond(client,"status",nonce);
      if (strcmp(cmd,"list") || !snapshot) return;
      size_t len=strlen(snapshot),chunks=(len+DIR_CHUNK-1)/DIR_CHUNK;
      if (chunks>1024) return;
      /* Page the catalog: at most eight <=12KiB chunks per command. The
       * receiver sends the next page only when explicitly requested, keeping
       * large inventories out of the control queue. */
      unsigned page=dict_get_uint(io->message,"directory.page",0);
      if (page > 127 || (size_t)page*8>=chunks) return;
      size_t start=(size_t)page*8;
      size_t stop=MIN(start+8,chunks);
      for(size_t i=start;i<stop;i++) {
         size_t offset=i*DIR_CHUNK;
         size_t length=MIN(DIR_CHUNK,len-offset);
         char *encoded=g_base64_encode((const guchar*)snapshot+offset,length);
         dict *row=dict_new();
         if (!row) {g_free(encoded);break;}
         dict_add(row,"msg.type","receiver-directory");
         dict_add(row,"directory.cmd","chunk");
         dict_add(row,"directory.request",nonce);
         dict_add(row,"directory.digest",digest);
         dict_add_ullong(row,"directory.generation",generation);
         dict_add_uint(row,"directory.sequence",(unsigned)i);
         dict_add_uint(row,"directory.total",(unsigned)chunks);
         dict_add_uint(row,"directory.page",page);
         dict_add(row,"directory.encoding","base64");
         dict_add(row,"directory.data",encoded);
         ws_send_dict(NULL,client,row,WEBSOCKET_OP_TEXT);
         dict_free(row);g_free(encoded);
      }
   } else if (!strcmp(cmd,"refresh")) {
      /* A refresh is advisory: bypassing the cache requires a separate
       * privileged operation, not a normal user clicking the button. */
      if (!request.thread && time(NULL)>=next_fetch) start_fetch();
      respond(client,"status",nonce);
   }
}
static void poll_dir(const char *event,const char *data,rrconn_t *client,void *unused) {
   (void)event;(void)data;(void)client;(void)unused;
   complete_download();
}
bool rrserver_receiver_directory_init(void) {
   const char *configured=cfg_get("receiver-directory.cache");
   db_path=configured && *configured ? g_strdup(configured) :
      g_build_filename(g_get_user_cache_dir(),"rustyrig","server-receivers.sqlite3",NULL);
   char *parent=g_path_get_dirname(db_path);
   bool ok=!g_mkdir_with_parents(parent,0700) && sqlite3_open(db_path,&db)==SQLITE_OK;
   g_free(parent);
   if (!ok) {Log(LOG_WARN,"receiver.directory","Cannot open %s",db_path);return false;}
   g_chmod(db_path,0600);
   if (sqlite3_exec(db,"CREATE TABLE IF NOT EXISTS snapshots(id INTEGER PRIMARY KEY CHECK(id=1),source TEXT,payload TEXT,digest TEXT,generation INTEGER,fetched INTEGER,next_fetch INTEGER,etag TEXT,modified TEXT)",NULL,NULL,NULL)!=SQLITE_OK) return false;
   sqlite3_stmt *stmt=NULL;
   if (sqlite3_prepare_v2(db,"SELECT source,payload,digest,generation,fetched,next_fetch,etag,modified FROM snapshots WHERE id=1",-1,&stmt,NULL)==SQLITE_OK &&
       sqlite3_step(stmt)==SQLITE_ROW) {
      source_url=g_strdup((const char*)sqlite3_column_text(stmt,0));
      snapshot=g_strdup((const char*)sqlite3_column_text(stmt,1));
      generation=sqlite3_column_int64(stmt,3);
      fetched_at=sqlite3_column_int64(stmt,4);
      next_fetch=sqlite3_column_int64(stmt,5);
      validator=g_strdup((const char*)sqlite3_column_text(stmt,6));
      modified=g_strdup((const char*)sqlite3_column_text(stmt,7));
      make_digest();
      Log(LOG_INFO,"receiver.directory","Loaded server directory generation %llu from SQLite",
         (unsigned long long)generation);
   }
   sqlite3_finalize(stmt);
   event_on_binary(RR_SERVER_ROUTE_EVENT,request_handler,NULL);
   event_on("server.poll",poll_dir,NULL);
   return true;
}
void rrserver_receiver_directory_shutdown(void) {
   if(request.thread) {g_thread_join(request.thread);request.thread=NULL;}
   if(db)sqlite3_close(db);db=NULL;
   g_free(snapshot);snapshot=NULL;g_free(source_url);source_url=NULL;
   g_free(validator);validator=NULL;g_free(modified);modified=NULL;
   g_free(db_path);db_path=NULL;
}
