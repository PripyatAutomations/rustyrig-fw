/* RustyScript example smoke test: parse and execute the artwork UI.
 * SPDX-License-Identifier: GPL-2.0-only */
#include <librustyscript/runtime.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned added;

static rrs_value_t *stub(rrs_vm_t *vm, int argc,
                         const rrs_value_t *const args[], void *userdata) {
   (void)vm; (void)argc; (void)args;
   const char *kind = userdata;
   if (!strcmp(kind, "data")) return rrs_string("/tmp");
   if (!strcmp(kind, "regex")) return rrs_boolean(false);
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
   struct { const char *name, *kind; } bindings[] = {
      { "env.data_dir", "data" }, { "ui.utility", "generic" },
      { "ui.add", "add" }, { "ui.show", "generic" },
      { "ui.set", "generic" }, { "ui.message", "generic" },
      { "string.regex", "regex" }
   };
   for (size_t i = 0; i < sizeof(bindings)/sizeof(bindings[0]); i++)
      assert(rrs_vm_register_native(vm, bindings[i].name, stub,
                                    (void *)bindings[i].kind));
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
   rrs_vm_free(vm);
   puts("RustyScript artwork example smoke tests passed");
   return 0;
}
