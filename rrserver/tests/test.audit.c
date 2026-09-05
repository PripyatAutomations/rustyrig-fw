// standalone test harness for rrserver/audit.c - not part of the build
#include <stdio.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <rrserver/database.h>
#include <rrserver/globalstate.h>

extern void audit_init(void);               // audit.c
extern void rrserver_register_events(void); // events.c
time_t now;
bool dying = false; // these are provided by rrserver/main.c in the real server
bool restarting = false;
struct GlobalState rig;                              // stubbed here, real one lives in rrserver/main.c
void shutdown_rig(uint32_t signum) { (void)signum; } // stub, real one in rrserver/main.c

static void dump_rows(void)
{
   sqlite3_stmt *stmt;
   if (sqlite3_prepare_v2(masterdb, "SELECT id, timestamp, username, event_type, details FROM audit_log ORDER BY id;", -1, &stmt, NULL) != SQLITE_OK)
   {
      fprintf(stderr, "FAIL: prepare: %s\n", sqlite3_errmsg(masterdb));
      return;
   }
   printf("--- audit_log contents ---\n");
   int rc;
   while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
   {
      printf("id:%d ts:%s user:%s type:%s details:%s\n",
             sqlite3_column_int(stmt, 0), sqlite3_column_text(stmt, 1),
             sqlite3_column_text(stmt, 2), sqlite3_column_text(stmt, 3),
             sqlite3_column_text(stmt, 4));
   }
   sqlite3_finalize(stmt);
}

int main(void)
{
   now = time(NULL);
   masterdb = db_open("/tmp/audit_test.db");
   if (!masterdb)
   {
      fprintf(stderr, "FAIL: db_open\n");
      return 1;
   }
   char *err = NULL;
   if (sqlite3_exec(masterdb, "CREATE TABLE IF NOT EXISTS audit_log (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, username TEXT NOT NULL, event_type TEXT NOT NULL, details TEXT);", NULL, NULL, &err) != SQLITE_OK)
   {
      fprintf(stderr, "FAIL: schema: %s\n", err);
      return 1;
   }

   audit_init();
   event_init(); // the event bus must be initialized before registering
   rrserver_register_events();

   // Simulate the chat !freq path: the rigctl event as emitted by srv.chat.c
   event_emit("rigctl", NULL,
              "{\"rigctl\":{\"from\":\"admin\",\"cmd\":\"freq\",\"vfo\":\"A\",\"freq\":7200000},\"msg\":{\"type\":\"rigctl\"}}");

   // Filter auth messages out of display/storage entirely - the audit callback
   // must still fire for them
   log_add_filter("auth", LOG_CRIT);
   Log(LOG_AUDIT, "auth", "User %s logged in from IP %s:%d", "alice", "192.168.1.10", 4242);
   Log(LOG_AUDIT, "ptt", "User %s set PTT to %s on vfo %s", "bob", "true", "A");
   Log(LOG_INFO, "test", "this INFO message must NOT be stored");
   Log(LOG_CRIT, "test", "this CRIT message must NOT be stored");
   Log(LOG_DEBUG, "test", "this DEBUG message must NOT be stored");
   Log(LOG_AUDIT, "test", "this message IS displayed (unfiltered) and IS stored once");

   dump_rows();
   return 0;
}
