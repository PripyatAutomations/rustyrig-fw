#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/irc.h>

extern rrconn_t *irc_cli_connect(server_cfg_t *srv);
time_t now = 100;
bool dying, restarting;

int main(void) {
   assert(!irc_cli_connect(NULL));
   int listener = socket(AF_INET, SOCK_STREAM, 0);
   assert(listener >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(listener, (struct sockaddr *)&address, sizeof(address)));
   assert(!listen(listener, 1));
   socklen_t len = sizeof(address);
   assert(!getsockname(listener, (struct sockaddr *)&address, &len));
   server_cfg_t srv = {
      0
   };
   snprintf(srv.host, sizeof(srv.host), "127.0.0.1");
   srv.port = ntohs(address.sin_port);
   rrconn_t *client = irc_cli_connect(&srv);
   assert(client && client->fd >= 0 && client->server == &srv);
   assert(fcntl(client->fd, F_GETFL) & O_NONBLOCK);
   assert(!strcmp(client->nick, "nonick") && !client->sent_login);
   int peer = accept(listener, NULL, NULL);
   assert(peer >= 0);
   irc_io_poll(client);
   assert(client->connected == now && client->sent_login);
   char registration[256];
   ssize_t received = recv(peer, registration, sizeof(registration) - 1, MSG_DONTWAIT);
   assert(received > 0);
   registration[received] = '\0';
   assert(strstr(registration, "NICK nonick\r\nUSER nonick 0 * :nonick\r\n"));
   srv.tls = true;
   assert(!irc_cli_connect(&srv));
   srv.tls = false;
   srv.port = 0;
   assert(!irc_cli_connect(&srv));
   close(peer);
   close(client->fd);
   free(client);
   close(listener);
   puts("PASS: IRC loopback connection, nonblocking socket and initial state");

   return 0;
}
