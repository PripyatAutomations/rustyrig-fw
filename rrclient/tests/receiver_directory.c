#include <assert.h>
#include <rrclient/receiver.directory.c>
time_t now = 1792080000;
static const char *database;
const char *cfg_get(const char *key) {
   return !strcmp(key, "receiver-directory.cache") ? database : "";
}
void Log(logpriority_t priority, const char *subsystem, const char *format, ...) { }
static const char *fixture = "{\"schema\":1,\"fetched_at\":\"2026-10-15T14:00:00Z\",\"receivers\":["
   "{\"url\":\"http://allowed.invalid:8073/\",\"name\":\"Allowed <b>RX</b>\",\"contact\":\"sysop@example.invalid\",\"loc\":\"Somewhere\",\"ext_api\":2,\"users_max\":4,\"bands\":[10000,30000000]},"
   "{\"url\":\"http://web.invalid/\",\"ext_api\":0},"
   "{\"url\":\"http://unknown.invalid/\"},"
   "{\"url\":\"http://offline.invalid/\",\"ext_api\":4,\"offline\":true},"
   "{\"url\":\"http://flagged.invalid/\",\"ext_api\":4,\"flagged\":true},"
   "{\"url\":\"http://invalidpolicy.invalid/\",\"ext_api\":\"4\"},"
   "{\"url\":\"javascript:alert(1)\",\"ext_api\":8}]}";
