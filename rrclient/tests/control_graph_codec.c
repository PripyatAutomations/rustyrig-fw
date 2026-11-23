// SPDX-License-Identifier: GPL-2.0-only
#include <rrclient/control.graph.h>
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char valid[]=
   "[control]\nversion=2\ngates=3\nwires=3\nserial=3\n"
   "[gate.0]\nid=constant.1\nkind=TRUE\ninputs=0\nx=10\ny=20\n"
   "[gate.1]\nid=gate.2\nkind=AND\ninputs=2\nx=200\ny=20\n"
   "[gate.2]\nid=gate.3\nkind=XNOR\ninputs=2\nx=400\ny=20\n"
   "[wire.0]\nfrom=constant.1\nfrom_port=out_a\nto=gate.2\nto_port=in_a\n"
   "[wire.1]\nfrom=cos.local\nfrom_port=out_a\nto=gate.2\nto_port=in_b\n"
   "[wire.2]\nfrom=gate.2\nfrom_port=out_a\nto=gate.3\nto_port=in_a\n";

static void reject_replace(const char *old,const char *replacement) {
   char *why=NULL;
   GKeyFile *k=g_key_file_new();
   assert(g_key_file_load_from_data(k,valid,-1,G_KEY_FILE_NONE,NULL));
   /* Replace one complete key value, avoiding fragile string offsets. */
   if (!strcmp(old,"version"))
      g_key_file_set_integer(k,"control","version",atoi(replacement));
   else if (!strcmp(old,"from_port"))
      g_key_file_set_string(k,"wire.0","from_port",replacement);
   else if (!strcmp(old,"to_port"))
      g_key_file_set_string(k,"wire.0","to_port",replacement);
   else if (!strcmp(old,"cycle"))
      g_key_file_set_string(k,"wire.2","to","gate.2");
   else if (!strcmp(old,"legacy")) {
      g_key_file_remove_key(k,"wire.0","from_port",NULL);
      g_key_file_remove_key(k,"wire.0","to_port",NULL);
      g_key_file_set_integer(k,"wire.0","pin",0);
   }
   gchar *serialized=g_key_file_to_data(k,NULL,NULL);
   assert(!rr_control_graph_text_validate(serialized,&why));
   assert(why && *why);
   g_free(why);
   g_free(serialized);
   g_key_file_unref(k);
}

int main(void) {
   char *why=NULL;
   assert(rr_control_graph_text_validate(valid,&why));
   assert(!why);
   reject_replace("cycle","");
   reject_replace("version","1");
   reject_replace("legacy","");
   reject_replace("from_port","in_a");
   reject_replace("to_port","out_a");
   reject_replace("to_port","in_zz");
   reject_replace("to_port","in_c");
   assert(!rr_control_graph_text_validate("[control]\nversion=2\ngates=33\nwires=0\nserial=0\n",&why));
   g_free(why);
   puts("PASS: v2 named ports, legacy rejection, direction, bad pins, cycles and bounds");
   return 0;
}
