//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <rrclient/irc.dcc.h>
#include <rrclient/irc.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include <lwip/inet.h>
#else
#include <arpa/inet.h>
#endif
static bool nick_valid(const char *nick) {
   if (!nick || !*nick || strlen(nick) > NICKLEN) {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)nick ; *p ; p++) {
      if (*p <= ' ' || *p >= 127 || strchr(",:#!&", *p)) {
         return false;
      }
   }

   return true;
}
bool irc_dcc_decode(const char *payload, irc_dcc_session_t *offer) {
   rustyirc_dcc_offer_t wire;
   if (!offer || !rustyirc_dcc_decode(payload, &wire)) return false;
   memset(offer, 0, sizeof(*offer));
   offer->type = wire.type; offer->size_known = wire.size_known;
   offer->port = wire.port; offer->size = wire.size;
   memcpy(offer->filename, wire.filename, sizeof(offer->filename));
   memcpy(offer->ip, wire.ip, sizeof(offer->ip));
   return true;
}
char *irc_dcc_encode(const irc_dcc_session_t *offer) {
   if (!offer) return NULL;
   rustyirc_dcc_offer_t wire = { .type = offer->type, .size_known = offer->size_known,
      .port = offer->port, .size = offer->size };
   memcpy(wire.filename, offer->filename, sizeof(wire.filename));
   memcpy(wire.ip, offer->ip, sizeof(wire.ip));
   return rustyirc_dcc_encode(&wire);
}
#ifdef HOST_POSIX
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <unistd.h>
#define DCC_BLOCK 4096
#define DCC_TIMEOUT UINT64_C(300000000)
struct dcc_session {
   irc_dcc_session_t info;
   int socket, listener, file;
   uint64_t deadline;
   uint8_t tx[DCC_BLOCK], ack[4];
   size_t tx_size, tx_offset, ack_used;
   char line[DCC_BLOCK];
   size_t line_size;
};
static struct dcc_session sessions[IRC_DCC_MAX_SESSIONS];
static unsigned next_id;
static void notice(const irc_dcc_session_t *info, const char *status, const char *text) {
   dict *d = dict_new();

   if (!d) {
      return;
   }
   dict_add_uint(d, "dcc.id", info->id);
   dict_add(d, "dcc.type", info->type == IRC_DCC_CHAT ? "CHAT" : "SEND");
   dict_add(d, "dcc.nick", info->nick);
   dict_add(d, "dcc.filename", info->filename);
   dict_add(d, "dcc.status", status);
   dict_add(d, "dcc.text", text ? text : "");
   dict_add_bool(d, "dcc.outgoing", info->outgoing);
   dict_add_bool(d, "dcc.size-known", info->size_known);
   dict_add_ullong(d, "dcc.size", info->size);
   dict_add_ullong(d, "dcc.transferred", info->transferred);
   event_emit_dict(IRC_DCC_EVENT, info->control, d);
   dict_free(d);
}
static void finish(struct dcc_session *s, const char *status, const char *reason) {
   irc_dcc_session_t info = s->info;

   if (s->socket >= 0) {
      close(s->socket);
   }

   if (s->listener >= 0) {
      close(s->listener);
   }

   if (s->file >= 0) {
      close(s->file);
   }
   memset(s, 0, sizeof(*s));

   if (info.id && status) {
      notice(&info, status, reason);
   }
}
static struct dcc_session *allocate(rrconn_t *client) {
   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      struct dcc_session *s = &sessions[i];

      if (s->info.id) {
         continue;
      }
      memset(s, 0, sizeof(*s));
      s->socket = s->listener = s->file = -1;
      bool duplicate;
      do {
         if (!++next_id) {
            ++next_id;
         }
         duplicate = false;

         for (unsigned j = 0 ; j < IRC_DCC_MAX_SESSIONS ; j++) {
            if (sessions[j].info.id == next_id) {
               duplicate = true;
            }
         }
      } while (duplicate);
      s->info.id = next_id;
      s->info.control = client;
      s->deadline = mono_us() + DCC_TIMEOUT;

