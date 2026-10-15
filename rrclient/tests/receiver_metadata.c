#include <assert.h>
#include <rrclient/objects.events.c>
static dict *url_state, *contact_state, *connected_state;
static unsigned learned;
const dict *rr_object_cache_property(rr_object_cache_t *unused, const char *uuid, const char *name, bool descriptor) {
   assert(!strcmp(uuid, "receiver-id") && !descriptor);

   return !strcmp(name, "receiver.url") ? url_state : !strcmp(name, "receiver.contact") ? contact_state :
          !strcmp(name, "receiver.connected_at") ? connected_state : NULL;
}
void receiver_directory_queue_learn(const char *url, dict *fields) {
   assert(!strcmp(url, "http://receiver.invalid:8073/"));
   assert(!strcmp(dict_get(fields, "contact", ""), "live@example.invalid"));
   assert(!strcmp(dict_get(fields, "connected_at", ""), "1792080000"));
   learned++;
}
int main(void) {
   cache = (rr_object_cache_t *) 1;
   url_state = dict_new();
   contact_state = dict_new();
   connected_state = dict_new();
   dict_add_bool(url_state, "property.known", true);
   dict_add(url_state, "property.value", "http://receiver.invalid:8073/");
   dict_add_bool(contact_state, "property.known", true);
   dict_add(contact_state, "property.value", "live@example.invalid");
   dict_add_bool(connected_state, "property.known", false);
   dict_add(connected_state, "property.value", "1792080000");
   dict *message = dict_new();
   dict_add(message, "target", "receiver-id");
   dict_add(message, "property.name", "receiver.contact");
   learn_receiver_metadata(message);
   assert(!learned);            // No successful connection observed yet.
   dict_add_bool(connected_state, "property.known", true);
   learn_receiver_metadata(message);
   assert(learned == 1);
   dict_add(message, "property.name", "frequency");
   learn_receiver_metadata(message);
   assert(learned == 1);        // Ordinary tuning never rewrites the directory database.
   dict_free(message);
   dict_free(url_state);
   dict_free(contact_state);
   dict_free(connected_state);
   puts("PASS: live rig metadata reaches the receiver cache only after a confirmed connection, with its observation time");
}
