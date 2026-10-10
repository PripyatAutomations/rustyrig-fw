// Runtime encoder hints: the pipeline explicitly names its encoder rr-encoder.
#ifndef FWDSP_QUALITY_H
#define FWDSP_QUALITY_H
#include <gst/gst.h>
#include <string.h>

typedef struct {
   const char *property;
   double baseline, worst;
} fwdsp_quality_t;

static inline gboolean fwdsp_quality_number(const GValue *value, double *number) {
   if (G_VALUE_HOLDS_INT(value)) *number = g_value_get_int(value);
   else if (G_VALUE_HOLDS_UINT(value)) *number = g_value_get_uint(value);
   else if (G_VALUE_HOLDS_FLOAT(value)) *number = g_value_get_float(value);
   else if (G_VALUE_HOLDS_DOUBLE(value)) *number = g_value_get_double(value);
   else return FALSE;
   return TRUE;
}

static inline gboolean fwdsp_quality_init(GstElement *pipeline, fwdsp_quality_t *state) {
   memset(state, 0, sizeof(*state));
   GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "rr-encoder");
   if (!encoder) return FALSE;
   const char *properties[] = {"quality", "bitrate"};
   for (unsigned i = 0; i < 2; i++) {
      GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), properties[i]);
      if (!spec || !(spec->flags & G_PARAM_READABLE) || !(spec->flags & G_PARAM_WRITABLE) ||
         !(spec->flags & GST_PARAM_MUTABLE_PLAYING)) continue;
      GValue value = G_VALUE_INIT;
      g_value_init(&value, spec->value_type);
      g_object_get_property(G_OBJECT(encoder), properties[i], &value);
      gboolean numeric = fwdsp_quality_number(&value, &state->baseline);
      g_value_unset(&value);
      if (!numeric || (i == 1 && state->baseline <= 0)) continue;
      state->property = properties[i];
      if (i == 0) {
         if (G_IS_PARAM_SPEC_FLOAT(spec)) state->worst = G_PARAM_SPEC_FLOAT(spec)->minimum;
         else if (G_IS_PARAM_SPEC_DOUBLE(spec)) state->worst = G_PARAM_SPEC_DOUBLE(spec)->minimum;
         else if (G_IS_PARAM_SPEC_INT(spec)) state->worst = G_PARAM_SPEC_INT(spec)->minimum;
         else if (G_IS_PARAM_SPEC_UINT(spec)) state->worst = G_PARAM_SPEC_UINT(spec)->minimum;
         // LAME quality runs in the opposite direction (0 is best).
         GstElementFactory *factory = gst_element_get_factory(encoder);
         if (factory && !strcmp(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)), "lamemp3enc") && G_IS_PARAM_SPEC_FLOAT(spec)) {
            state->worst = G_PARAM_SPEC_FLOAT(spec)->maximum;
         }
      }
      break;
   }
   gst_object_unref(encoder);
   return state->property != NULL;
}

static inline gboolean fwdsp_quality_apply(GstElement *pipeline, const fwdsp_quality_t *state, unsigned percent) {
   if (!state->property || percent < 25 || percent > 100) return FALSE;
   GstElement *encoder = gst_bin_get_by_name(GST_BIN(pipeline), "rr-encoder");
   if (!encoder) return FALSE;
   GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(encoder), state->property);
   gboolean applied = FALSE;
   if (spec && (spec->flags & G_PARAM_WRITABLE) && (spec->flags & GST_PARAM_MUTABLE_PLAYING)) {
      GValue value = G_VALUE_INIT;
      g_value_init(&value, spec->value_type);
      double target = state->worst + (state->baseline - state->worst) * percent / 100;
      if (G_VALUE_HOLDS_INT(&value)) g_value_set_int(&value, (gint)CLAMP(target, G_MININT, G_MAXINT));
      else if (G_VALUE_HOLDS_UINT(&value)) g_value_set_uint(&value, (guint)CLAMP(target, 0, G_MAXUINT));
      else if (G_VALUE_HOLDS_FLOAT(&value)) g_value_set_float(&value, (gfloat)target);
      else if (G_VALUE_HOLDS_DOUBLE(&value)) g_value_set_double(&value, target);
      double unused;
      if (fwdsp_quality_number(&value, &unused)) {
         g_param_value_validate(spec, &value);
         g_object_set_property(G_OBJECT(encoder), state->property, &value);
         applied = TRUE;
      }
      g_value_unset(&value);
   }
   gst_object_unref(encoder);
   return applied;
}
#endif
