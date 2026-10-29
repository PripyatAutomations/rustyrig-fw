/* OpenSSL/wslay mesh driver. All callbacks run on the GLib main context.
 * No protocol policy or platform types escape into rr_transport. */
#include "mesh.wss.internal.h"
#include <librustyaxe/core.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

static SSL_CTX *tls;
static rr_mesh_pin_fn trusted;
static rr_mesh_http_fn http_handler;
static struct mesh_wss *connections;
static size_t connection_count;
static guint timer;
static bool stopping;
static bool ready(int fd, GIOCondition events, void *arg);
static bool connect_next(struct mesh_wss *c);

static void reject(struct mesh_wss *c, const char *reason) {
   if (!c->dead) g_strlcpy(c->reason, reason, sizeof(c->reason));
   c->dead = true;
   if (c->resolving && c->resolve_cancel) g_cancellable_cancel(c->resolve_cancel);
}

/* Explicit enrollment is the trust anchor, including for self-signed leaf
 * certificates. OpenSSL still verifies TLS CertificateVerify/key possession.
 * We validate the presented leaf, its validity and strength, and a durable
 * SPKI pin; no permissive CA callback or SSL_VERIFY_NONE path exists. */
static int verify_certificate(X509_STORE_CTX *store, void *unused) {
   (void)unused;
   SSL *ssl = X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());
   struct mesh_wss *c = SSL_get_app_data(ssl);
   X509 *cert = X509_STORE_CTX_get0_cert(store);
   if (!cert && c && !c->client) return 1; /* /ws and enrollment need no client certificate. */
   EVP_PKEY *key = cert ? X509_get_pubkey(cert) : NULL;
   unsigned char *der = NULL;
   int length = key ? i2d_PUBKEY(key, &der) : -1;
   unsigned n = 0;
   bool ok = c && cert && key && EVP_PKEY_security_bits(key) >= 112 && length > 0 &&
      X509_cmp_current_time(X509_get0_notBefore(cert)) < 0 &&
      X509_cmp_current_time(X509_get0_notAfter(cert)) > 0 &&
      EVP_Digest(der, length, c->pin, &n, EVP_sha256(), NULL) && n == 32 &&
      (!c->client || CRYPTO_memcmp(c->pin, c->expected, 32) == 0) &&
      trusted && trusted(c->pin);
   OPENSSL_free(der);
   EVP_PKEY_free(key);
   if (!ok) {
      X509_STORE_CTX_set_error(store, X509_V_ERR_CERT_REJECTED);
      if (c) reject(c, "TLS peer certificate rejected: SPKI not enrolled/expected, expired certificate, or weak key");
   }
   if (ok) c->peer_verified = true;
   return ok;
}

/* SSL error queues are cleared before every operation. WANT states retain
 * stable write arguments and drive fd interests, including cross-direction
 * WANT_READ from write and WANT_WRITE from read. */
static int ssl_result(struct mesh_wss *c, int result, int *want) {
   *want = 0;
   if (result > 0) return result;
   int error = SSL_get_error(c->ssl, result);
   if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
      *want = error;
      return 0;
   }
   unsigned long code = ERR_peek_last_error();
   char reason[256];
   snprintf(reason, sizeof(reason), "TLS %s: %s", error == SSL_ERROR_ZERO_RETURN ? "peer closed" : "I/O failed",
      code ? ERR_reason_error_string(code) : (error == SSL_ERROR_SYSCALL ? g_strerror(errno) : "connection ended"));
   reject(c, reason);
   return -1;
}

static ssize_t ws_receive(wslay_event_context_ptr ws, uint8_t *buf, size_t len, int flags, void *arg) {
   (void)flags;
   struct mesh_wss *c = arg;
   if (c->dead || !c->read_budget) goto blocked;
   if (c->http_len) {
      size_t n = MIN(len, c->http_len);
      memcpy(buf, c->http, n);
      memmove(c->http, c->http + n, c->http_len - n);
      c->http_len -= n;
      return n;
   }
   len = MIN(len, c->read_budget);
   ERR_clear_error();
   int n = SSL_read(c->ssl, buf, (int)len);
   n = ssl_result(c, n, &c->read_want);
   if (n > 0) { c->read_budget -= n; return n; }
   if (n < 0) {
      wslay_event_set_error(ws, WSLAY_ERR_CALLBACK_FAILURE);
      return -1;
   }
blocked:
   wslay_event_set_error(ws, WSLAY_ERR_WOULDBLOCK);
   return -1;
}

