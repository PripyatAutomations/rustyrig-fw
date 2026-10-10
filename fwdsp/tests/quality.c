#include <assert.h>
#include <stdio.h>
#include <fwdsp/quality.h>
// Test a runtime quality encoder without depending on an optional plugin.
typedef struct { GstElement parent; gdouble quality; gint bitrate; } HintEncoder;
typedef struct { GstElementClass parent; } HintEncoderClass;
G_DEFINE_TYPE(HintEncoder, hint_encoder, GST_TYPE_ELEMENT)
static void hint_set(GObject *object, guint id, const GValue *value, GParamSpec *spec) {
   (void)spec;
   HintEncoder *encoder = (HintEncoder *)object;
   if (id == 1) encoder->quality = g_value_get_double(value);
   else encoder->bitrate = g_value_get_int(value);
}
static void hint_get(GObject *object, guint id, GValue *value, GParamSpec *spec) {
   (void)spec;
   HintEncoder *encoder = (HintEncoder *)object;
   if (id == 1) g_value_set_double(value, encoder->quality);
   else g_value_set_int(value, encoder->bitrate);
}
static void hint_encoder_class_init(HintEncoderClass *klass) {
   GObjectClass *object = G_OBJECT_CLASS(klass);
   object->set_property = hint_set; object->get_property = hint_get;
   g_object_class_install_property(object, 1, g_param_spec_double("quality", "quality", "quality", 0, 1, 0.8, G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING));
   g_object_class_install_property(object, 2, g_param_spec_int("bitrate", "bitrate", "bitrate", 1000, 100000, 24000, G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING));
}
static void hint_encoder_init(HintEncoder *encoder) { encoder->quality = 0.8; encoder->bitrate = 24000; }

int main(int argc, char **argv) {
   gst_init(&argc, &argv);
   GstElementFactory *factory = gst_element_factory_find("opusenc");
   if (!factory) { puts("SKIP: opusenc not installed"); return 0; }
   gst_object_unref(factory);
   GError *error = NULL;
   GstElement *pipeline = gst_parse_launch("audiotestsrc is-live=true ! audioconvert ! opusenc name=rr-encoder bitrate=24000 ! fakesink", &error);
   assert(pipeline && !error);
   fwdsp_quality_t quality;
   assert(fwdsp_quality_init(pipeline, &quality));
   assert(quality.baseline == 24000);
   assert(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
   assert(gst_element_get_state(pipeline, NULL, NULL, GST_SECOND) != GST_STATE_CHANGE_FAILURE);
   GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "rr-encoder");
   assert(fwdsp_quality_apply(pipeline, &quality, 50));
   gint bitrate = 0;
   g_object_get(encoder, "bitrate", &bitrate, NULL);
   assert(bitrate == 12000);
   assert(!fwdsp_quality_apply(pipeline, &quality, 0));
   assert(!fwdsp_quality_apply(pipeline, &quality, 101));
   assert(fwdsp_quality_apply(pipeline, &quality, 25));
   g_object_get(encoder, "bitrate", &bitrate, NULL);
   assert(bitrate == 6000);
   assert(fwdsp_quality_apply(pipeline, &quality, 100));
   g_object_get(encoder, "bitrate", &bitrate, NULL);
   assert(bitrate == 24000); // restore baseline, not a compounded reduction
   gst_object_unref(encoder);
   gst_element_set_state(pipeline, GST_STATE_NULL);
   gst_object_unref(pipeline);
   pipeline = gst_parse_launch("audiotestsrc ! fakesink", NULL);
   assert(!fwdsp_quality_init(pipeline, &quality));
   assert(!fwdsp_quality_apply(pipeline, &quality, 50));
   gst_object_unref(pipeline);
   pipeline = gst_pipeline_new(NULL);
   HintEncoder *hint = g_object_new(hint_encoder_get_type(), "name", "rr-encoder", NULL);
   gst_bin_add(GST_BIN(pipeline), GST_ELEMENT(hint));
   assert(fwdsp_quality_init(pipeline, &quality));
   assert(!strcmp(quality.property, "quality"));
   assert(fwdsp_quality_apply(pipeline, &quality, 50));
   assert(hint->quality == 0.4 && hint->bitrate == 24000); // prefer quality over bitrate
   assert(fwdsp_quality_apply(pipeline, &quality, 100));
   assert(hint->quality == 0.8);
   gst_object_unref(pipeline);
   puts("PASS: quality preference, live Opus bitrate hints, bounds, recovery and optional opt-in");
   return 0;
}