static void pump(void) {
   while (job) {
      now = time(NULL);
      g_main_context_iteration(NULL, TRUE);
   }
}
int main(int argc, char **argv) {
   assert(argc >= 2);
   database = argv[1];
   event_init();
   assert(!directory_url_allowed("https://kiwisdr.com/public/"));
   assert(!directory_url_allowed("https://files.kiwisdr.com/public/"));
   assert(!directory_url_allowed("http://some-mirror.invalid/list"));
   assert(!directory_url_allowed("file:///tmp/list"));
   assert(directory_url_allowed("https://cdn.example.invalid/receivers.json"));
   assert(directory_url_allowed("http://127.0.0.1:8080/list"));
   const char *invalid[] = { "[]", "{\"schema\":2,\"receivers\":[]}", "{\"schema\":1,\"receivers\":[{}]", "{\"schema\":1,\"receivers\":[]}<html>", "{\"provider\":\"other-websdr\",\"schema\":1,\"receivers\":[]}" };
   for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++) assert(!parse(invalid[i], strlen(invalid[i])));
   GPtrArray *list = parse(fixture, strlen(fixture));
   assert(list && list->len == 6);
   receiver_directory_entry_t *first = g_ptr_array_index(list, 0);
   assert(receiver_directory_entry_accessible(first));
   assert(first->low_hz == 10000 && first->high_hz == 30000000);
   assert(!strcmp(first->contact, "sysop@example.invalid"));
   char *config = receiver_directory_bridge_config(first);
   assert(config && strstr(config, "host = allowed.invalid") && strstr(config, "rx-min-hz = 10000"));
   g_free(config);
   for (guint i = 1; i < list->len; i++) {
      assert(!receiver_directory_entry_accessible(g_ptr_array_index(list, i)));
      assert(!receiver_directory_bridge_config(g_ptr_array_index(list, i)));
   }
   assert(((receiver_directory_entry_t *)g_ptr_array_index(list, 2))->ext_api == -1);
   g_ptr_array_free(list, TRUE);
   char *path = g_strconcat(database, ".json", NULL);
   assert(g_file_set_contents(path, fixture, -1, NULL));
   assert(receiver_directory_import(path));
   assert(receiver_directory_count() == 6);
   assert(receiver_directory_set_state("http://allowed.invalid:8073/", "unreachable", "Connection failed (user report)"));
   assert(!receiver_directory_entry_accessible(receiver_directory_receiver(0)));
   assert(!receiver_directory_set_state("http://allowed.invalid:8073/", "bogus", ""));
   receiver_directory_shutdown();
   assert(receiver_directory_count() == 6);
   assert(!strcmp(receiver_directory_receiver(0)->local_state, "unreachable"));
   assert(receiver_directory_receiver(0)->state_updated == now);
   assert(receiver_directory_set_state("http://allowed.invalid:8073/", "restricted", "Operator restriction"));
   assert(!receiver_directory_entry_accessible(receiver_directory_receiver(0)));
   assert(receiver_directory_set_state("http://allowed.invalid:8073/", "unknown", "Cleared"));
   assert(receiver_directory_entry_accessible(receiver_directory_receiver(0)));
   assert(!receiver_directory_import("/nonexistent/receiver-directory.json"));
   assert(receiver_directory_count() == 6);
   // Re-import/refresh never erases local restrictions.
   assert(receiver_directory_set_state("http://allowed.invalid:8073/", "restricted", "Operator restriction"));
   assert(receiver_directory_import(path));
   assert(!receiver_directory_entry_accessible(receiver_directory_receiver(0)));
   // Learned fields win over directory updates, blanks and removal. Missing
   // entries retain both metadata and status until absent for ninety days.
   dict *learned = dict_new();
   dict_add(learned, "contact", "learned@example.invalid");
   dict_add(learned, "name", "Live receiver name");
   assert(receiver_directory_learn("http://allowed.invalid:8073/", learned));
   dict_free(learned);
   assert(receiver_directory_import(path));
   assert(!strcmp(receiver_directory_receiver(0)->contact, "learned@example.invalid"));
   assert(!strcmp(receiver_directory_receiver(0)->name, "Live receiver name"));
   learned = dict_new();
   dict_add(learned, "contact", "stale@example.invalid");
   dict_add_llong(learned, "connected_at", now - 1);
   assert(receiver_directory_learn("http://allowed.invalid:8073", learned));
   dict_free(learned);
   assert(!strcmp(receiver_directory_receiver(0)->contact, "learned@example.invalid"));
   const char *empty = "{\"schema\":1,\"receivers\":[]}";
   assert(g_file_set_contents(path, empty, -1, NULL));
   now += 89 * 86400;
   assert(receiver_directory_import(path));
   assert(receiver_directory_count() == 6 && receiver_directory_receiver(0)->deleted);
   assert(!strcmp(receiver_directory_receiver(0)->contact, "learned@example.invalid"));
   receiver_directory_shutdown();
   assert(receiver_directory_count() == 6 && receiver_directory_receiver(0)->deleted);
   now += 2 * 86400;
   assert(receiver_directory_import(path));
   assert(receiver_directory_count() == 0);
   puts("PASS: live metadata wins, removed receivers retain fields/status, and missing entries expire after ninety days");
   const char *live = g_getenv("RRCLIENT_KIWI_TEST_LIVE_JSON");
   if (live) {
      assert(receiver_directory_import(live));
      size_t allowed = 0, unknown = 0, restricted = 0;
      for (size_t i = 0; i < receiver_directory_count(); i++) {
         const receiver_directory_entry_t *r = receiver_directory_receiver(i);
         allowed += receiver_directory_entry_accessible(r);
         unknown += r->ext_api < 0;
         restricted += r->ext_api == 0;
      }
      printf("PASS: real mirror snapshot parsed locally: %zu receivers, %zu API-eligible, %zu unknown policies, %zu web-only\n", receiver_directory_count(), allowed, unknown, restricted);
   }
   g_unlink(path); g_free(path);
   receiver_directory_shutdown();
   g_unlink(database);
   if (argc == 3) {
      g_setenv("RRCLIENT_RECEIVER_DIRECTORY_URL", argv[2], TRUE);
      now = time(NULL);
      assert(receiver_directory_refresh());
      assert(!receiver_directory_refresh()); // Coalesced while in flight.
      pump();
      assert(receiver_directory_count() == 1);
      assert(!receiver_directory_refresh()); // Cooldown also applies to explicit refresh.
      // Persisted cooldown survives full service destruction/recreation.
      receiver_directory_shutdown();
      assert(receiver_directory_count() == 1);
      assert(!receiver_directory_refresh());
      next_request = 0;
      assert(receiver_directory_refresh()); pump(); // Conditional 304 retains list.
      assert(receiver_directory_count() == 1);
      next_request = 0;
      assert(receiver_directory_refresh()); pump(); // 429 preserves good cached data.
      assert(receiver_directory_count() == 1 && next_request >= now + 7200);
      next_request = 0;
      g_free(source); source = g_strdup("https://previous.example.invalid/list");
      assert(receiver_directory_refresh()); pump(); // A changed/broken mirror also preserves cached records.
      assert(receiver_directory_count() == 1);
      assert(strstr(receiver_directory_status(), "failed"));
   }
   receiver_directory_shutdown(); event_shutdown();
   puts("PASS: bounded directory parsing, API policy, URL rules, offline hints, persistent receiver reports, cache and throttled refresh");
}
