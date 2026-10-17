// Named RX slice deduplication and subscription bookkeeping.
// Actual channelization and PCM fanout will be connected in the audio hub.
#include <rrserver/rx.slice.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct rr_rx_slice {
   char id[RR_RX_SLICE_NAME_MAX];
   unsigned subscribers;
   struct rr_rx_slice *next;
};
struct rr_rx_slices {
   struct rr_rx_slice *first;
   size_t count;
};

static bool valid(const rr_rx_slice_spec_t *s) {
   if (!s || !s->source[0] || !memchr(s->source, 0, sizeof(s->source)) ||
       !s->center_hz || !s->bandwidth_hz || !s->rate ||
       s->bandwidth_hz > s->rate || s->rate > 192000 ||
       (s->channels != 1 && s->channels != 2) ||
       (s->format != RR_RX_S16LE && s->format != RR_RX_F32LE)) return false;
   for (const char *p = s->source; *p; p++)
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_'))
         return false;
   return true;
}
bool rr_rx_slice_name(const rr_rx_slice_spec_t *spec, char *out, size_t size) {
   if (!out || !size) return false;
   out[0] = 0;
   if (!valid(spec)) return false;
   int n = snprintf(out, size, "rx://%s/slice/%" PRIu64
      "Hz/bw%" PRIu32 "Hz/rate%" PRIu32 "/%s/%s",
      spec->source, spec->center_hz, spec->bandwidth_hz, spec->rate,
      spec->format == RR_RX_S16LE ? "s16le" : "f32le",
      spec->channels == 1 ? "mono" : "stereo");
   return n > 0 && (size_t)n < size;
}
bool rr_rx_slice_parse(const char *name, rr_rx_slice_spec_t *out) {
   if (!name || !out || strlen(name) >= RR_RX_SLICE_NAME_MAX ||
       strncmp(name, "rx://", 5)) return false;
   rr_rx_slice_spec_t spec = {0};
   char format[16] = {0}, layout[16] = {0};
   int consumed = 0;
   int matched = sscanf(name, "rx://%63[^/]/slice/%" SCNu64
      "Hz/bw%" SCNu32 "Hz/rate%" SCNu32 "/%15[^/]/%15[^/]%n",
      spec.source, &spec.center_hz, &spec.bandwidth_hz,
      &spec.rate, format, layout, &consumed);
   if (matched != 6 || consumed != (int)strlen(name)) return false;
   if (!strcmp(format, "s16le")) spec.format = RR_RX_S16LE;
   else if (!strcmp(format, "f32le")) spec.format = RR_RX_F32LE;
   else return false;
   if (!strcmp(layout, "mono")) spec.channels = 1;
   else if (!strcmp(layout, "stereo")) spec.channels = 2;
   else return false;
   char canonical[RR_RX_SLICE_NAME_MAX];
   if (!rr_rx_slice_name(&spec, canonical, sizeof(canonical)) ||
       strcmp(canonical, name)) return false;
   *out = spec;
   return true;
}
rr_rx_slices_t *rr_rx_slices_new(void) {
   return calloc(1, sizeof(rr_rx_slices_t));
}
void rr_rx_slices_free(rr_rx_slices_t *registry) {
   if (!registry) return;
   while (registry->first) {
      rr_rx_slice_t *slice = registry->first;
      registry->first = slice->next;
      free(slice);
   }
   free(registry);
}
rr_rx_slice_t *rr_rx_slice_acquire(rr_rx_slices_t *registry,
   const rr_rx_slice_spec_t *spec) {
   if (!registry) return NULL;
   char id[RR_RX_SLICE_NAME_MAX];
   if (!rr_rx_slice_name(spec, id, sizeof(id))) return NULL;
   for (rr_rx_slice_t *s = registry->first; s; s = s->next) {
      if (!strcmp(s->id, id)) {
         if (s->subscribers == UINT32_MAX) return NULL;
         s->subscribers++;
         return s;
      }
   }
   rr_rx_slice_t *slice = calloc(1, sizeof(*slice));
   if (!slice) return NULL;
   snprintf(slice->id, sizeof(slice->id), "%s", id);
   slice->subscribers = 1;
   slice->next = registry->first;
   registry->first = slice;
   registry->count++;
   return slice;
}
void rr_rx_slice_release(rr_rx_slices_t *registry, rr_rx_slice_t *slice) {
   if (!registry || !slice) return;
   rr_rx_slice_t **slot = &registry->first;
   while (*slot && *slot != slice) slot = &(*slot)->next;
   if (!*slot) return;
   if (--slice->subscribers) return;
   *slot = slice->next;
   registry->count--;
   free(slice);
}
const char *rr_rx_slice_id(const rr_rx_slice_t *slice) {
   return slice ? slice->id : NULL;
}
unsigned rr_rx_slice_subscribers(const rr_rx_slice_t *slice) {
   return slice ? slice->subscribers : 0;
}
size_t rr_rx_slices_count(const rr_rx_slices_t *registry) {
   return registry ? registry->count : 0;
}