static ssize_t ws_send(wslay_event_context_ptr ws, const uint8_t *data, size_t len, int flags, void *arg) {
   (void)flags;
   struct mesh_wss *c = arg;
   size_t n = MIN(len, sizeof(c->tx) - c->tx_len);
   if (!n || c->dead) { wslay_event_set_error(ws, WSLAY_ERR_WOULDBLOCK); return -1; }
   memcpy(c->tx + c->tx_len, data, n);
   c->tx_len += n;
   return n;
}

static int mask(wslay_event_context_ptr ws, uint8_t *buf, size_t len, void *arg) {
   (void)ws; (void)arg;
   return RAND_bytes(buf, (int)len) == 1 ? 0 : -1;
}

static void ws_message(wslay_event_context_ptr ws, const struct wslay_event_on_msg_recv_arg *msg, void *arg) {
   (void)ws;
   struct mesh_wss *c = arg;
   if (c->dead) return;
   if (wslay_event_get_queued_msg_count(ws) > 256 || rr_mesh_wss_queued(c->transport) > MESH_QUEUE) {
      reject(c, "WebSocket control queue exceeded");
      return;
   }
   if (msg->opcode == WSLAY_CONNECTION_CLOSE) reject(c, "WebSocket peer closed");
   else if (msg->opcode == WSLAY_PONG)
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_PONG, NULL, NULL, 0);
   else if (msg->opcode == WSLAY_TEXT_FRAME)
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_MESSAGE, NULL, msg->msg, msg->msg_length);
   else if (msg->opcode == WSLAY_BINARY_FRAME && c->client_route)
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_BINARY_MESSAGE, NULL, msg->msg, msg->msg_length);
   else if (msg->opcode != WSLAY_PING) reject(c, "Mesh requires text messages");
}

static int queue_message(void *arg, const uint8_t *data, size_t len, uint8_t opcode) {
   struct mesh_wss *c = arg;
   if (c->dead || !c->open) return RR_TRANSPORT_CLOSED_ERROR;
   size_t queued = rr_mesh_wss_queued(c->transport);
   if (len > (c->client_route ? CLIENT_FRAME : MESH_FRAME) || queued > MESH_QUEUE || len + 16 > MESH_QUEUE - queued ||
       wslay_event_get_queued_msg_count(c->ws) >= 256) return RR_TRANSPORT_BACKPRESSURE;
   struct wslay_event_msg msg = { opcode, data, len };
   if (wslay_event_queue_msg(c->ws, &msg)) return RR_TRANSPORT_IO_ERROR;
   rr_mesh_wss_arm(c);
   return RR_TRANSPORT_OK;
}
static int send_message(void *arg, const uint8_t *data, size_t len) {
   return queue_message(arg, data, len, WSLAY_TEXT_FRAME);
}
static int send_binary(void *arg, const uint8_t *data, size_t len) {
   struct mesh_wss *c = arg;
   return c->client_route ? queue_message(arg, data, len, WSLAY_BINARY_FRAME) : RR_TRANSPORT_UNSUPPORTED;
}

static bool peer_pin(void *arg, uint8_t out[32]) {
   struct mesh_wss *c = arg;
   if (!c->tls_done || !c->peer_verified || c->dead) return false;
   memcpy(out, c->pin, 32);
   return true;
}
static void driver_close(void *arg) {
   struct mesh_wss *c = arg;
   if (c->dead) return;
   if (!c->open || !c->ws || (!c->tx_len && !wslay_event_want_write(c->ws))) {
      reject(c, "Local transport close");
      return;
   }
   c->draining = true;
   c->close_started = mono_ms();
   rr_mesh_wss_arm(c);
}
static size_t queued_bytes(void *arg) {
   struct mesh_wss *c = arg;
   return c->tx_len + (c->ws ? wslay_event_get_queued_msg_length(c->ws) : 0);
}
static uint32_t connection_capabilities(void *arg) {
   struct mesh_wss *c = arg;
   uint32_t basic = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_TLS_ENCRYPTED;
   if (c->client_route) {
      return basic | RR_TRANSPORT_BINARY_MESSAGES;
   }
   return basic | RR_TRANSPORT_TLS_AUTHENTICATED | RR_TRANSPORT_PEER_SPKI;
}
static const rr_transport_driver_t driver = {
   .abi_version = RR_TRANSPORT_ABI_VERSION,
   .capabilities = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_BINARY_MESSAGES | RR_TRANSPORT_TLS_ENCRYPTED | RR_TRANSPORT_TLS_AUTHENTICATED | RR_TRANSPORT_PEER_SPKI,
   .connection_capabilities = connection_capabilities,
   .send_message = send_message, .send_binary_message = send_binary,
   .queued_bytes = queued_bytes, .peer_spki_sha256 = peer_pin, .close = driver_close
};

