/* Strict, bounded RFC 6455 opening handshake for the mesh-only endpoint. */
#include "mesh.wss.internal.h"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <string.h>
#include <stdio.h>

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
   bool valid = c->client ? !strcmp(lines[0], "HTTP/1.1 101 Switching Protocols") :
      !strcmp(lines[0], "GET /link HTTP/1.1");
   const char *names[] = {"upgrade", "connection", "sec-websocket-protocol",
      "sec-websocket-key", "sec-websocket-version", "sec-websocket-accept", "host"};
   char *values[7] = {0};
   for (unsigned i = 1; lines[i]; ++i) {
      char *colon = strchr(lines[i], ':');
      if (!colon || lines[i][0] == ' ' || lines[i][0] == '\t') { valid = false; break; }
      *colon++ = 0;
      for (unsigned j = 0; j < 7; ++j) {
         if (g_ascii_strcasecmp(lines[i], names[j])) continue;
         if (values[j]) valid = false;
         values[j] = g_strstrip(colon);
      }
      if (!g_ascii_strcasecmp(lines[i], "transfer-encoding") ||
          !g_ascii_strcasecmp(lines[i], "content-length") ||
          !g_ascii_strcasecmp(lines[i], "sec-websocket-extensions")) valid = false;
   }
   valid = valid && values[0] && !g_ascii_strcasecmp(values[0], "websocket") &&
      values[1] && token(values[1], "upgrade") && values[2] && !strcmp(values[2], MESH_PROTOCOL);
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
      if (valid) c->tx_len = snprintf((char *)c->tx, sizeof(c->tx),
         "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
         "Connection: Upgrade\r\nSec-WebSocket-Protocol: " MESH_PROTOCOL "\r\n"
         "Sec-WebSocket-Accept: %s\r\n\r\n", c->accept);
   }
   g_strfreev(lines);
   memmove(c->http, c->http + used, c->http_len - used);
   c->http_len -= used;
   return valid ? 1 : -1;
}
