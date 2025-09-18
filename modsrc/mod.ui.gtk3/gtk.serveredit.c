//
// rrclient/gtk.serveredit.c: Server editor
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/config.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include "../ext/libmongoose/mongoose.h"
#include <librustyaxe/logger.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/http.h>
#include <librustyaxe/posix.h>
#include "rrclient/auth.h"
#include "mod.ui.gtk3/gtk.core.h"
#include "rrclient/ws.h"

extern dict *cfg;

typedef enum {
   SERVER_WS,
   SERVER_WSS
} server_proto_t;

typedef struct serverlist {
   char *name;          // friendly name
   char *host;          // host from URL
   int port;            // parsed port
   char *user;          // optional user
   char *pass;          // optional pass
   server_proto_t proto;
   struct serverlist *next;
} serverlist_t;

bool server_parse_url(const char *url, char *host, int *port,
                      char *user, char *pass, server_proto_t *proto) {
   const char *p = url;

   // Scheme
   if (strncmp(p, "ws://", 5) == 0) {
      *proto = SERVER_WS;
      *port = 80;
      p += 5;
   } else if (strncmp(p, "wss://", 6) == 0) {
      *proto = SERVER_WSS;
      *port = 443;
      p += 6;
   } else {
      return false;
   }

   // Optional user[:pass]@
   const char *at = strchr(p, '@');
   if (at) {
      const char *colon = memchr(p, ':', at - p);
      if (colon) {
         size_t ulen = colon - p;
         size_t plen = at - colon - 1;
         if (ulen >= HTTP_USER_LEN || plen >= HTTP_PASS_LEN) {
            return false;
         }
         memcpy(user, p, ulen); user[ulen] = '\0';
         memcpy(pass, colon + 1, plen); pass[plen] = '\0';
      } else {
         size_t ulen = at - p;
         if (ulen >= HTTP_USER_LEN) {
            return false;
         }
         memcpy(user, p, ulen); user[ulen] = '\0';
         pass[0] = '\0';
      }
      p = at + 1;
   } else {
      user[0] = '\0';
      pass[0] = '\0';
   }

   // Host[:port]
   const char *slash = strchr(p, '/');
   const char *hostend = slash ? slash : p + strlen(p);
   const char *colon = memchr(p, ':', hostend - p);

   if (colon) {
      size_t hlen = colon - p;
      if (hlen >= 256) {
         return false;
      }
      memcpy(host, p, hlen); host[hlen] = '\0';
      *port = atoi(colon + 1);
   } else {
      size_t hlen = hostend - p;
      if (hlen >= 256) {
         return false;
      }
      memcpy(host, p, hlen); host[hlen] = '\0';
   }

   return true;
}

serverlist_t *serverlist_add(const char *name, const char *url) {
   serverlist_t *sp = malloc(sizeof(serverlist_t));
   if (!sp) {
      return NULL;
   }
   memset(sp, 0, sizeof(*sp));

   char host[256];
   int port = -1;
   char user[HTTP_USER_LEN];
   char pass[HTTP_PASS_LEN];
   server_proto_t proto;

   if (!server_parse_url(url, host, &port, user, pass, &proto)) {
      free(sp);
      return NULL;
   }

   sp->name = strdup(name ? name : "");
   sp->host = strdup(host);
   sp->port = port;
   sp->user = strdup(user);
   sp->pass = strdup(pass);
   sp->proto = proto;
   sp->next = NULL;

   if (!sp->name || !sp->host || !sp->user || !sp->pass) {
      free(sp->name); free(sp->host);
      free(sp->user); free(sp->pass);
      free(sp);
      return NULL;
   }

   return sp;
}

void serverlist_free(serverlist_t *sp) {
   if (!sp) {
      return;
   }
   free(sp->name);
   free(sp->host);
   free(sp->user);
   free(sp->pass);
   free(sp);
}

// --- lookup helpers ---

serverlist_t *serverlist_find_by_name(serverlist_t *head, const char *name) {
   for (serverlist_t *sp = head; sp; sp = sp->next) {
      if (strcmp(sp->name, name) == 0) {
         return sp;
      }
   }

   return NULL;
}

serverlist_t *serverlist_find_by_url(serverlist_t *head, const char *url) {
   char host[256], user[HTTP_USER_LEN], pass[HTTP_PASS_LEN];
   int port = -1;
   server_proto_t proto;

   if (!server_parse_url(url, host, &port, user, pass, &proto)) {
      return NULL;
   }

   for (serverlist_t *sp = head; sp; sp = sp->next) {
      if (sp->port == port &&
          sp->proto == proto &&
          strcmp(sp->host, host) == 0 &&
          strcmp(sp->user, user) == 0 &&
          strcmp(sp->pass, pass) == 0) {
         return sp;
      }
   }
   return NULL;
}

GtkWidget *server_edit_create(serverlist_t *sp) {
   // Show the server edit dialog
   GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   if (!w) {
      return NULL;
   }

   gtk_window_set_title(GTK_WINDOW(w), "Server Editor");
   gui_window_t *window_t = ui_new_window(w, "serveredit");
   if (!window_t) {
      // XXX: Free w
   }

   // If sp passed, populate with the server's details
   if (sp) {
      //
   }
   
   return w;
}
