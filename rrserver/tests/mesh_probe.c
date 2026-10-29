/* Adversarial real TLS client. Deliberately independent of the production WSS driver and auth receiver; signs using the permanent identity API. */
#include <rrserver/identity.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <glib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
time_t now;
bool dying, restarting;

static bool exact(SSL *ssl, void *buf, size_t len) {
   size_t offset = 0;
   while (offset < len) {
      int n = SSL_read(ssl, (char *)buf + offset, len - offset);

      if (n <= 0) {
         assert(errno != EAGAIN && errno != EWOULDBLOCK);   /* timeout is not rejection */

         return false;
      }
      offset += n;
   }
   return true;
}

static bool send_text(SSL *ssl, const char *text) {
   size_t len = strlen(text), h = len < 126 ? 2 : 4;
   assert(len < 65536);
   uint8_t *wire = malloc(h + 4 + len);
   wire[0] = 0x81;
   wire[1] = 0x80 | (len < 126 ? len : 126);

   if (h == 4) {
      wire[2] = len >> 8;
      wire[3] = len;
   }
   assert(RAND_bytes(wire + h, 4) == 1);

   for (size_t i = 0 ; i < len ; ++i) {
      wire[h + 4 + i] = text[i] ^ wire[h + (i % 4)];
   }

   bool ok = SSL_write(ssl, wire, h + 4 + len) == (int)(h + 4 + len);
   free(wire);

   return ok;
}

static dict *receive(SSL *ssl) {
   uint8_t head[2];

   if (!exact(ssl, head, 2)) {
      return NULL;
   }

   if ((head[0] & 15) == 8) {
      return NULL;
   }
   assert(head[0] == 0x81 && !(head[1] & 0x80));
   size_t len = head[1] & 127;

   if (len == 126) {
      assert(exact(ssl, head, 2));
      len = head[0] * 256 + head[1];
   }
   assert(len < 8192);
   char text[8192];

   if (!exact(ssl, text, len)) {
      return NULL;
   }
   text[len] = 0;

   return json2dict(text);
}

