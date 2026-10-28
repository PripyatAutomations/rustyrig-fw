/* Strict, bounded RFC 6455 opening handshake for the mesh-only endpoint. */
#include "mesh.wss.internal.h"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>
#include <librrprotocol/wire-version.h>

static bool accept_key(const char *key, char out[32]) {
   char input[128];
   unsigned char digest[EVP_MAX_MD_SIZE];
   unsigned n = 0;
   int len = snprintf(input, sizeof(input), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
   if (len < 0 || len >= (int)sizeof(input) ||
       !EVP_Digest(input, len, digest, &n, EVP_sha1(), NULL) || n != 20) return false;
   EVP_EncodeBlock((unsigned char *)out, digest, n);
   return true;
}

bool rr_mesh_upgrade_prepare(struct mesh_wss *c) {
   if (!c->client) return true;
   unsigned char nonce[16];
   char key[25];
   if (RAND_bytes(nonce, sizeof(nonce)) != 1) return false;
   EVP_EncodeBlock((unsigned char *)key, nonce, sizeof(nonce));
   if (!accept_key(key, c->accept)) return false;
   c->tx_len = snprintf((char *)c->tx, sizeof(c->tx),
      "GET /link HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n"
      "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: " MESH_PROTOCOL "\r\n\r\n",
      c->authority, key);
   return c->tx_len < sizeof(c->tx);
}

static bool token(const char *value, const char *wanted) {
   gchar **parts = g_strsplit(value, ",", -1);
   bool found = false;
   for (unsigned i = 0; parts[i]; ++i)
      if (!g_ascii_strcasecmp(g_strstrip(parts[i]), wanted)) found = true;
   g_strfreev(parts);
   return found;
}

int rr_mesh_upgrade_parse(struct mesh_wss *c) {
   char *end = strstr(c->http, "\r\n\r\n");
   if (!end) return c->http_len == sizeof(c->http) - 1 ? -1 : 0;
   size_t used = (size_t)(end - c->http) + 4;
   *end = 0;
   gchar **lines = g_strsplit(c->http, "\r\n", -1);
   bool mesh = !strcmp(lines[0], "GET /link HTTP/1.1");
   bool client_route = !strcmp(lines[0], "GET /ws HTTP/1.1") ||
      !strcmp(lines[0], "GET /ws/ HTTP/1.1");
   const char *protocol = c->client || mesh ? MESH_PROTOCOL : RR_WS_SUBPROTOCOL;
   bool valid = c->client ? !strcmp(lines[0], "HTTP/1.1 101 Switching Protocols") :
      mesh || client_route;
   const char *names[] = {"upgrade", "connection", "sec-websocket-protocol",
      "sec-websocket-key", "sec-websocket-version", "sec-websocket-accept", "host", "user-agent"};
   char *values[8] = {0};
   for (unsigned i = 1; lines[i]; ++i) {
      char *colon = strchr(lines[i], ':');
      if (!colon || lines[i][0] == ' ' || lines[i][0] == '\t') { valid = false; break; }
      *colon++ = 0;
      for (unsigned j = 0; j < 8; ++j) {
         if (g_ascii_strcasecmp(lines[i], names[j])) continue;
         if (values[j]) valid = false;
         values[j] = g_strstrip(colon);
      }
      if (!g_ascii_strcasecmp(lines[i], "transfer-encoding") ||
          !g_ascii_strcasecmp(lines[i], "content-length") ||
          (!client_route && !g_ascii_strcasecmp(lines[i], "sec-websocket-extensions"))) valid = false;
   }
   valid = valid && values[0] && !g_ascii_strcasecmp(values[0], "websocket") &&
      values[1] && token(values[1], "upgrade") && values[2] && !strcmp(values[2], protocol);
   if (c->client) {
      valid = valid && values[5] && !strcmp(values[5], c->accept);
   } else {
      valid = valid && values[3] && strlen(values[3]) == 24 && values[4] &&
         !strcmp(values[4], "13") && values[6] && *values[6];
      if (valid) {
         gsize n;
         guchar *raw = g_base64_decode(values[3], &n);
         char *canonical = g_base64_encode(raw, n);
         valid = n == 16 && !strcmp(values[3], canonical) && accept_key(values[3], c->accept);
         g_free(raw); g_free(canonical);
      }
      if (valid && mesh && !c->peer_verified) valid = false;
      if (valid) {
         c->client_route = client_route;
         g_strlcpy(c->path, client_route ? "/ws" : "/link", sizeof(c->path));
         if (values[7]) g_strlcpy(c->user_agent, values[7], sizeof(c->user_agent));
         c->tx_len = snprintf((char *)c->tx, sizeof(c->tx),
            "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Protocol: %s\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n", protocol, c->accept);
      }
   }
   g_strfreev(lines);
   memmove(c->http, c->http + used, c->http_len - used);
   c->http_len -= used;
   return valid ? 1 : -1;
}

/* One bounded POST, no chunked encoding or ambiguous request framing. */
int rr_native_http_request(struct mesh_wss *c, rr_mesh_http_fn handler) {
   char *end = strstr(c->http, "\r\n\r\n");
   if (!end) return c->http_len == sizeof(c->http)-1 ? -1 : 0;
   size_t head = (size_t)(end - c->http) + 4;
   char *line = strstr(c->http, "\r\n");
   if (!line || (size_t)(line - c->http) >= 127) return -1;
   char request[128];
   memcpy(request, c->http, line - c->http);
   request[line-c->http] = 0;
   if (!strcmp(request, "GET /link HTTP/1.1") ||
       !strcmp(request, "GET /ws HTTP/1.1") ||
       !strcmp(request, "GET /ws/ HTTP/1.1")) return 1;
   char path[64], extra;
   if (sscanf(request, "POST %63s HTTP/1.1%c", path, &extra) != 1 ||
       strncmp(path, "/mesh/enroll/", 13)) return -1;
   char *headers = g_strndup(line + 2, end - (line + 2));
   gchar **rows = g_strsplit(headers, "\r\n", -1);
   size_t length = 0;
   bool found = false, valid = true, host = false;
   for (unsigned i = 0; rows[i] && *rows[i]; ++i) {
      char *colon = strchr(rows[i], ':');
      if (!colon || rows[i][0] == ' ' || rows[i][0] == '\t') { valid = false; break; }
      *colon++ = 0;
      char *value = g_strstrip(colon);
      if (!g_ascii_strcasecmp(rows[i], "Host")) { if (host || !*value) valid = false; host = true; }
      if (!g_ascii_strcasecmp(rows[i], "Content-Length")) {
         if (found || !*value) { valid = false; break; }
         found = true;
         for (const char *p = value; *p; ++p) if (*p < '0' || *p > '9') valid = false;
         if (valid) length = g_ascii_strtoull(value, NULL, 10);
      }
      if (!g_ascii_strcasecmp(rows[i], "Transfer-Encoding") ||
          !g_ascii_strcasecmp(rows[i], "Upgrade")) valid = false;
   }
   g_strfreev(rows); g_free(headers);
   if (!valid || !host || !found || length > 4096 || head + length > sizeof(c->http)-1) return -1;
   if (c->http_len < head + length) return 0;
   if (c->http_len != head + length) return -1;
   char *body = g_strndup(c->http + head, length);
   int status = 404;
   char *reply = NULL;
   if (handler) handler(path, body, length, &status, &reply);
   g_free(body);
   if (!reply) reply = g_strdup("Not found\n");
   size_t n = strlen(reply);
   c->tx_len = snprintf((char *)c->tx, sizeof(c->tx),
      "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\nCache-Control: no-store\r\n"
      "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
      status, status == 200 ? "OK" : status == 404 ? "Not Found" : "Forbidden", n, reply);
   g_free(reply);
   c->http_reply = true;
   return 2;
}
