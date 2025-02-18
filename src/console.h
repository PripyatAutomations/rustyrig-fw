#include <stdbool.h>

struct cons_cmds {
   char verb[16];
   int min_args,
       max_args;
   bool (*hndlr)();
};
