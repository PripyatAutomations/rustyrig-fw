#if     0
bool handle_talk_cmd() {
   // XXX: Move this to rrserver's event handler for chat.msg
   //sqlite3 *masterdb = NULL;
   /*
    *  bool db_add_chat_msg(sqlite3 *db, time_t msg_ts, const char *msg_src,
    * const char *msg_dest, const char *msg_type, const char *msg_data) {
    *  (void)db;
    *  (void)msg_ts;
    *  (void)msg_src;
    *  (void)msg_dest;
    *  (void)msg_type;
    *  (void)msg_data;
    *  return false;
    *  }
    */

   // Log to database, if configured
   if (cfg_get_bool("chat.log", false) ) {
      bool db_res = db_add_chat_msg(masterdb, now, cptr->chatname, channel, msg_type, data);

      if (!db_res) {
         fprintf(stderr, "db_add_chat_msg failed\n");
      }
   }
   const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "msg", VAL_STR, "talk.data", data, VAL_STR, "talk.from",
      cptr->chatname, VAL_STR, "talk.target", channel, VAL_STR, "talk.msg_type", msg_type, VAL_LONG, "talk.ts", now);

   return false;
}
