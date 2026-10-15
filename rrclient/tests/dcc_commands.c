#include <assert.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
extern char *expand_path(const char *path);
static char *test_expand_path(const char *path);
#define expand_path test_expand_path
#include <rrclient/dcc.c>
#undef expand_path
static char downloads[512];
static char *test_expand_path(const char *path) {
   return !strcmp(path, "~/Downloads/DCC") ? strdup(downloads) : expand_path(path);
}

rrconn_t *ws_conn;
static char output[32768], last_window[512];
static const char *active_window = "commands";
bool ui_print(const char *window, const char *format, ...) {
   assert(window);
   snprintf(last_window, sizeof(last_window), "%s", window);
   va_list args;
   va_start(args, format);
   size_t length = strlen(output);
   vsnprintf(output + length, sizeof(output) - length, format, args);
   va_end(args);

   return false;
}
void rrclient_connection_dispatch(rrconn_t *client, void (*callback) (void *), void *data) {
   (void)client;
   callback(data);
}
const char *ui_active_window_name(void) {
   return active_window;
}
const char *rrclient_connection_name(const rrconn_t *client) {
   (void)client;

   return "test-irc";
}
static void fresh_port(void) {
   int fd = socket(AF_INET, SOCK_STREAM, 0);
   assert(fd >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(fd, (struct sockaddr *)&address, sizeof(address)));
   socklen_t length = sizeof(address);
   assert(!getsockname(fd, (struct sockaddr *)&address, &length));
   close(fd);
   dict_add_int(cfg, "dcc.port-min", ntohs(address.sin_port));
   dict_add_int(cfg, "dcc.port-max", ntohs(address.sin_port));
}
static unsigned offer_to(rrconn_t *client, int fd, irc_dcc_type_t type) {
   char wire[1024], incoming[1200];
   ssize_t n = recv(fd, wire, sizeof(wire) - 1, 0);
   assert(n > 0);
   wire[n] = 0;
   char *end = strchr(wire, '\r');

   if (end) {
      *end = 0;
   }
   snprintf(incoming, sizeof(incoming), ":alice!user@host %s", wire);
   assert(!irc_process_message(client, incoming));
   irc_dcc_session_t session;

   for (unsigned i = 0 ; irc_dcc_session_get(i, &session) ; i++) {
      if (session.control == client && session.type == type && session.state == IRC_DCC_PENDING) {
         return session.id;
      }
   }

   assert(0);

   return 0;
}
int main(void) {
   cfg = dict_new();
   dict_add(cfg, "dcc.host", "127.0.0.1");
   event_init();
   assert(!irc_init());
   rrclient_dcc_register_events();
   int irc[2];
   assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, irc));
   rrconn_t alice = {
      .fd = irc[0], .connected = true, .authenticated = true
   }, bob = {
      .fd = -1, .connected = true, .authenticated = true
   };
   strcpy(alice.nick, "alice");
   strcpy(bob.nick, "bob");
   char directory[] = "/tmp/rr-dcc-cmd-XXXXXX";
   assert(mkdtemp(directory));
   snprintf(downloads, sizeof(downloads), "%s/Downloads/DCC", directory);
   char source[512], destination[512], existing[512], link[512];
   snprintf(source, sizeof(source), "%s/source file.bin", directory);
   snprintf(destination, sizeof(destination), "%s/received file.bin", directory);
   snprintf(existing, sizeof(existing), "%s/existing", directory);
   snprintf(link, sizeof(link), "%s/link", directory);
   const char data[] = "binary\0data\n";
   int fd = open(source, O_CREAT | O_WRONLY | O_EXCL, 0600);
   assert(fd >= 0 && write(fd, data, sizeof(data)) == sizeof(data));
   close(fd);
   fd = open(existing, O_CREAT | O_WRONLY | O_EXCL, 0600);
   assert(fd >= 0 && write(fd, "keep", 4) == 4);
   close(fd);
   assert(!symlink(existing, link));
   fresh_port();
   ws_conn = &alice;
   char split[512];
   snprintf(split, sizeof(split), "%s", source);
   char *space = strrchr(split, ' ');
   assert(space);
   *space++ = 0;
   char *send[] = {
      "dcc", "send", "bob", split, space
   };
   assert(!cmd_dcc(5, send));
   unsigned id = offer_to(&bob, irc[1], IRC_DCC_SEND);
   char identifier[16];
   snprintf(identifier, sizeof(identifier), "%u", id);
   assert(strstr(output, "SEND offer") && strstr(output, "/dcc get ") && strstr(output, "[optional-filename]"));
   char *accept[] = {
      "dcc", "send", "accept", identifier, existing
   };
   ws_conn = &bob;
   assert(cmd_dcc(5, accept));
   accept[4] = link;
   assert(cmd_dcc(5, accept));
   irc_dcc_session_t session;
   assert(irc_dcc_session_get(1, &session) && session.state == IRC_DCC_PENDING);
   accept[4] = destination;
   ws_conn = &alice;
   assert(cmd_dcc(5, accept) && access(destination, F_OK));
   ws_conn = &bob;
   char *get[] = {
      "DCC", "GET", identifier, destination
   };
   assert(!cmd_dcc(4, get));

   for (unsigned i = 0 ; i < 1000 && irc_dcc_session_get(0, &session) ; i++) {
      rrclient_dcc_poll();
      usleep(100);
   }

   assert(!irc_dcc_session_get(0, &session));
   char copied[sizeof(data)];
   fd = open(destination, O_RDONLY);
   assert(fd >= 0 && read(fd, copied, sizeof(copied)) == sizeof(copied) && !memcmp(copied, data, sizeof(data)));
   struct stat st;
   assert(!fstat(fd, &st) && !(st.st_mode & 077));
   close(fd);
   fd = open(existing, O_RDONLY);
   assert(fd >= 0 && read(fd, copied, 4) == 4 && !memcmp(copied, "keep", 4));
   close(fd);
   /* Default acceptance creates the directory and retains the offered name, including spaces. A repeated offer must not overwrite that download. */
   char downloaded[1024];
   snprintf(downloaded, sizeof(downloaded), "%s/source file.bin", downloads);
   assert(access(downloads, F_OK));

   for (unsigned attempt = 0 ; attempt < 3 ; attempt++) {
      fresh_port();
      ws_conn = &alice;
      assert(!cmd_dcc(5, send));
      id = offer_to(&bob, irc[1], IRC_DCC_SEND);
      snprintf(identifier, sizeof(identifier), "%u", id);
      ws_conn = &bob;

      if (attempt != 1) {
         if (attempt == 2) {
            get[3] = "renamed file.bin";
            snprintf(downloaded, sizeof(downloaded), "%s/renamed file.bin", downloads);
         }
         assert(!cmd_dcc(attempt == 0 ? 3 : 4, get));

         for (unsigned i = 0 ; i < 1000 && irc_dcc_session_get(0, &session) ; i++) {
            rrclient_dcc_poll();
            usleep(100);
         }

         assert(!irc_dcc_session_get(0, &session));
      } else {
         assert(cmd_dcc(3, get));
         assert(irc_dcc_close(&bob, id));
         assert(irc_dcc_session_get(0, &session));
         assert(irc_dcc_close(&alice, session.id));
      }
      fd = open(downloaded, O_RDONLY);
      assert(fd >= 0 && read(fd, copied, sizeof(copied)) == sizeof(copied) && !memcmp(copied, data, sizeof(data)));
      assert(!fstat(fd, &st) && !(st.st_mode & 077));
      close(fd);
   }

   unlink(downloaded);
   snprintf(downloaded, sizeof(downloaded), "%s/source file.bin", downloads);
   unlink(downloaded);
   rmdir(downloads);
   char downloads_parent[512];
   snprintf(downloads_parent, sizeof(downloads_parent), "%s/Downloads", directory);
   rmdir(downloads_parent);
   fresh_port();
   ws_conn = &alice;
   char *chat[] = {
      "dcc", "chat", "bob"
   };
   assert(!cmd_dcc(3, chat));
   id = offer_to(&bob, irc[1], IRC_DCC_CHAT);
   snprintf(identifier, sizeof(identifier), "%u", id);
   ws_conn = &bob;
   char *accept_chat[] = {
      "dcc", "chat", "accept", identifier
   };
   assert(!cmd_dcc(4, accept_chat));

   for (unsigned i = 0 ; i < 4 ; i++) {
      rrclient_dcc_poll();
   }

   char *msg[] = {
      "dcc", "chat", "msg", identifier, "message with spaces"
   };
   assert(!cmd_dcc(5, msg));
   active_window = "another-tab";

   for (unsigned i = 0 ; i < 4 ; i++) {
      rrclient_dcc_poll();
   }

   assert(strstr(output, "<bob> message with spaces") && !strcmp(last_window, "commands"));
   char *list[] = {
      "dcc"
   };
   output[0] = 0;
   assert(!cmd_dcc(1, list) && strstr(output, "CHAT") && !strstr(output, "No active"));
   char *close_chat[] = {
      "dcc", "chat", "close", identifier
   };
   assert(!cmd_dcc(4, close_chat));

   for (unsigned i = 0 ; i < 3 ; i++) {
      rrclient_dcc_poll();
   }

   assert(!irc_dcc_session_get(0, &session));
   irc_shutdown();
   event_shutdown();
   dict_free(cfg);
   close(irc[0]);
   close(irc[1]);
   unlink(link);
   unlink(existing);
   unlink(source);
   unlink(destination);
   rmdir(directory);
   puts("PASS: shared DCC commands, file paths with spaces, explicit acceptance, no overwrite/symlink, control-session scope, private modes and chat listing");
}
