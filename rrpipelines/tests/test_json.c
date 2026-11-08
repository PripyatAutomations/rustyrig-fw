// This is part of RustyRig: https://github.com/PripyatAutomations/rustyrig-fw
// SPDX-License-Identifier: GPL-2.0-only
// See doc/LICENSE for the full GNU General Public License version 2 text.
#include <librustyaxe/json.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
   dict *request = dict_new();
   assert(request);
   assert(dict_add_uint(request, "pipelines.count", 2) == 0);
   assert(dict_add(request, "pipelines.0.id", "custom.rx") == 0);
   assert(dict_add(request, "pipelines.0.launch", "audiotestsrc wave=sine ! fakesink") == 0);
   assert(dict_add(request, "pipelines.1.id", "custom.tx") == 0);
   assert(dict_add(request, "pipelines.1.launch", "audiotestsrc wave=square ! fakesink") == 0);
   assert(dict_add(request, "metadata.source", "rrclient") == 0);
   char *json = dict2json(request);
   assert(json);
   dict *parsed = json2dict(json);
   assert(parsed);
   assert(dict_get_int(parsed, "pipelines.count", 0) == 2);
   assert(!strcmp(dict_get(parsed, "pipelines.0.id", ""), "custom.rx"));
   assert(!strcmp(dict_get(parsed, "pipelines.0.launch", ""), "audiotestsrc wave=sine ! fakesink"));
   assert(!strcmp(dict_get(parsed, "pipelines.1.id", ""), "custom.tx"));
   assert(!strcmp(dict_get(parsed, "metadata.source", ""), "rrclient"));
   free(json);
   dict_free(parsed);
   dict_free(request);
   puts("rrpipelines librustyaxe JSON contract tests passed");

   return 0;
}
