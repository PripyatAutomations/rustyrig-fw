// http.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we deal with http requests using mongoose
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/server.events.h>
#include <librrprotocol/transport.h>

extern time_t now;

rrconn_t *http_find_client_by_token(const char *token) {
   if (!token) {
      return NULL;
   }
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
      if (cptr->token[0] == '\0') {
         continue;
      }

      if (memcmp(cptr->token, token, strlen(cptr->token)) == 0) {
         Log(LOG_CRAZY, "http.client", "find_client_by_token |%s| returning index %i: %p |%s|", token, i, cptr, (*cptr->chatname ? cptr->chatname :
            "<UNAUTHENTICATED>"));

         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_CRAZY, "http.client", "find client: no matches for token |%s|!", token);

   return NULL;
}

rrconn_t *http_find_client_by_guest_id(int gid) {
   rrconn_t *cptr = http_client_list;
   int i = 0;

   // this filters out invalid calls
   if (gid <= 1) {
      Log(LOG_WARN, "http", "find_client_by_guestid: gid %d isn't valid", gid);

      return NULL;
   }
   while (cptr) {
      if (cptr->guest_id == gid) {
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   return NULL;
}

rrconn_t *http_find_client_by_name(const char *name) {
   rrconn_t *cptr = http_client_list;
   int i = 0;

   if (!name) {
      return NULL;
   }
   // Trailing spaces (e.g. from tab-completion) should never cause a
   // lookup to fail. Trim and use a local copy for the match.
   char namebuf[HTTP_USER_LEN + 1];
   snprintf(namebuf, sizeof(namebuf), "%s", name);
   char *nm = namebuf;
   while (*nm == ' ' || *nm == '\t') {
      nm++;
   }
   char *end = nm + strlen(nm);
   while (end > nm && (end[-1] == ' ' || end[-1] == '\t')) {
      *--end = '\0';
   }
   while (cptr) {
      Log(LOG_CRAZY, "http.client", "find client by name: i: %d user:<%p> chatname: %s", i, cptr->user, cptr->chatname);

      // incomplete entry
      if (!cptr->user || (cptr->chatname[0] == '\0')) {
         cptr = cptr->next;
         continue;
      }

      // match?
      if (strcasecmp(cptr->chatname, nm) == 0) {
         Log(LOG_CRAZY, "http.client", "find client by name |%s| found match at index %d: <%p> |%s|", name, i, cptr, (*cptr->chatname ? cptr->chatname :
            "<UNAUTHENTICATED>"));

         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_DEBUG, "http.client", "find client by name found no results for %s, index was %d", name, i);

   return NULL;
}

void http_client_free_resources(rrconn_t *cptr) {
   if (!cptr) {
      return;
   }
   event_emit("protocol.object.close", cptr, "");
   free(cptr->user_agent);
   cptr->user_agent = NULL;
   free(cptr->cli_version);
   cptr->cli_version = NULL;
}

bool http_client_set_user_agent(rrconn_t *cptr, const char *ua, size_t len) {
   if (!cptr || !ua) {
      return false;
   }
   size_t copy_len = len < HTTP_UA_LEN ? len : HTTP_UA_LEN;
   char *copy = malloc(copy_len + 1);

   if (!copy) {
      return false;
   }
   memcpy(copy, ua, copy_len);
   copy[copy_len] = '\0';
   free(cptr->user_agent);
   cptr->user_agent = copy;

   return true;
}

void http_dump_clients(void) {
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
#if     defined(USE_MONGOOSE)
      Log(LOG_DEBUG, "http", " => %d at <%p> %sactive %swebsocket, conn: <%p>, next: <%p> ", i, cptr, (cptr->active ? "" : "in"), (cptr->is_ws ? "" : "NOT "),
         cptr->conn, cptr->next);
#endif // defined(USE_MONGOOSE)
      i++;
      cptr = cptr->next;
   }
}

// Add a new client to the client list (HTTP or WebSocket)
rrconn_t *http_add_native_client(rr_transport_t *transport, const char *ip, int port) {
   if (!transport || http_count_connections() >= HTTP_MAX_SESSIONS) {
      return NULL;
   }
   rrconn_t *client = calloc(1, sizeof(*client));

   if (!client) {
      return NULL;
   }
   auth_generate_nonce(client->token, sizeof(client->token));
   auth_generate_nonce(client->nonce, sizeof(client->nonce));
   client->connected = client->last_heard = now;
   client->active = client->is_ws = true;
   client->native_transport = transport;
   snprintf(client->user_ip, sizeof(client->user_ip), "%s", ip ? ip : "");
   client->user_port = port;
   client->next = http_client_list;
   http_client_list = client;

   return client;
}

void http_remove_native_client(rrconn_t *client) {
   if (!client) {
      return;
   }
   rrconn_t **slot = &http_client_list;
   while (*slot && *slot != client) {
      slot = &(*slot)->next;
   }

   if (*slot != client) {
      return;
   }
   event_emit("protocol.session.closed", client, NULL);
   *slot = client->next;
   client->active = false;
   client->native_transport = NULL;

   if (client->user && client->authenticated && client->is_ws && client->user->sessions > 0) {
      client->user->sessions--;
   }
   http_client_free_resources(client);
   memset(client, 0, sizeof(*client));
   free(client);
}

#ifdef  USE_MONGOOSE
rrconn_t *http_add_client(struct mg_connection *c, bool is_ws) {
   rrconn_t *cptr = (rrconn_t *) malloc(sizeof(rrconn_t));

   if (!cptr) {
      fprintf(stderr, "OOM in http_add_client\n");

      return NULL;
   }
   memset(cptr, 0, sizeof(rrconn_t));

   // create some randomness for login hashing and session
   auth_generate_nonce(cptr->token, sizeof(cptr->token));
   auth_generate_nonce(cptr->nonce, sizeof(cptr->nonce));
   Log(LOG_CRAZY, "http", "add_client: generated session token and nonce for cptr:<%p>", cptr);
   cptr->connected = now;
   cptr->authenticated = false;
   cptr->active = true;
   cptr->conn = c;
   cptr->is_ws = is_ws;

#ifdef  USE_MONGOOSE
   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;

   if (c->rem.is_ip6) {
      inet_ntop(AF_INET6, c->rem.addr.ip6, ip, sizeof(ip));
   } else {
      inet_ntop(AF_INET, &c->rem.addr.ip4, ip, sizeof(ip));
   }

   // save the user's IP
   snprintf(cptr->user_ip, sizeof(cptr->user_ip), "%s", ip);
   cptr->user_port = port;
#endif

   // Add to the top of the list
   cptr->next = http_client_list;
   http_client_list = cptr;

   Log(LOG_DEBUG, "http", "Added new client at cptr:<%p> (%d clients and %d sessions total now)", cptr, http_count_connections(), http_count_clients());

   return cptr;
}

rrconn_t *http_find_client_by_c(struct mg_connection *c) {
   if (!c) {
      return NULL;
   }
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
      if (cptr->conn == c) {
//         Log( LOG_CRAZY, "http.client", "find_client_by_c <%p> returning index %i: %p
// |%s|",
//              c, i, cptr, (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>") );
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
//   Log(LOG_CRAZY, "http.client", "find_client_by_c <%p> no matches!", c);
   return NULL;
}

// Remove a client (WebSocket or HTTP) from the list
static void http_release_client(struct mg_connection *c, bool close_socket) {
   if (!c) {
      Log(LOG_CRIT, "http", "http_remove_client passed NULL mg_conn?!");

      return;
   }
   rrconn_t *prev = NULL;
   rrconn_t *current = http_client_list;

   // Count BEFORE unlinking, so the log reports the true state including
   // the client being removed
   int conns = http_count_connections();
   int users = http_count_clients();

   while (current) {
      if (current->conn == c) {
         event_emit("protocol.session.closed", current, NULL);

         // Found the client to remove, mark it dead
         if (close_socket) {
            mg_ws_send(c, NULL, 0, WEBSOCKET_OP_CLOSE);
            current->conn->is_closing = 1;
         }
         current->active = false;

         if (!prev) {
            http_client_list = current->next;
         } else {
            prev->next = current->next;
         }

         if (current->user) {
            if (current->authenticated && current->is_ws && current->user->sessions > 0) {
               current->user->sessions--;
            }

            if (current->user->sessions < 0) {
               Log(LOG_WARN, "http", "Client at cptr:<%p> has %d sessions??", current, current->user->sessions);
               current->user->sessions = 0;
            }
         }
         Log(LOG_CRAZY, "http", "Removed client at cptr:<%p> with mgconn:<%p> (%d connections / %d users remain)", current, c, conns - 1, (current->user &&
            current->authenticated ? users - 1 : users));
         http_client_free_resources(current);
         memset(current, 0, sizeof(rrconn_t));
         free(current);

         return;
      }
      prev = current;
      current = current->next;
   }
   Log(LOG_CRAZY, "http", "http_remove_client: no client found for conn:<%p>", c);
}
#endif // USE_MONGOOSE

// Counts only websocket clients that are logged in
int http_count_clients(void) {
   int c = 0;
   rrconn_t *cptr = http_client_list;
   while (cptr) {
      if (cptr->authenticated && cptr->is_ws) {
         c++;
      }
      cptr = cptr->next;
   }
   return c;
}

// Counts ALL websocket clients
int http_count_connections(void) {
   int c = 0;
   rrconn_t *cptr = http_client_list;
   while (cptr) {
      c++;
      cptr = cptr->next;
   }
   return c;
}

// Returns the user actively PTTing
rrconn_t *whos_talking(void) {
   rrconn_t *cptr = http_client_list;

   while (cptr) {
      if (cptr->authenticated && cptr->is_ptt) {
         Log(LOG_CRAZY, "http", "whos_talking: returning cptr:<%p> - %s", cptr, cptr->chatname);

         return cptr;
      }
      cptr = cptr->next;
   }
   rr_server_talker_t query = {
      0
   };
   event_emit_binary(RR_SERVER_TALKER_EVENT, NULL, &query, sizeof(query));

   return query.client;
}

#ifdef USE_MONGOOSE
/* Transfer an unauthenticated HTTP socket to an application-owned protocol. */
void http_detach_client(struct mg_connection *c) {
   http_release_client(c, false);
}

void http_remove_client(struct mg_connection *c) {
   http_release_client(c, true);
}
#endif /* USE_MONGOOSE */

/* Applications own consent; missing policy listeners fail closed. */
bool ws_supervision_allowed(rrconn_t *client, const char *room, bool request) {
   if (!client || !client->authenticated || !client->user) {
      return false;
   }

   Log(LOG_DEBUG, "auth", "Supervision check: user=%s uid=%d room=%s noob=%d", client->user->name, client->user->uid, room ? room : "-", has_priv(client->user->
      uid, "noob"));

   if (!has_priv(client->user->uid, "noob")) {
      return true;
   }
   rr_server_supervision_t query = {
      .room = room, .request = request
   };
   event_emit_binary(RR_SERVER_SUPERVISION_EVENT, client, &query, sizeof(query));

   return query.allowed;
}
