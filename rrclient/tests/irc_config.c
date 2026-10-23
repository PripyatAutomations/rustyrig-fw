#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "librrprotocol/cfg.servers.c"

time_t now;
bool dying, restarting;
int main(void) {
   assert(!add_server("chat", "localhost:6667"));

   assert(!add_server("chat", "irc://localhost:99999"));
   assert(!add_server("chat", "ws://localhost:8420/ws/"));
   assert(!server_list);
   assert(add_server("chat", "irc://localhost"));
   assert(add_server("secure", "ircs://nick:secret@[::1]:6697|autojoin=#room"));
   assert(server_list->port == 6667 && !server_list->tls);
   assert(server_list->next->port == 6697 && server_list->next->tls);
   assert(!strcmp(server_list->next->host, "::1"));
   FILE *fp = tmpfile();
   assert(fp && config_servers_save_cb(fp, "test"));
   rewind(fp);
   char contents[4096];
   size_t length = fread(contents, 1, sizeof(contents) - 1, fp);
   contents[length] = '\0';
   fclose(fp);
   assert(strstr(contents, "irc://localhost:6667\n"));
   assert(strstr(contents, "ircs://nick:secret@[::1]:6697|autojoin=#room\n"));
   while (server_list) {
      server_cfg_t *next = server_list->next;
      free(server_list);
      server_list = next;
   }
   puts("PASS: legacy IRC config validates URL schemes/ports and saves explicit IPv6/default ports");

   return 0;
}