static void flush(struct mesh_wss *c) {
   if (!c->tx_len || c->dead) return;
   if (!c->retry_len) c->retry_len = MIN(c->tx_len, 16384);
   ERR_clear_error();
   int n = SSL_write(c->ssl, c->tx, (int)c->retry_len);
   n = ssl_result(c, n, &c->write_want);
   if (n > 0) {
      memmove(c->tx, c->tx + n, c->tx_len - n);
      c->tx_len -= n;
      c->retry_len = 0;
   }
}

void rr_mesh_wss_arm(struct mesh_wss *c) {
   if (c->watch) { rr_reactor_watch_cancel(c->watch); c->watch = NULL; }
   if (c->dead || c->fd < 0) return;
   GIOCondition events = G_IO_IN;
   if (c->connecting || c->tls_want == SSL_ERROR_WANT_WRITE || c->read_want == SSL_ERROR_WANT_WRITE ||
       (c->tls_done && c->write_want != SSL_ERROR_WANT_READ &&
        (c->tx_len || (c->ws && wslay_event_want_write(c->ws))))) events |= G_IO_OUT;
   c->watch = rr_reactor_watch_fd(NULL, c->fd, events, ready, c);
   if (!c->watch) reject(c, "Cannot watch mesh socket");
}

static bool ready(int fd, GIOCondition events, void *arg) {
   struct mesh_wss *c = arg;
   if (c->connecting) {
      int error = 0;
      socklen_t size = sizeof(error);
      if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
         if (!connect_next(c)) {
            char reason[256];
            snprintf(reason, sizeof(reason), "TCP connect to %s (%s:%u) failed: %s",
               c->authority, c->ip[0] ? c->ip : "unresolved", c->port,
               error ? g_strerror(error) : "socket error");
            reject(c, reason);
         }
         events = 0; /* A failed address must not close the replacement fd. */
         goto done;
      }
      c->connecting = false;
   }
   if (events & (G_IO_ERR | G_IO_NVAL)) { reject(c, "Socket error; check remote listener and routing"); goto done; }
   if (!c->tls_done) {
      ERR_clear_error();
      int n = ssl_result(c, SSL_do_handshake(c->ssl), &c->tls_want);
      if (n <= 0) goto done;
      X509 *peer = SSL_get1_peer_certificate(c->ssl);
      bool valid = (!c->client || (peer && c->peer_verified)) &&
         SSL_get_verify_result(c->ssl) == X509_V_OK &&
         (SSL_version(c->ssl) >= TLS1_3_VERSION || SSL_get_extms_support(c->ssl));
      X509_free(peer);
      const char label[] = "EXPORTER-RustyRig-mesh-v1";
      if (!valid || !SSL_export_keying_material(c->ssl, c->binding, 32, label, sizeof(label)-1, NULL, 0, 0)) {
         reject(c, "TLS certificate or channel binding verification failed"); goto done;
      }
      c->tls_done = true;
      if (!rr_mesh_upgrade_prepare(c)) { reject(c, "Cannot prepare WSS upgrade"); goto done; }
   }
   flush(c);
   if (c->dead) goto done;
   if (c->http_reply) {
      if (!c->tx_len) reject(c, "HTTP response complete");
      goto done;
   }
   if (!c->open) {
      /* Do not overwrite an unfinished HTTP request with a reply. */
      if (c->client && c->tx_len) goto done;
      ERR_clear_error();
      int n = SSL_read(c->ssl, c->http + c->http_len, sizeof(c->http) - 1 - c->http_len);
      n = ssl_result(c, n, &c->read_want);
      if (n <= 0) goto done;
      if (memchr(c->http + c->http_len, 0, n)) { reject(c, "NUL in HTTP upgrade"); goto done; }
      c->http_len += n;
      c->http[c->http_len] = 0;
      int http_status = c->client ? 1 : rr_native_http_request(c, http_handler);
      if (http_status < 0) { reject(c, "Invalid native HTTP request"); goto done; }
      if (http_status == 2) { flush(c); goto done; }
      if (!http_status) goto done;
      int status = rr_mesh_upgrade_parse(c);
      if (status < 0) { reject(c, "Invalid mesh WSS upgrade or subprotocol"); goto done; }
      if (!status) goto done;
      struct wslay_event_callbacks cb = {
         .recv_callback = ws_receive, .send_callback = ws_send,
         .genmask_callback = mask, .on_msg_recv_callback = ws_message
      };
      int result = c->client ? wslay_event_context_client_init(&c->ws, &cb, c) :
         wslay_event_context_server_init(&c->ws, &cb, c);
      if (result) { reject(c, "Cannot allocate WebSocket framing"); goto done; }
      wslay_event_config_set_max_recv_msg_length(c->ws, c->client_route ? CLIENT_FRAME : MESH_FRAME);
      c->open = true;
      rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
   }
   if (!c->dead) {
      c->read_budget = 65536;
      if (!c->draining && wslay_event_recv(c->ws)) reject(c, "WebSocket receive failed");
      if (!c->dead && wslay_event_send(c->ws)) reject(c, "WebSocket send failed");
      if (!c->draining && !wslay_event_get_read_enabled(c->ws)) reject(c, "WebSocket framing rejected or closed");
      flush(c);
      if (c->draining && !c->dead && !c->tx_len && !wslay_event_want_write(c->ws))
         reject(c, "Local transport close");
   }
