#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

time_t now = 100;
bool dying, restarting;
static ssize_t next_result;
static int next_error;
static char sent[4096];
static size_t sent_length;
ssize_t rr_test_send(int fd, const void *data, size_t length, int flags) {
   assert(fd == 0);
   assert(flags & MSG_DONTWAIT);
   assert(flags & MSG_NOSIGNAL);

   if (next_result < 0) {
      errno = next_error;

      return -1;
   }
   size_t amount = (size_t)next_result < length ? (size_t)next_result : length;
   assert(sent_length + amount < sizeof(sent));
   memcpy(sent + sent_length, data, amount);
   sent_length += amount;

   return (ssize_t) amount;
}
int main(void) {
   rrconn_t conn = {
      .fd = 0, .connected = now
   };
   /* A partial send must preserve a following incomplete fragment too. */
   snprintf(conn.sendq, sizeof(conn.sendq), "PING :one\r\npartial");
   next_result = 3;
   assert(irc_send(&conn, "tail"));
   assert(!strcmp(conn.sendq, "G :one\r\npartialtail\r\n"));
   next_result = -1;
   next_error = EINTR;
   assert(irc_send(&conn, "PING :two"));
   assert(!strcmp(conn.sendq, "G :one\r\npartialtail\r\nPING :two\r\n"));
   next_error = EAGAIN;
   assert(irc_send(&conn, "PING :three"));
   next_result = 4096;
   assert(irc_send(&conn, "PING :four"));
   sent[sent_length] = '\0';
   assert(!strcmp(sent, "PING :one\r\npartialtail\r\nPING :two\r\nPING :three\r\nPING :four\r\n"));
   assert(!conn.sendq[0] && conn.fd == 0);
   char oversized[IRC_MSGLEN + 1];
   memset(oversized, 'a', sizeof(oversized) - 1);
   oversized[sizeof(oversized) - 1] = '\0';
   assert(!irc_send(&conn, "%s", oversized));
   assert(!irc_send(&conn, "PING\nQUIT"));
   assert(irc_send(&conn, "%.*s", 510, oversized));
   assert(!irc_send(&conn, "%.*s", 511, oversized));
   assert(!irc_init());
   irc_process_message(&conn, ":server 005 nick LINELEN=80 :supported");
   assert(irc_send(&conn, "%.*s", 78, oversized));
   assert(!irc_send(&conn, "%.*s", 79, oversized));
   irc_shutdown();
   assert(!conn.sendq[0]);
   puts("PASS: IRC partial send preservation, EINTR/EAGAIN, fd zero and output validation");

   return 0;
}
