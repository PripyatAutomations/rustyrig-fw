// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>
#include <sqlite3.h>
#include <rrserver/identity.h>
#include <librrprotocol/crypto.h>
#define JOINER "00000000-0000-4000-8000-000000000002"
int main(void) {
   char *dir = g_dir_make_tmp("rr-mesh-enroll-XXXXXX", NULL);
   assert(dir);
   assert(rr_crypto_init());
   assert(rr_identity_init(dir));
   sqlite3 *db = NULL;
   assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
   uint8_t issuer_spki_raw[32];
   assert(rr_crypto_random(issuer_spki_raw, sizeof(issuer_spki_raw)));
   char *issuer_spki = g_base64_encode(issuer_spki_raw, 32);
   char *token = rr_identity_invite_issue(db, "testdomain", "wss://127.0.0.1:4420/link", issuer_spki, JOINER, time(NULL) + 3600);
   assert(token);
   char **fields = g_strsplit(token, "\n", -1);
   assert(g_strv_length(fields) == 9);
   char *first = rr_identity_invite_challenge(db, fields[1], "testdomain", JOINER);
   assert(first && strlen(first) == 44);
   char *second = rr_identity_invite_challenge(db, fields[1], "testdomain", JOINER);
   assert(second && strcmp(first, second));
   assert(!rr_identity_invite_challenge_claim(db, fields[1], "testdomain", JOINER, first));
   assert(rr_identity_invite_challenge_claim(db, fields[1], "testdomain", JOINER, second));
   assert(!rr_identity_invite_challenge_claim(db, fields[1], "testdomain", JOINER, second));
   gsize pemlen = 0;
   guchar *pem = g_base64_decode(fields[7], &pemlen);
   FILE *stream = fmemopen(pem, pemlen, "r");
   assert(stream);
   rr_crypto_key_t *invitation_key = rr_crypto_key_read_pem(stream);
   fclose(stream);
   assert(invitation_key);
   uint8_t join_spki_raw[32];
   assert(rr_crypto_random(join_spki_raw, sizeof(join_spki_raw)));
   char *join_spki = g_base64_encode(join_spki_raw, 32);
   rr_crypto_key_t *permanent = rr_crypto_ed25519_generate();
   assert(permanent);
   uint8_t pub[32], signature[64];
   assert(rr_crypto_ed25519_public_bytes(permanent, pub));
   char *join_pub = g_base64_encode(pub, sizeof(pub));
   char *transcript = g_strdup_printf("rustyrig.mesh.enroll.v1\n%s\n%s\n%s\n%s\n%s\n%s\n", fields[1], "testdomain", JOINER, second, join_pub, join_spki);
   assert(rr_crypto_ed25519_sign(invitation_key, transcript, strlen(transcript), signature));
   assert(rr_identity_invite_verify_proof(db, fields[1], "testdomain", transcript, strlen(transcript), signature));
   assert(rr_identity_invite_redeem(db, fields[1], "testdomain", JOINER, join_pub, join_spki));
   assert(rr_identity_trust_verify(db, "testdomain", JOINER, join_pub, join_spki));
   /* An independent catalog pin cannot be displaced by enrollment. */
   const char *pinned_uuid = "00000000-0000-4000-8000-000000000003";
   assert(sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS mesh_identity_pins("
      "domain TEXT NOT NULL,owner_uuid TEXT NOT NULL,public_key TEXT NOT NULL,"
      "first_seen INTEGER NOT NULL DEFAULT (unixepoch())," "PRIMARY KEY(domain,owner_uuid))", NULL, NULL, NULL) == SQLITE_OK);
   sqlite3_stmt *pin = NULL;
   assert(sqlite3_prepare_v2(db, "INSERT INTO mesh_identity_pins(domain,owner_uuid,public_key)" " VALUES(?1,?2,?3)", -1, &pin, NULL) == SQLITE_OK);
   sqlite3_bind_text(pin, 1, "testdomain", -1, SQLITE_STATIC);
   sqlite3_bind_text(pin, 2, pinned_uuid, -1, SQLITE_STATIC);
   sqlite3_bind_text(pin, 3, join_pub, -1, SQLITE_TRANSIENT);
   assert(sqlite3_step(pin) == SQLITE_DONE);
   sqlite3_finalize(pin);
   rr_crypto_key_t *intruder = rr_crypto_ed25519_generate();
   assert(intruder && rr_crypto_ed25519_public_bytes(intruder, pub));
   char *intruder_pub = g_base64_encode(pub, sizeof(pub));
   assert(!rr_identity_trust_enroll(db, "testdomain", pinned_uuid, intruder_pub, join_spki));
   assert(rr_identity_trust_enroll(db, "testdomain", pinned_uuid, join_pub, join_spki));
   g_free(intruder_pub);
   rr_crypto_key_free(intruder);
   assert(rr_identity_invite_redeemed(db, fields[1], "testdomain", JOINER, join_pub, join_spki));
   assert(!rr_identity_invite_redeemed(db, fields[1], "other-domain", JOINER, join_pub, join_spki));
   assert(!rr_identity_invite_redeemed(db, fields[1], "testdomain", JOINER, fields[6], join_spki));
   assert(!rr_identity_invite_redeem(db, fields[1], "testdomain", JOINER, join_pub, join_spki));
   assert(!rr_identity_invite_challenge(db, fields[1], "testdomain", JOINER));
   assert(!rr_identity_invite_verify_proof(db, fields[1], "testdomain", transcript, strlen(transcript), signature));
   /* Re-enrollment cannot replace the active node signing identity. */
   char *replacement = rr_identity_invite_issue(db, "testdomain", "wss://127.0.0.1:4420/link", issuer_spki, JOINER, time(NULL) + 3600);
   assert(replacement);
   char **replacement_fields = g_strsplit(replacement, "\n", -1);
   rr_crypto_key_t *different = rr_crypto_ed25519_generate();
   assert(different && rr_crypto_ed25519_public_bytes(different, pub));
   char *different_pub = g_base64_encode(pub, sizeof(pub));
   assert(!rr_identity_invite_redeem(db, replacement_fields[1], "testdomain", JOINER, different_pub, join_spki));
   assert(rr_identity_trust_verify(db, "testdomain", JOINER, join_pub, join_spki));
   assert(!rr_identity_trust_verify(db, "testdomain", JOINER, different_pub, join_spki));
   g_free(different_pub);
   rr_crypto_key_free(different);
   g_strfreev(replacement_fields);
   memset(replacement, 0, strlen(replacement));
   g_free(replacement);
   g_free(transcript);
   g_free(join_pub);
   g_free(join_spki);
   g_free(first);
   g_free(second);
   g_free(issuer_spki);
   rr_crypto_key_free(permanent);
   rr_crypto_key_free(invitation_key);
   memset(pem, 0, pemlen);
   g_free(pem);
   g_strfreev(fields);
   memset(token, 0, strlen(token));
   g_free(token);
   sqlite3_close(db);
   rr_identity_fini();
   char *keypath = g_build_filename(dir, "server-ed25519.pem", NULL);
   remove(keypath);
   g_free(keypath);
   rmdir(dir);
   g_free(dir);
   puts("PASS: single-use enrollment challenge and atomic identity trust");

   return 0;
}