done:
   if ((events & G_IO_HUP) && !c->dead) reject(c, "Peer disconnected");
   rr_mesh_wss_arm(c);
   return true; /* arm cancelled this source; a new watch owns the next turn. */
}

static void release(struct mesh_wss *c) {
   if (c->listed) --connection_count;
   if (c->watch) rr_reactor_watch_cancel(c->watch);
   if (c->fd >= 0) close(c->fd);
   if (c->ws) wslay_event_context_free(c->ws);
   SSL_free(c->ssl);
   g_clear_object(&c->resolve_cancel);
   g_resolver_free_addresses(c->addresses);
   rr_transport_free(c->transport);
   free(c);
}

static gboolean maintenance(void *unused) {
   (void)unused;
   struct mesh_wss **p = &connections;
   while (*p) {
      struct mesh_wss *c = *p;
      if (!c->open && mono_ms() - c->started > cfg_get_int("links.handshake-seconds", 10) * 1000LL)
         reject(c, "TCP/TLS/WSS handshake timed out");
      if (c->draining && mono_ms() - c->close_started > 2000)
         reject(c, "Client close drain timed out");
      if (c->dead && !c->resolving) {
         *p = c->next;
         if (*c->reason && !c->http_reply) rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_ERROR, NULL, c->reason, strlen(c->reason));
         rr_transport_emit(c->transport, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
         release(c);
      } else {
         /* TLS may retain complete records after a bounded read turn. */
         if (!c->dead && c->ssl && c->tls_done && SSL_pending(c->ssl)) ready(c->fd, G_IO_IN, c);
         p = &c->next;
      }
   }
   if (stopping && !connections) { timer = 0; return G_SOURCE_REMOVE; }
   return G_SOURCE_CONTINUE;
}

