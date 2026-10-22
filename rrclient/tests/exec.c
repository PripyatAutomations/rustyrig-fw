#include <assert.h>
#include <stdarg.h>
#include <rrclient/cmd.exec.c>
time_t now;
rrconn_t *ws_conn;
static rrconn_t first = {
   .authenticated = true
}, second;

static rrconn_t *current = &first;
static char local[65536], parsed[1024], plain[1024];
static unsigned parser_lines, chat_lines, dispatches;
const char *rrclient_active_room(void) {
   return "#original";
}
const char *ui_active_window_name(void) {
   return "#original";
}
rrconn_t *rrclient_active_connection(void) {
   return &first;
}
const char *rrclient_window_name(const char *room) {
   return room;
}
const char *rrclient_connection_name(const rrconn_t *connection) {
   assert(connection == &first);

   return "first";
}
rrconn_t *rrclient_connection_find(const char *name) {
   assert(!strcmp(name, "first"));

   return current;
}
bool ui_print(const char *room, const char *format, ...) {
   va_list args;
   va_start(args, format);
   size_t offset = strlen(local);
   vsnprintf(local + offset, sizeof(local) - offset, format, args);
   va_end(args);

   return false;
}
void rrclient_input_dispatch(rrconn_t *connection, const char *room, void (*callback) (void *), void *data) {
   if (connection) {
      assert(connection == &first && !strcmp(room, "#original"));
   }
   dispatches++;
   callback(data);
}
bool parse_chat_input_real(const char *text) {
   parser_lines++;
   snprintf(parsed, sizeof(parsed), "%s", text);

   return false;
}
bool parse_chat_input_text(const char *text) {
   chat_lines++;
   snprintf(plain, sizeof(plain), "%s", text);

   return false;
}
static bool run(const char *text) {
   char *args[] = {
      "exec", (char *)text
   };

   return cmd_exec(2, args);
}
static void finish(void) {
   uint64_t deadline = mono_us() + 5000000;
   while (exec_jobs && mono_us() < deadline) {
      g_main_context_iteration(NULL, FALSE);
      g_usleep(1000);
   }
   assert(!exec_jobs && !exec_source);
}
int main(void) {
   event_init();
   assert(!run("printf '%s\\n' 'a  b' 'c;d'"));
   finish();
   assert(strstr(local, "a  b") && strstr(local, "c;d") && !parser_lines && !chat_lines);
   /* -c is a direct file read into the local window, not shell execution or forwarding of apparent commands to a server. */
   char filename[] = "/tmp/rustyrig exec cat XXXXXX";
   int fd = g_mkstemp(filename);
   assert(fd >= 0);
   const char contents[] = "local file line\\n/quit\\n";
   assert(write(fd, contents, sizeof(contents) - 1) == (ssize_t) (sizeof(contents) - 1));
   assert(close(fd) == 0);
   local[0] = 0;
   char cat_command[256];
   snprintf(cat_command, sizeof(cat_command), "-c %s", filename);
   assert(!run(cat_command));
   assert(strstr(local, "local file line") && strstr(local, "/quit"));
   assert(!parser_lines && !chat_lines && !exec_jobs);
   assert(unlink(filename) == 0);
   assert(run(cat_command));
   assert(strstr(local, "cannot open"));
   local[0] = 0;
   assert(!run("printf partial; sleep .06; printf ' line\\n'"));
   finish();
   assert(strstr(local, "partial line"));
   assert(!run("-o printf '/quit\\n'; printf '/bad-command\\n' >&2"));
   finish();
   assert(chat_lines == 1 && !strcmp(plain, "/quit") && !parser_lines && strstr(local, "stderr] /bad-command"));
   assert(!run("-e printf '/rxroute L\\n'"));
   finish();
   assert(parser_lines == 1 && !strcmp(parsed, "/rxroute L"));
   assert(!run("-e head -c 20000 /dev/zero | tr '\\0' x; printf '\\n/valid\\n'"));
   finish();
   assert(parser_lines == 2 && !strcmp(parsed, "/valid") && strstr(local, "exceeding 16 KiB"));
   assert(!run("-e printf 'bad\\0command\\n'"));
   finish();
   assert(parser_lines == 2);
   assert(run("-o -e printf anything"));
   assert(!run("-o sleep .06; printf 'must not forward\\n'"));
   current = &second;
   finish();
   assert(chat_lines == 1);
   current = &first;

   for (unsigned i = 0 ; i < EXEC_JOBS_MAX ; i++) {
      assert(!run("sleep 10"));
   }

   assert(run("sleep 10"));
   assert(!run("-stop"));
   finish();
   assert(!run("sleep 10"));
   assert(!run("sleep 10"));
   char stop[48];
   snprintf(stop, sizeof(stop), "-stop %u", ((exec_job_t *) exec_jobs->data)->id);
   assert(!run(stop));
   uint64_t deadline = mono_us() + 1000000;
   while (g_list_length(exec_jobs) == 2 && mono_us() < deadline) {
      g_main_context_iteration(NULL, FALSE);
      g_usleep(1000);
   }
   assert(g_list_length(exec_jobs) == 1);
   assert(!run("-stop all"));
   finish();
   assert(!run("sleep 10"));
   rrclient_exec_shutdown();
   assert(!exec_jobs && !exec_source);
   assert(dispatches);
   event_shutdown();
   puts("PASS: async exec quoting, partial/oversized/binary lines, explicit forwarding modes, stderr isolation, session binding, job limits and shutdown");
}
