//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Named ordered ACLs are application configuration; matching is reusable. */
#include <rrserver/acl.h>
#include <librrprotocol/server.events.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

static bool in_group(const char *group, void *context) {
   const http_user_t *account = context;

   if (!account || !account->enabled) {
      return false;
   }

   for (const char *p = account->privs ; *p ; ) {
      p += strspn(p, ", \t");
      size_t length = strcspn(p, ", \t");

      if (strlen(group) == length && !strncasecmp(p, group, length)) {
         return true;
      }
      p += length;
   }

   return false;
}

acl_result_t rrserver_acl_check(const char *section, const char *ip, const http_user_t *account, const char *user) {
   if (!section || cfg_get_bool("acl.invalid", false)) {
      return ACL_DENY;
   }
   char key[256];
   snprintf(key, sizeof(key), "%s.acl", section);
   const char *name = cfg_get(key);

   if (!name || !*name || strlen(name) >= 124 || strchr(name, '.')) {
      return ACL_DENY;
   }
   snprintf(key, sizeof(key), "acl:%s.count", name);
   int count = cfg_get_int(key, 0);

   if (count <= 0 || count > MAX_ACL_RULES) {
      return ACL_DENY;
   }
   acl_subject_t subject;

   if (!acl_subject_init(&subject, ip, user, in_group, (void *)account)) {
      return ACL_DENY;
   }
   subject.ignore_case = true;

   for (int i = 0 ; i < count ; i++) {
      snprintf(key, sizeof(key), "acl:%s.%03d", name, i);
      const char *stored = cfg_get(key);
      acl_rule_t rule;

      if (!stored || (strncmp(stored, "allow=", 6) && strncmp(stored, "deny=", 5)) ||
         !acl_rule_parse(&rule, !strncmp(stored, "allow=", 6), stored + (!strncmp(stored, "allow=", 6) ? 6 : 5))) {
         return ACL_DENY;
      }
      acl_result_t result = acl_rule_match(&rule, &subject);

      if (result != ACL_NO_MATCH) {
         return result;
      }
   }

   return ACL_DENY;
}

#ifdef USE_MONGOOSE
static void admission(const char *event, const void *data, size_t length, rrconn_t *client, void *context) {
   (void)event;
   (void)client;
   (void)context;

   if (length != sizeof(rr_server_admission_t)) {
      return;
   }
   rr_server_admission_t *request = (rr_server_admission_t *) data;
   char section[64];
   snprintf(section, sizeof(section), "listener:%s", request->listener);
   acl_result_t result = rrserver_acl_check(section, request->ip, request->account, request->account ? request->account->name : NULL);
   request->allowed = result == ACL_ALLOW || (!request->account && result == ACL_DEFER);

   if (!request->allowed) {
      Log(LOG_INFO, "acl", "Denied %s listener access from %s", request->listener, request->ip);
   }
}
#endif
void rrserver_acl_register_events(void) {
#ifdef USE_MONGOOSE
   event_on_binary(RR_SERVER_ADMISSION_EVENT, admission, NULL);
#endif
}
