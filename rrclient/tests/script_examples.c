/* RustyScript example smoke test: parse and execute the artwork UI.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <librustyscript/runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned added;
static rrs_value_t *test_state;

static rrs_value_t *stub(rrs_vm_t *vm, int argc,
                         const rrs_value_t *const args[], void *userdata) {
   (void)vm;
   const char *kind = userdata;
   if (!strcmp(kind, "data")) return rrs_string("/tmp");
   if (!strcmp(kind, "regex")) {
      const char *text=rrs_value_string(args[0]);
      const char *pattern=rrs_value_string(args[1]);
      return rrs_boolean(text && pattern &&
         ((!strcmp(pattern,"^[#&]") && text[0]=='#') ||
          (strchr(text,3) && strstr(pattern,"[0-9]"))));
   }
   if (!strcmp(kind, "hash")) return rrs_string("capturetest");
   if (!strcmp(kind, "slug")) return rrs_string("hamradio");
   if (!strcmp(kind, "slice")) return rrs_string("hamradio");
   if (!strcmp(kind, "stamp")) return rrs_string("20261122-150000");
   if (!strcmp(kind, "chr")) {
      char buffer[2]={(char)rrs_value_int(args[0]),0};
      return rrs_string(buffer);
   }
   if (!strcmp(kind, "state_get")) {
      const rrs_value_t *value=rrs_value_field(test_state,rrs_value_string(args[0]));
      return value?rrs_value_copy(value):rrs_null();
   }
   if (!strcmp(kind, "state_set")) {
      assert(argc==2);
      assert(rrs_dict_put(test_state,rrs_value_string(args[0]),args[1]));
      return rrs_boolean(true);
   }
   if (!strcmp(kind, "add")) added++;
   return rrs_null();
}

static rrs_value_t *make_event(const char *id) {
   rrs_value_t *event = rrs_dictionary();
   rrs_value_t *value = rrs_string(id);
   assert(event && value && rrs_dict_put(event, "id", value));
   rrs_value_free(value);
   return event;
}

int main(int argc, char **argv) {
   assert(argc == 2);
   FILE *file = fopen(argv[1], "rb");
   assert(file);
   assert(fseek(file, 0, SEEK_END) == 0);
   long length = ftell(file);
   assert(length > 0 && length < 1024 * 1024);
   rewind(file);
   char *source = malloc((size_t)length + 1);
   assert(source && fread(source, 1, (size_t)length, file) == (size_t)length);
   source[length] = 0;
   fclose(file);

   rrs_vm_t *vm = rrs_vm_new();
   assert(vm);
   assert(rrs_vm_load(vm, source));
   free(source);
   test_state=rrs_dictionary();
   assert(test_state);
   static char data[]="data",generic[]="generic",add[]="add",regex[]="regex";
   static char hash[]="hash",slug[]="slug",slice[]="slice",stamp[]="stamp";
   static char chr[]="chr",state_get[]="state_get",state_set[]="state_set";
   struct {const char *name;char *kind;} bindings[]={
      {"env.data_dir",data},{"ui.utility",generic},{"ui.add",add},
      {"ui.show",generic},{"ui.set",generic},{"ui.message",generic},
      {"string.regex",regex},{"string.chr",chr},{"string.hash",hash},
      {"string.slug",slug},{"string.slice",slice},{"time.stamp",stamp},
      {"state.get",state_get},{"state.set",state_set},{"timer.after",generic}
   };
   for (size_t i = 0; i < sizeof(bindings)/sizeof(bindings[0]); i++)
      assert(rrs_vm_register_native(vm, bindings[i].name, stub,
                                    bindings[i].kind));
   if (!rrs_vm_invoke(vm, RRSCRIPT_COMMAND, "/artwork", NULL)) {
      fprintf(stderr, "artwork command: %s\n", rrs_vm_error(vm));
      return 1;
   }
   assert(added >= 20);
   rrs_value_t *event = make_event("artwork_refresh");
   if (!rrs_vm_invoke(vm, RRSCRIPT_EVENT, "ui.click", event)) {
      fprintf(stderr, "artwork UI click: %s\n", rrs_vm_error(vm));
      return 1;
   }
   rrs_value_free(event);
   event = make_event("ignored");
   if (!rrs_vm_invoke(vm, RRSCRIPT_EVENT, "ui.timer", event)) {
      fprintf(stderr, "artwork timer: %s\n", rrs_vm_error(vm));
      return 1;
   }
   rrs_value_free(event);
   /* Exercise the handler the client previously called without event.room:
    * its failure was incorrectly reported as "expected )". */
   event=rrs_dictionary();
   rrs_value_t *room=rrs_string("#hamradio");
   rrs_value_t *nick=rrs_string("artist");
   rrs_value_t *body=rrs_string("\00304,01 colored artwork");
   assert(event&&room&&nick&&body);
   assert(rrs_dict_put(event,"room",room));
   assert(rrs_dict_put(event,"nick",nick));
   assert(rrs_dict_put(event,"text",body));
   rrs_value_free(room);rrs_value_free(nick);rrs_value_free(body);
   if(!rrs_vm_invoke(vm,RRSCRIPT_EVENT,"irc.privmsg",event)) {
      fprintf(stderr,"artwork IRC handler: %s\n",rrs_vm_error(vm));
      return 1;
   }
   rrs_value_free(event);
   assert(rrs_value_field(test_state,"capture-capturetest.body"));
   rrs_value_free(test_state);
   rrs_vm_free(vm);
   puts("RustyScript artwork example smoke tests passed");
   return 0;
}
