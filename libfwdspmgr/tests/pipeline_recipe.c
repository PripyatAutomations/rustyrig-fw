// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <string.h>
#include <glib.h>
#include <libfwdspmgr/pipeline.recipe.h>

int main(void) {
   rr_pipeline_recipe_t in = {
      RR_PIPELINE_CAPTURE_MONO, 48000, 16000, "default"
   };
   char *text = rr_pipeline_recipe_compile(&in);
   assert(text && strstr(text, "pulsesrc device=default"));
   assert(strstr(text, "processor-sink"));
   assert(strstr(text, "rate=16000,channels=1"));
   g_free(text);

   rr_pipeline_recipe_t out = {
      RR_PIPELINE_PLAYBACK_STEREO, 44100, 44100,
      "USB Audio DAC"
   };
   text = rr_pipeline_recipe_compile(&out);
   assert(text && strstr(text, "appsrc name=processor-src"));
   assert(strstr(text, "rate=44100,channels=2"));
   assert(strstr(text, "device=\"USB Audio DAC\""));
   assert(strstr(text, "processor-vol"));
   g_free(text);

   out.device = "x\" ! fakesink name=unsafe";
   text = rr_pipeline_recipe_compile(&out);
   assert(text && strstr(text, "device=\"x\\\" ! fakesink name=unsafe\""));
   g_free(text);

   out.device_rate_hz = 96000;
   assert(!rr_pipeline_recipe_compile(&out));
   out.device_rate_hz = 44100;
   out.pcm_rate_hz = 48000;
   assert(!rr_pipeline_recipe_compile(&out));
   out.pcm_rate_hz = 44100;
   out.device = "foo\nbar";
   assert(!rr_pipeline_recipe_compile(&out));
   g_print("pipeline recipes: PASS\n");

   return 0;
}