bool rr_mesh_wss_init(const char *cert, const char *key, rr_mesh_pin_fn trust) {
   if (tls) return true;
   /* OpenSSL socket BIO writes must report EPIPE instead of terminating the
    * process when a peer resets a connection during a queued write. */
   signal(SIGPIPE, SIG_IGN);
   tls = SSL_CTX_new(TLS_method());
   if (!tls) return false;
   SSL_CTX_set_min_proto_version(tls, TLS1_2_VERSION);
   SSL_CTX_set_options(tls, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_TICKET);
   SSL_CTX_set_session_cache_mode(tls, SSL_SESS_CACHE_OFF);
   SSL_CTX_set_num_tickets(tls, 0);
   SSL_CTX_set_verify(tls, SSL_VERIFY_PEER, NULL);
   SSL_CTX_set_cert_verify_callback(tls, verify_certificate, NULL);
   if (!cert || !key || !trust || SSL_CTX_use_certificate_chain_file(tls, cert) != 1 ||
       SSL_CTX_use_PrivateKey_file(tls, key, SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(tls) != 1) {
      SSL_CTX_free(tls); tls = NULL; return false;
   }
   trusted = trust;
   stopping = false;
   timer = g_timeout_add(20, maintenance, NULL);
   return true;
}
void rr_mesh_wss_http_handler(rr_mesh_http_fn handler) { http_handler = handler; }

static struct mesh_wss *allocate(rr_transport_callback_t cb, void *arg, bool client) {
   if (!tls || stopping || connection_count >= 96) return NULL;
   struct mesh_wss *c = calloc(1, sizeof(*c));
   if (!c) return NULL;
   c->fd = -1;
   c->client = client;
   c->started = mono_ms();
   c->ssl = SSL_new(tls);
   if (!c->ssl) { free(c); return NULL; }
   c->transport = rr_transport_new(&driver, c, cb, arg);
   if (!c->transport || !c->ssl) { release(c); return NULL; }
   SSL_set_app_data(c->ssl, c);
   if (client) SSL_set_connect_state(c->ssl); else SSL_set_accept_state(c->ssl);
   c->next = connections;
   connections = c;
   c->listed = true;
   ++connection_count;
   return c;
}

static bool attach_socket(struct mesh_wss *c, int fd) {
   c->fd = fd;
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || SSL_set_fd(c->ssl, fd) != 1) {
      reject(c, "Cannot initialize nonblocking TLS socket"); return false;
   }
   rr_mesh_wss_arm(c);
   return true;
}

rr_transport_t *rr_mesh_wss_accept(int fd, rr_transport_callback_t cb, void *arg) {
   struct mesh_wss *c = allocate(cb, arg, false);
   if (!c) { close(fd); return NULL; }
   struct sockaddr_storage addr;
   socklen_t len = sizeof(addr);
   if (!getpeername(fd, (struct sockaddr *)&addr, &len)) {
      if (addr.ss_family == AF_INET) {
         inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr, c->ip, sizeof(c->ip));
         c->peer_port = ntohs(((struct sockaddr_in *)&addr)->sin_port);
      } else if (addr.ss_family == AF_INET6) {
         inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr, c->ip, sizeof(c->ip));
         c->peer_port = ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
      }
   }
   attach_socket(c, fd);
   return c->transport;
}

static bool connect_next(struct mesh_wss *c) {
   if (c->watch) { rr_reactor_watch_cancel(c->watch); c->watch = NULL; }
   if (c->fd >= 0) { close(c->fd); c->fd = -1; }
   while (c->next_address && !c->dead) {
      GList *p = c->next_address;
      c->next_address = p->next;
      GInetAddress *ip = p->data;
      GSocketAddress *address = g_inet_socket_address_new(ip, c->port);
      struct sockaddr_storage addr;
      int family = g_inet_address_get_family(ip);
      int fd = socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      bool ok = fd >= 0 && g_socket_address_to_native(address, &addr, sizeof(addr), NULL);
      if (ok) {
         int rc = connect(fd, (struct sockaddr *)&addr, g_socket_address_get_native_size(address));
         ok = rc == 0 || errno == EINPROGRESS;
         c->connecting = true; /* Also schedule initial TLS write on immediate connect. */
      }
      g_object_unref(address);
      if (!ok) { if (fd >= 0) close(fd); continue; }
      char *numeric = g_inet_address_to_string(ip);
      g_strlcpy(c->ip, numeric, sizeof(c->ip));
      g_free(numeric);
      Log(LOG_INFO, "mesh.native", "Mesh TCP dial %s resolved to %s:%u",
          c->authority, c->ip, c->port);
      attach_socket(c, fd);
      return !c->dead;
   }
   return false;
}