      return s;
   }

   return NULL;
}
static struct dcc_session *find(rrconn_t *client, unsigned id) {
   if (!id) {
      return NULL;
   }

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      if (sessions[i].info.id == id && sessions[i].info.control == client) {
         return &sessions[i];
      }
   }

   return NULL;
}
static bool connected(rrconn_t *client) {
   return client && !client->is_ws && client->authenticated;
}
static int new_socket(void) {
   int fd = socket(AF_INET, SOCK_STREAM, 0);

   if (fd >= 0 && (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)) {
      close(fd);

      return -1;
   }

   return fd;
}
bool irc_dcc_advertised_ip(rrconn_t *client, const char *configured, char ip[64]) {
   if (!client || !ip) {
      return false;
   }

   if (configured && *configured) {
      snprintf(ip, 64, "%s", configured);
   } else {
#ifdef USE_MONGOOSE

      if (client->conn) {
         if (client->conn->loc.is_ip6 || !inet_ntop(AF_INET, &client->conn->loc.addr.ip4, ip, 64)) {
            return false;
         }
      } else
#endif
      {
         struct sockaddr_in address;
         socklen_t length = sizeof(address);

         if (client->fd < 0 || getsockname(client->fd, (struct sockaddr *)&address, &length) || address.sin_family != AF_INET ||
            !inet_ntop(AF_INET, &address.sin_addr, ip, 64)) {
            return false;
         }
      }
   }
   struct in_addr address;

   return inet_pton(AF_INET, ip, &address) == 1 && address.s_addr != INADDR_ANY && address.s_addr != INADDR_BROADCAST;
}
static bool listen_offer(struct dcc_session *s, const irc_dcc_options_t *options) {
   if (!options || !options->port_min || options->port_max > 65535 || options->port_min > options->port_max ||
      !irc_dcc_advertised_ip(s->info.control, options->host, s->info.ip)) {
      return false;
   }
   struct sockaddr_in address = {
      .sin_family = AF_INET
   };

   if (inet_pton(AF_INET, options->bind ? options->bind : "0.0.0.0", &address.sin_addr) != 1) {
      return false;
   }
   uint32_t random;

   if (getrandom(&random, sizeof(random), 0) != sizeof(random)) {
      return false;
   }
   unsigned count = options->port_max - options->port_min + 1, start = random % count;

   for (unsigned i = 0 ; i < count ; i++) {
      unsigned port = options->port_min + (start + i) % count;
      int fd = new_socket();

      if (fd < 0) {
         return false;
      }
      address.sin_port = htons(port);

      if (!bind(fd, (struct sockaddr *)&address, sizeof(address)) && !listen(fd, 1)) {
         s->listener = fd;
         s->info.port = port;
         s->info.outgoing = true;
         s->info.state = IRC_DCC_LISTENING;
         char *payload = irc_dcc_encode(&s->info);
         bool sent = payload && irc_send(s->info.control, "PRIVMSG %s :%s", s->info.nick, payload);
         free(payload);

         return sent;
      }
      close(fd);
   }

   return false;
}
unsigned irc_dcc_offer_chat(rrconn_t *client, const char *nick, const irc_dcc_options_t *options) {
   if (!connected(client) || !nick_valid(nick)) {
      return 0;
   }
   struct dcc_session *s = allocate(client);

   if (!s) {
      return 0;
   }
   s->info.type = IRC_DCC_CHAT;
   snprintf(s->info.nick, sizeof(s->info.nick), "%s", nick);

   if (!listen_offer(s, options)) {
      finish(s, NULL, NULL);

      return 0;
   }
   unsigned id = s->info.id;
   notice(&s->info, "offered", "");

   return id;
}
unsigned irc_dcc_offer_file(rrconn_t *client, const char *nick, const char *filename, int fd, const irc_dcc_options_t *options) {
   struct stat st;
   int flags = fcntl(fd, F_GETFL);

   if (!connected(client) || !nick_valid(nick) || !rustyirc_dcc_filename_valid(filename) || flags < 0 || (flags & O_ACCMODE) == O_WRONLY ||
      fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
      return 0;
   }
   struct dcc_session *s = allocate(client);

   if (!s) {
      return 0;
   }
   s->info.type = IRC_DCC_SEND;
   s->info.size_known = true;
   s->info.size = st.st_size;
   snprintf(s->info.nick, sizeof(s->info.nick), "%s", nick);
   snprintf(s->info.filename, sizeof(s->info.filename), "%s", filename);
   s->file = fcntl(fd, F_DUPFD_CLOEXEC, 0);

   if (s->file < 0 || !listen_offer(s, options)) {
      finish(s, NULL, NULL);

      return 0;
   }
   unsigned id = s->info.id;
   notice(&s->info, "offered", "");

   return id;
}
bool irc_dcc_message(rrconn_t *client, const irc_message_t *message) {
   if (!message || message->argc < 3 || !message->argv[2] || strncasecmp(message->argv[2], "\001DCC ", 5)) {
      return false;
   }

   if (!strncasecmp(message->argv[2], "\001DCC AUDIO", 10)) {
      return false;
   }

   /* Unsupported/malformed/public DCC never opens sockets or becomes chat text. */
   if (!connected(client) || !message->prefix || !message->argv[1] || strcasecmp(message->argv[1], client->nick) ||
      (strcasecmp(message->argv[0], "PRIVMSG") && strcasecmp(message->argv[0], "NOTICE"))) {
      return true;
   }
   irc_dcc_session_t offer;

   if (!irc_dcc_decode(message->argv[2], &offer)) {
      return true;
   }
   size_t n = strcspn(message->prefix, "!");

   if (!n || n > NICKLEN) {
      return true;
   }
   char nick[NICKLEN + 1];
   memcpy(nick, message->prefix, n);
   nick[n] = 0;

   if (!nick_valid(nick)) {
      return true;
   }
   struct dcc_session *s = allocate(client);

   if (!s) {
      return true;
   }
   unsigned id = s->info.id;
   s->info = offer;
   s->info.id = id;
   s->info.control = client;
   snprintf(s->info.nick, sizeof(s->info.nick), "%s", nick);
   s->info.state = IRC_DCC_PENDING;
   notice(&s->info, "offer", "");

   return true;
}
bool irc_dcc_accept(rrconn_t *client, unsigned id, int destination_fd) {
   struct dcc_session *s = find(client, id);

   if (!connected(client) || !s || s->info.state != IRC_DCC_PENDING || (uint64_t) mono_us() >= s->deadline) {
      return false;
   }
   int file = -1;

   if (s->info.type == IRC_DCC_SEND) {
      struct stat st;
      int flags = fcntl(destination_fd, F_GETFL);

      if (flags < 0 || (flags & O_ACCMODE) == O_RDONLY || (flags & O_APPEND) || fstat(destination_fd, &st) || !S_ISREG(st.st_mode) || st.st_size != 0) {
         return false;
      }
      file = fcntl(destination_fd, F_DUPFD_CLOEXEC, 0);

      if (file < 0) {
         return false;
      }
   }
   int fd = new_socket();

   if (fd < 0) {
      if (file >= 0) {
         close(file);
      }

      return false;
   }
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_port = htons(s->info.port)
   };
   inet_pton(AF_INET, s->info.ip, &address.sin_addr);

   if (connect(fd, (struct sockaddr *)&address, sizeof(address)) && errno != EINPROGRESS) {
      close(fd);

      if (file >= 0) {
         close(file);
      }

      return false;
   }
   s->file = file;
   s->socket = fd;
   s->info.state = IRC_DCC_CONNECTING;
   s->deadline = mono_us() + DCC_TIMEOUT;

   return true;
}
bool irc_dcc_chat_send(rrconn_t *client, unsigned id, const char *text) {
   struct dcc_session *s = find(client, id);
   size_t length = text ? strlen(text) : 0;

   if (!connected(client) || !s || s->info.type != IRC_DCC_CHAT || s->info.state != IRC_DCC_ACTIVE || !length ||
      length > DCC_BLOCK - 2 || strchr(text, '\r') || strchr(text, '\n') || s->tx_size) {
      return false;
   }
   memcpy(s->tx, text, length);
   s->tx[length] = '\r';
   s->tx[length + 1] = '\n';
   s->tx_size = length + 2;
   s->tx_offset = 0;

   return true;
}
bool irc_dcc_close(rrconn_t *client, unsigned id) {
   struct dcc_session *s = find(client, id);

   if (!s) {
      return false;
   }
   finish(s, "closed", "closed locally");

   return true;
}
bool irc_dcc_session_get(unsigned index, irc_dcc_session_t *session) {
   if (!session) {
      return false;
   }

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      if (!sessions[i].info.id) {
         continue;
      }

      if (index--) {
         continue;
      }
      *session = sessions[i].info;

      return true;
   }

   return false;
}
static bool again(void) {
   return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
static bool flush(struct dcc_session *s) {
   while (s->tx_offset < s->tx_size) {
      ssize_t n = send(s->socket, s->tx + s->tx_offset, s->tx_size - s->tx_offset, MSG_NOSIGNAL);

      if (n < 0 && again()) {
         return true;
      }

      if (n <= 0) {
         finish(s, "error", "TCP write failed");

         return false;
      }
      s->tx_offset += n;

      if (s->info.type == IRC_DCC_SEND && s->info.outgoing) {
         s->info.transferred += n;
      }
      s->deadline = s->info.type == IRC_DCC_CHAT ? 0 : mono_us() + DCC_TIMEOUT;
   }
   s->tx_size = s->tx_offset = 0;

   return true;
}
static bool write_file(struct dcc_session *s, const uint8_t *data, size_t size) {
   size_t offset = 0;
   while (offset < size) {
      ssize_t n = pwrite(s->file, data + offset, size - offset, s->info.transferred + offset);

      if (n < 0 && errno == EINTR) {
         continue;
      }

      if (n <= 0) {
         return false;
      }
      offset += n;
   }
   s->info.transferred += size;

   return true;
}
static void receive_data(struct dcc_session *s) {
   for (unsigned budget = 0 ; budget < 16 && s->info.id ; budget++) {
      if (s->info.type == IRC_DCC_SEND && !s->info.outgoing && s->tx_size) {
         break;
      }
      uint8_t data[DCC_BLOCK];
      size_t maximum = sizeof(data);

      if (s->info.type == IRC_DCC_SEND && !s->info.outgoing && s->info.size_known) {
         uint64_t remaining = s->info.size - s->info.transferred;

         if (!remaining) {
            break;
         }

         if (remaining < maximum) {
            maximum = remaining;
         }
      }
      ssize_t n = recv(s->socket, data, maximum, 0);

      if (n < 0 && again()) {
         break;
      }

      if (n <= 0) {
         bool complete = n == 0 && s->info.type == IRC_DCC_SEND && !s->info.outgoing && !s->info.size_known;
         finish(s, n < 0 ? "error" : complete ? "complete" : s->info.type == IRC_DCC_CHAT ? "closed" : "error", n < 0 ? "TCP read failed" : complete ? "" :
            "peer closed connection");

         return;
      }

      if (s->info.type == IRC_DCC_SEND) {
         s->deadline = mono_us() + DCC_TIMEOUT;
      }

      if (s->info.type == IRC_DCC_CHAT) {
         for (ssize_t i = 0 ; i < n ; i++) {
            if (!data[i] || s->line_size >= sizeof(s->line) - 1) {
               finish(s, "error", "invalid or overlong chat line");

               return;
            }

            if (data[i] == '\n') {
               if (s->line_size && s->line[s->line_size - 1] == '\r') {
                  s->line_size--;
               }
               s->line[s->line_size] = 0;
               dict *d = dict_new();

               if (d) {
                  dict_add_uint(d, "dcc.id", s->info.id);
                  dict_add(d, "dcc.nick", s->info.nick);
                  dict_add(d, "dcc.text", s->line);
                  unsigned id = s->info.id;
                  event_emit_dict(IRC_DCC_CHAT_EVENT, s->info.control, d);
                  dict_free(d);

                  if (s->info.id != id) {
                     return;
                  }
               }
               s->line_size = 0;

               if (!s->info.id) {
                  return;
               }
            } else {
               s->line[s->line_size++] = data[i];
            }
         }
      } else if (s->info.outgoing) {
         for (ssize_t i = 0 ; i < n ; i++) {
            s->ack[s->ack_used++] = data[i];

            if (s->ack_used < 4) {
               continue;
            }
            uint32_t network;
            memcpy(&network, s->ack, 4);
            s->ack_used = 0;
            uint64_t ack = (s->info.acknowledged & ~UINT64_C(0xffffffff)) | ntohl(network);

            if (ack < s->info.acknowledged) {
               ack += UINT64_C(0x100000000);
            }

            if (ack > s->info.transferred) {
               finish(s, "error", "invalid file acknowledgement");

               return;
            }
            s->info.acknowledged = ack;

            if (ack == s->info.size && s->info.transferred == s->info.size && !s->tx_size) {
               finish(s, "complete", "");

               return;
            }
         }
      } else {
         if (!write_file(s, data, n)) {
            finish(s, "error", "file write failed");

            return;
         }
         uint32_t ack = htonl((uint32_t) s->info.transferred);
         memcpy(s->tx, &ack, 4);
         s->tx_size = 4;
         s->tx_offset = 0;

         if (!flush(s)) {
            return;
         }
      }
   }
}
void irc_dcc_poll(void) {
   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      struct dcc_session *s = &sessions[i];

      if (!s->info.id) {
         continue;
      }
      unsigned original_id = s->info.id;
      uint64_t now = mono_us();

      if (!connected(s->info.control)) {
         finish(s, "closed", "IRC session ended");
         continue;
      }

      if (s->deadline && now >= s->deadline) {
         finish(s, "error", "DCC offer or transfer timed out");
         continue;
      }

      if (s->info.state == IRC_DCC_PENDING) {
         continue;
      }

      if (s->info.state == IRC_DCC_LISTENING) {
         int fd = accept(s->listener, NULL, NULL);

         if (fd < 0) {
            if (!again()) {
               finish(s, "error", "accept failed");
            }
            continue;
         }

         if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
            close(fd);
            finish(s, "error", "socket setup failed");
            continue;
         }
         close(s->listener);
         s->listener = -1;
         s->socket = fd;
         s->info.state = IRC_DCC_ACTIVE;
         s->deadline = s->info.type == IRC_DCC_CHAT ? 0 : now + DCC_TIMEOUT;
         notice(&s->info, "active", "");
      } else if (s->info.state == IRC_DCC_CONNECTING) {
         struct pollfd ready = {
            .fd = s->socket, .events = POLLOUT
         };

         if (poll(&ready, 1, 0) <= 0) {
            continue;
         }
         int error;
         socklen_t size = sizeof(error);

         if (getsockopt(s->socket, SOL_SOCKET, SO_ERROR, &error, &size) || error) {
            finish(s, "error", "connect failed");
            continue;
         }
         s->info.state = IRC_DCC_ACTIVE;
         s->deadline = s->info.type == IRC_DCC_CHAT ? 0 : now + DCC_TIMEOUT;
         notice(&s->info, "active", "");
      }

      if (s->info.id != original_id || !flush(s)) {
         continue;
      }

      if (s->info.type == IRC_DCC_SEND && s->info.outgoing && !s->tx_size && s->info.acknowledged == s->info.transferred && s->info.transferred < s->info.size)
      {
         uint64_t remaining = s->info.size - s->info.transferred;
         size_t amount = remaining < sizeof(s->tx) ? remaining : sizeof(s->tx);
         ssize_t n = pread(s->file, s->tx, amount, s->info.transferred);

         if (n < 0 && errno == EINTR) {
            continue;
         }

         if (n <= 0) {
            finish(s, "error", "source file read failed or file shrank");
            continue;
         }
         s->tx_size = n;
         s->tx_offset = 0;

         if (!flush(s)) {
            continue;
         }
      }

      if (s->info.type == IRC_DCC_SEND && s->info.size_known && !s->tx_size &&
         s->info.transferred == s->info.size && (!s->info.outgoing || s->info.acknowledged == s->info.size)) {
         finish(s, "complete", "");
         continue;
      }
      receive_data(s);
   }
}
static void disconnected(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event;
   (void)data;
   (void)user;

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      if (sessions[i].info.id && sessions[i].info.control == client) {
         finish(&sessions[i], "closed", "IRC session ended");
      }
   }
}
static void offer_event(const char *event, const char *data, rrconn_t *client, void *user) {
   (void)event; (void)user;
   dict *d = json2dict(data);
   if (!d) return;
   char *args[] = { (char *)dict_get(d, "msg.arg0", ""),
      (char *)dict_get(d, "msg.arg1", ""), (char *)dict_get(d, "msg.arg2", ""), NULL };
   irc_message_t message = { .argc = 3, .argv = args, .prefix = (char *)dict_get(d, "msg.prefix", "") };
   irc_dcc_message(client, &message);
   dict_free(d);
}
void irc_dcc_init(void) {
   event_off("irc.dcc.offer", offer_event, NULL);
   event_on("irc.dcc.offer", offer_event, NULL);
   event_off("irc.disconnected", disconnected, NULL);
   event_on("irc.disconnected", disconnected, NULL);
   event_off("disconnected", disconnected, NULL);
   event_on("disconnected", disconnected, NULL);
   event_off("disconnect", disconnected, NULL);
   event_on("disconnect", disconnected, NULL);
}
void irc_dcc_shutdown(void) {
   event_off("irc.dcc.offer", offer_event, NULL);
   event_off("irc.disconnected", disconnected, NULL);
   event_off("disconnected", disconnected, NULL);
   event_off("disconnect", disconnected, NULL);

   for (unsigned i = 0 ; i < IRC_DCC_MAX_SESSIONS ; i++) {
      if (sessions[i].info.id) {
         finish(&sessions[i], "closed", "shutdown");
      }
   }
}
#else
bool irc_dcc_advertised_ip(rrconn_t *client, const char *configured, char ip[64]) {
   (void)client;
   (void)configured;
   (void)ip;

   return false;
}
unsigned irc_dcc_offer_chat(rrconn_t *client, const char *nick, const irc_dcc_options_t *options) {
   (void)client;
   (void)nick;
   (void)options;

   return 0;
}
unsigned irc_dcc_offer_file(rrconn_t *client, const char *nick, const char *filename, int fd, const irc_dcc_options_t *options) {
   (void)client;
   (void)nick;
   (void)filename;
   (void)fd;
   (void)options;

   return 0;
}
bool irc_dcc_message(rrconn_t *client, const irc_message_t *message) {
   (void)client;
   (void)message;

   return false;
}
bool irc_dcc_accept(rrconn_t *client, unsigned id, int destination_fd) {
   (void)client;
   (void)id;
   (void)destination_fd;

   return false;
}
bool irc_dcc_chat_send(rrconn_t *client, unsigned id, const char *text) {
   (void)client;
   (void)id;
   (void)text;

   return false;
}
bool irc_dcc_close(rrconn_t *client, unsigned id) {
   (void)client;
   (void)id;

   return false;
}
bool irc_dcc_session_get(unsigned index, irc_dcc_session_t *session) {
   (void)index;
   (void)session;

   return false;
}
void irc_dcc_init(void) {
}
void irc_dcc_shutdown(void) {
}
void irc_dcc_poll(void) {
}
#endif
