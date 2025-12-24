
// is an admin or owner online?
bool is_admin_online(void) {
   if (http_client_list == NULL) {
      return false;
   }

   http_client_t *curr = http_client_list;
   while (curr) {
      if (!curr->is_ws || !curr->authenticated || curr->user == NULL) {
         return false;
      }
      if (has_priv(curr->user->uid, "admin|owner")) {
         return true;
      }
      curr = curr->next;
   }
   return false;
}

// is an elmer online?
bool is_elmer_online(void) {
   if (http_client_list == NULL) {
      return false;
   }

   http_client_t *curr = http_client_list;
   while (curr) {
      if (!curr->is_ws || !curr->authenticated || !curr->user) {
         continue;
      }
      if (client_has_flag(curr, FLAG_ELMER)) {
         Log(LOG_CRAZY, "auth", "is_elmer_online: returning cptr:<%x> - |%s|", curr, curr->chatname);
         return true;
      }
      curr = curr->next;
   }
   return false;
}