static void resolved(GObject *source, GAsyncResult *result, gpointer arg) {
   struct mesh_wss *c = arg;
   GError *error = NULL;
   c->addresses = g_resolver_lookup_by_name_finish(G_RESOLVER(source), result, &error);
   c->next_address = c->addresses;
   c->resolving = false;
   if (!c->dead && !connect_next(c)) reject(c, error ? error->message : "No reachable mesh address");
   g_clear_error(&error);
}

rr_transport_t *rr_mesh_wss_connect(const char *url, const char *pin, rr_transport_callback_t cb, void *arg) {
   GUri *uri = url ? g_uri_parse(url, G_URI_FLAGS_NONE, NULL) : NULL;
   const char *host = uri ? g_uri_get_host(uri) : NULL;
   int port = uri ? g_uri_get_port(uri) : -1;
   gsize n = 0;
   guchar *expected = pin ? g_base64_decode(pin, &n) : NULL;
   bool valid = uri && host && *host && strlen(host) < 256 &&
      !g_strcmp0(g_uri_get_scheme(uri), "wss") && !g_strcmp0(g_uri_get_path(uri), "/link") &&
      !g_uri_get_userinfo(uri) && !g_uri_get_query(uri) && !g_uri_get_fragment(uri) &&
      port > 0 && port <= 65535 && n == 32;
   struct mesh_wss *c = valid ? allocate(cb, arg, true) : NULL;
   if (c) {
      memcpy(c->expected, expected, 32);
      c->port = port;
      g_strlcpy(c->host, host, sizeof(c->host));
      snprintf(c->authority, sizeof(c->authority), strchr(host, ':') ? "[%s]:%d" : "%s:%d", host, port);
      SSL_set_tlsext_host_name(c->ssl, host);
      c->resolve_cancel = g_cancellable_new();
      c->resolving = true;
      GResolver *resolver = g_resolver_get_default();
      g_resolver_lookup_by_name_async(resolver, host, c->resolve_cancel, resolved, c);
      g_object_unref(resolver);
   }
   if (uri) g_uri_unref(uri);
   g_free(expected);
   return c ? c->transport : NULL;
}

void rr_mesh_wss_abort(rr_transport_t *t, const char *reason) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   if (!c) return;
   reject(c, reason ? reason : "Local close");
   rr_transport_close(t);
   if (c->resolve_cancel) g_cancellable_cancel(c->resolve_cancel);
}
bool rr_mesh_wss_binding(rr_transport_t *t, uint8_t out[32]) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   if (!c || !c->open || c->dead) return false;
   memcpy(out, c->binding, 32);
   return true;
}
const char *rr_mesh_wss_ip(rr_transport_t *t) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   return c ? c->ip : "";
}
unsigned rr_mesh_wss_peer_port(rr_transport_t *t) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   return c ? c->peer_port : 0;
}
const char *rr_mesh_wss_path(rr_transport_t *t) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   return c ? c->path : "";
}
const char *rr_mesh_wss_user_agent(rr_transport_t *t) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   return c ? c->user_agent : "";
}
size_t rr_mesh_wss_queued(rr_transport_t *t) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   return c ? c->tx_len + (c->ws ? wslay_event_get_queued_msg_length(c->ws) : 0) : 0;
}
void rr_mesh_wss_ping(rr_transport_t *t) {
   struct mesh_wss *c = rr_transport_driver_context(t);
   if (!c || !c->open || c->dead) return;
   struct wslay_event_msg msg = { WSLAY_PING, NULL, 0 };
   if (wslay_event_queue_msg(c->ws, &msg)) reject(c, "Cannot queue heartbeat");
   rr_mesh_wss_arm(c);
}
void rr_mesh_wss_stop(void) {
   stopping = true;
   if (timer) { g_source_remove(timer); timer = 0; }
   for (struct mesh_wss *c = connections; c; c = c->next) rr_mesh_wss_abort(c->transport, "Server shutdown");
   /* Resolve cancellations before releasing borrowed application contexts. */
   while (connections) {
      maintenance(NULL);
      if (connections) g_main_context_iteration(NULL, TRUE);
   }
   if (timer) { g_source_remove(timer); timer = 0; }
   SSL_CTX_free(tls); tls = NULL;
}