int main(int argc, char **argv) {
   assert(argc == 10);
   const char *mode = argv[8];
   signal(SIGPIPE, SIG_IGN);
   assert(rr_identity_init(argv[1]));
   SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
   assert(ctx && SSL_CTX_load_verify_locations(ctx, argv[4], NULL));
   SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

   if (strcmp(mode, "no-cert")) {
      assert(SSL_CTX_use_certificate_file(ctx, argv[2], SSL_FILETYPE_PEM));
      assert(SSL_CTX_use_PrivateKey_file(ctx, argv[3], SSL_FILETYPE_PEM));
   }
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   struct timeval timeout = {
      .tv_sec = 6
   };
   setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
   setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
   struct sockaddr_in addr = {
      .sin_family = AF_INET, .sin_port = htons(atoi(argv[5]))
   };
   inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
   assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
   SSL *ssl = SSL_new(ctx);
   SSL_set_fd(ssl, fd);
   int handshake = SSL_connect(ssl);
   bool expect_tls_reject = !strcmp(mode, "no-cert") || !strcmp(mode, "untrusted-cert");

   if (handshake != 1) {
      assert(expect_tls_reject);
      goto done;
   }
   char request[1024];
   snprintf(request, sizeof(request), "GET /link HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
      "Sec-WebSocket-Protocol: %s\r\n\r\n", !strcmp(mode, "protocol") ? "rustyrig.v2" : "rustyrig-mesh.v1");

   if (SSL_write(ssl, request, strlen(request)) <= 0) {
      assert(expect_tls_reject);
      goto done;
   }
   char headers[4096] = {
      0
   };
   size_t used = 0;
   while (!strstr(headers, "\r\n\r\n")) {
      assert(used + 1 < sizeof(headers));

      if (!exact(ssl, headers + used, 1)) {
         assert(expect_tls_reject || !strcmp(mode, "protocol") || !strcmp(mode, "acl"));
         goto done;
      }
      headers[++used] = 0;
   }
   assert(!expect_tls_reject && strstr(headers, "101 Switching Protocols"));
   dict *challenge = receive(ssl);

   if (!strcmp(mode, "acl")) {
      assert(!challenge);
      goto done;
   }
   assert(challenge && !strcmp(dict_get(challenge, "mesh.auth", ""), "challenge-v1"));

   if (!strcmp(mode, "timeout")) {
      assert(!receive(ssl));
      dict_free(challenge);
      goto done;
   }

   if (!strcmp(mode, "preauth")) {
      assert(send_text(ssl, "{\"op\":\"link.hello\"}"));
      assert(!receive(ssl));
      dict_free(challenge);
      goto done;
   }

   if (!strcmp(mode, "oversize")) {
      char *big = g_strnfill(32769, 'x');
      assert(send_text(ssl, big));
      g_free(big);
      assert(!receive(ssl));
      dict_free(challenge);
      goto done;
   }
   uint8_t bytes[32], binding[32];
   assert(RAND_bytes(bytes, 32));
   char *nonce = g_base64_encode(bytes, 32);
   assert(SSL_export_keying_material(ssl, binding, 32, "EXPORTER-RustyRig-mesh-v1", 25, NULL, 0, 0));
   /* sizeof label - 1 is intentionally checked against the wire contract. */
   char *channel = g_base64_encode(binding, 32);
   char *hello = g_strdup_printf("{\"mesh.auth\":\"challenge-v1\",\"nonce\":\"%s\"}", nonce);
   assert(send_text(ssl, hello));
   g_free(hello);
   dict *remote_proof = receive(ssl);
   assert(remote_proof);
   dict *proof = dict_new();
   dict_add(proof, "mesh.auth", "proof-v1");
   dict_add(proof, "identity.domain", !strcmp(mode, "domain") ? "wrong.test" : "mesh.test");
   dict_add(proof, "identity.owner", !strcmp(mode, "uuid") ? "00000000-0000-4000-8000-000000000001" : argv[6]);
   dict_add(proof, "identity.public-key", rr_identity_public_base64());
   dict_add(proof, "site", argv[7]);
   dict_add(proof, "nonce", nonce);
   dict_add(proof, "challenge", dict_get(challenge, "nonce", ""));
   dict_add(proof, "channel", !strcmp(mode, "binding") ? nonce : channel);
   dict_add(proof, "role", !strcmp(mode, "role") ? "acceptor" : "initiator");
   assert(rr_identity_sign_catalog(proof));

   if (!strcmp(mode, "signature")) {
      char *sig = strdup(dict_get(proof, "identity.signature", ""));
      sig[0] = sig[0] == 'A' ? 'B' : 'A';
      dict_add(proof, "identity.signature", sig);
      free(sig);
   }
   char *wire = dict2json(!strcmp(mode, "reflection") ? remote_proof : proof);

   if (!strcmp(mode, "record")) {
      assert(g_file_set_contents(argv[9], wire, -1, NULL));
   }

   if (!strcmp(mode, "replay")) {
      free(wire);
      wire = NULL;
      assert(g_file_get_contents(argv[9], &wire, NULL, NULL));
   }
   assert(send_text(ssl, wire));
   free(wire);
   dict_free(proof);
   dict_free(remote_proof);
   dict_free(challenge);
   g_free(nonce);
   g_free(channel);

   if (!strcmp(mode, "record") || !strcmp(mode, "hello")) {
      hello = g_strdup_printf("{\"op\":\"link.hello\",\"node\":\"%s\",\"site\":\"%s\",\"password\":\"\",\"domain\":\"mesh.test\"}", argv[6], !strcmp(mode,
         "hello") ? "forged-site" : argv[7]);
      assert(send_text(ssl, hello));
      g_free(hello);
      dict *reply = receive(ssl);

      if (!strcmp(mode, "record")) {
         assert(reply && !strcmp(dict_get(reply, "op", ""), "link.hello"));
      } else {
         assert(!reply);
      }

      if (reply) {
         dict_free(reply);
      }
   } else {
      assert(!receive(ssl));
   }
done:
   SSL_free(ssl);
   SSL_CTX_free(ctx);
   close(fd);
   rr_identity_fini();
   printf("PASS: mesh probe %s\n", mode);

   return 0;
}
