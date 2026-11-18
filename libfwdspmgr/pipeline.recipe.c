// SPDX-License-Identifier: GPL-2.0-only
#include <libfwdspmgr/pipeline.recipe.h>
#include <glib.h>
#include <string.h>

/* Restrict inputs to bounded integer rates and escaped device identifiers. Gst's launch string parser must never treat the device as pipeline syntax. */
static char *device_argument(const char *device) {
   if (!device || !*device || !strcmp(device, "default")) {
      return g_strdup("device=default");
   }

   if (strlen(device) > 255 || !g_utf8_validate(device, -1, NULL)) {
      return NULL;
   }
   GString *quoted = g_string_new("device=\"");

   for (const char *p = device ; *p ; p++) {
      if (*p == '\\' || *p == '"') {
         g_string_append_c(quoted, '\\');
      }

      if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7f) {
         g_string_free(quoted, TRUE);

         return NULL;
      }
      g_string_append_c(quoted, *p);
   }

   g_string_append_c(quoted, '"');

   return g_string_free(quoted, FALSE);
}

char *rr_pipeline_recipe_compile(const rr_pipeline_recipe_t *recipe) {
   if (!recipe || (recipe->device_rate_hz != 44100 && recipe->device_rate_hz != 48000)) {
      return NULL;
   }
   const bool capture = recipe->endpoint == RR_PIPELINE_CAPTURE_MONO;

   if (!capture && recipe->endpoint != RR_PIPELINE_PLAYBACK_STEREO) {
      return NULL;
   }

   if (recipe->pcm_rate_hz != (capture ? 16000u : recipe->device_rate_hz)) {
      return NULL;
   }
   char *device = device_argument(recipe->device);

   if (!device) {
      return NULL;
   }
   char *description;

   if (capture) {
      description = g_strdup_printf("pulsesrc %s name=processor-mic client-name=rrclient-mic do-timestamp=true ! "
         "audioconvert ! capsfilter name=processor-device-caps caps=audio/x-raw ! "
         "audioresample ! audio/x-raw,format=S16LE,rate=16000,channels=1,layout=interleaved ! "
         "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! "
         "appsink name=processor-sink emit-signals=false sync=false max-buffers=5 drop=true", device);
   } else {
      description = g_strdup_printf("appsrc name=processor-src is-live=true format=time do-timestamp=true "
         "caps=audio/x-raw,format=S16LE,rate=%u,channels=2,layout=interleaved ! "
         "queue max-size-buffers=8 max-size-time=100000000 leaky=downstream ! "
         "volume name=processor-vol ! audioconvert ! audioresample ! "
         "capsfilter name=processor-device-caps caps=audio/x-raw,rate=%u ! "
         "pulsesink %s name=processor-speaker client-name=rrclient-speaker sync=false", recipe->pcm_rate_hz, recipe->device_rate_hz, device);
   }
   g_free(device);

   return description;
}
