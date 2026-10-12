/* Named ordered ACLs are application configuration; matching is reusable. */
#include <rrserver/acl.h>
#include <librrprotocol/server.events.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#define MAX_ACL_RULES 256

static char *trim(char *text) {
   while (isspace((unsigned char)*text))
      text++;
   char *end = text + strlen(text);
   while (end > text && isspace((unsigned char)end[-1]))
      *--end = 0;
   return text;
}
static bool config_section(const char *path, int line, const char *section, const char *input) {
   char *copy = strdup(input);
   bool ok = false;
   if (!copy || strlen(section) >= 128)
      goto done;
   char *value = strchr(copy, '=');
   if (!value)
      goto done;
   *value++ = 0;
   char *key = trim(copy);
   value = trim(value);
   char fullkey[256];
   if (!strncmp(section, "listener:", 9)) {
      if (strcmp(key, "acl") || (strcmp(section, "listener:http") && strcmp(section, "listener:https") &&
            strcmp(section, "listener:link") && strcmp(section, "listener:mqtt") && strcmp(section, "listener:media")))
         goto done;
      snprintf(fullkey, sizeof(fullkey), "%s.acl", section);
      ok = dict_add(cfg, fullkey, value) == 0;
   } else if (!strncmp(section, "acl:", 4) && section[4] && !strchr(section + 4, '.')) {
      acl_rule_t rule;
      if ((strcmp(key, "allow") && strcmp(key, "deny")) || !acl_rule_parse(&rule, !strcmp(key, "allow"), value))
         goto done;
      snprintf(fullkey, sizeof(fullkey), "%s.count", section);
      int count = cfg_get_int(fullkey, 0);
      if (count < 0 || count >= MAX_ACL_RULES)
         goto done;
      char number[16], rulekey[256], stored[264];
      snprintf(rulekey, sizeof(rulekey), "%s.%03d", section, count);
      snprintf(stored, sizeof(stored), "%s=%s", key, value);
      snprintf(number, sizeof(number), "%d", count + 1);
      ok = dict_add(cfg, rulekey, stored) == 0 && dict_add(cfg, fullkey, number) == 0;
   }
 done:
   if (!ok) {
      dict_add(cfg, "acl.invalid", "true");
      Log(LOG_CRIT, "acl", "Invalid ACL configuration at %s:%d in [%s]", path, line, section);
   }
   free(copy);
   return !ok;
}
static bool owns_key(const char *key) {
   return !strncmp(key, "acl:", 4) || !strcmp(key, "acl.invalid") ||
      !strncmp(key, "listener:", 9) || !strncmp(key, "peer.", 5) || !strncmp(key, "peer:", 5) || !strncmp(key, "rtp.", 4) || !strncmp(key, "rtp:", 4);
}
static bool save_sections(FILE *fp, const char *path) {
   (void)path;
   dict *seen = dict_new();
   if (!seen)
      return true;
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      if (!owns_key(key) || !strcmp(key, "acl.invalid"))
         continue;
      const char *dot = strchr(key, '.');
      if (!dot || (size_t)(dot - key) >= 128)
         continue;
      char section[128], fullkey[256];
      memcpy(section, key, dot - key);
      section[dot - key] = 0;
      if (dict_get(seen, section, NULL))
         continue;
      dict_add(seen, section, "true");
      fprintf(fp, "\n[%s]\n", section);
      if (!strncmp(section, "acl:", 4)) {
         snprintf(fullkey, sizeof(fullkey), "%s.count", section);
         int count = cfg_get_int(fullkey, 0);
         for (int i = 0; i < count; i++) {
            snprintf(fullkey, sizeof(fullkey), "%s.%03d", section, i);
            const char *rule = cfg_get(fullkey);
            if (rule)
               fprintf(fp, "%s\n", rule);
         }
      } else {
         int next = 0;
         const char *entry;
         char *stored;
         while ((next = dict_enumerate(cfg, next, &entry, &stored)) >= 0) {
            if (!strncmp(entry, section, strlen(section)) && entry[strlen(section)] == '.' && stored) {
               fprintf(fp, "%s=%s\n", entry + strlen(section) + 1, stored);
            }
         }
      }
   }
   dict_free(seen);
   return ferror(fp) != 0;
}
bool rrserver_acl_config_init(void) {
   return cfg_add_callback(NULL, "acl:*", config_section) && cfg_add_callback(NULL, "listener:*", config_section) &&
      cfg_add_owned_save_callback("named ACLs, listeners and peers", save_sections, owns_key);
}
bool rrserver_acl_validate(void) {
   if (cfg_get_bool("acl.invalid", false))
      return false;
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(cfg, rank, &key, &value)) >= 0) {
      size_t length = strlen(key);
      if (length < 4 || strcmp(key + length - 4, ".acl") || !value || !*value)
         continue;
      if (strncmp(key, "listener:", 9) && strncmp(key, "peer.", 5) && strncmp(key, "peer:", 5) && strncmp(key, "rtp.", 4) && strncmp(key, "rtp:", 4))
         continue;
      char countkey[256];
      snprintf(countkey, sizeof(countkey), "acl:%s.count", value);
      if (strchr(value, '.') || strlen(value) >= 124 || !cfg_get(countkey)) {
         Log(LOG_CRIT, "acl", "Unknown ACL '%s' referenced by %s", value, key);
         return false;
      }
   }
   return true;
}
static bool in_group(const char *group, void *context) {
   const http_user_t *account = context;
   if (!account || !account->enabled)
      return false;
   for (const char *p = account->privs; *p;) {
      p += strspn(p, ", \t");
      size_t length = strcspn(p, ", \t");
      if (strlen(group) == length && !strncasecmp(p, group, length))
         return true;
      p += length;
   }
   return false;
}
acl_result_t rrserver_acl_check(const char *section, const char *ip, const http_user_t *account, const char *user) {
   if (!section || cfg_get_bool("acl.invalid", false))
      return ACL_DENY;
   char key[256];
   snprintf(key, sizeof(key), "%s.acl", section);
   const char *name = cfg_get(key);
   if (!name || !*name || strlen(name) >= 124 || strchr(name, '.'))
      return ACL_DENY;
   snprintf(key, sizeof(key), "acl:%s.count", name);
   int count = cfg_get_int(key, 0);
   if (count <= 0 || count > MAX_ACL_RULES)
      return ACL_DENY;
   acl_subject_t subject;
   if (!acl_subject_init(&subject, ip, user, in_group, (void *)account))
      return ACL_DENY;
   subject.ignore_case = true;
   for (int i = 0; i < count; i++) {
      snprintf(key, sizeof(key), "acl:%s.%03d", name, i);
      const char *stored = cfg_get(key);
      acl_rule_t rule;
      if (!stored || (strncmp(stored, "allow=", 6) && strncmp(stored, "deny=", 5)) ||
         !acl_rule_parse(&rule, !strncmp(stored, "allow=", 6), stored + (!strncmp(stored, "allow=", 6) ? 6 : 5)))
         return ACL_DENY;
      acl_result_t result = acl_rule_match(&rule, &subject);
      if (result != ACL_NO_MATCH)
         return result;
   }
   return ACL_DENY;
}

#ifdef USE_MONGOOSE
static void admission(const char *event, const void *data, size_t length, rrconn_t *client, void *context) {
   (void)event;
   (void)client;
   (void)context;
   if (length != sizeof(rr_server_admission_t))
      return;
   rr_server_admission_t *request = (rr_server_admission_t *) data;
   char section[64];
   snprintf(section, sizeof(section), "listener:%s", request->listener);
   acl_result_t result = rrserver_acl_check(section, request->ip, request->account,
      request->account ? request->account->name : NULL);
   request->allowed = result == ACL_ALLOW || (!request->account && result == ACL_DEFER);
   if (!request->allowed)
      Log(LOG_INFO, "acl", "Denied %s listener access from %s", request->listener, request->ip);
}
#endif
void rrserver_acl_register_events(void) {
#ifdef USE_MONGOOSE
   event_on_binary(RR_SERVER_ADMISSION_EVENT, admission, NULL);
#endif
}
