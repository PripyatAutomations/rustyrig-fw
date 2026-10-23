// Read-only, server-owned five-minute process statistics.
// Do not transmit filesystem paths, client identities or private configuration.
#include "build_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/resource.h>
#ifdef __linux__
#include <dirent.h>
#endif
#include <glib.h>
#include <rrserver/performance.h>
#include <rrserver/discovery.h>
#include <rrserver/relay.h>
#include <rrserver/link.h>
#include <librrprotocol/wire.h>

typedef struct {
   time_t at;
   uint64_t rss_bytes, virtual_bytes, peak_rss_bytes;
   unsigned long fds, ptys;
   bool fd_known, memory_known;
   double cpu_user_s, cpu_system_s;
   event_profile_summary_t events, event_delta;
   rr_wire_profile_summary_t wire, wire_delta;
   rr_catalog_profile_info_t catalog;
   bool event_enabled, wire_enabled, catalog_enabled;
} perf_sample_t;

static perf_sample_t performance;
static bool performance_sampled;

static uint64_t positive_difference(uint64_t current, uint64_t previous) {
   return current >= previous ? current - previous : 0;
}

static void process_snapshot(perf_sample_t *s) {
   struct rusage usage;
   if (!getrusage(RUSAGE_SELF, &usage)) {
      s->cpu_user_s = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
      s->cpu_system_s = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
#ifdef __linux__
      s->peak_rss_bytes = (uint64_t)usage.ru_maxrss * 1024;
#endif
   }
#ifdef __linux__
   FILE *statm = fopen("/proc/self/statm", "r");
   if (statm) {
      unsigned long long pages = 0, resident = 0;
      if (fscanf(statm, "%llu %llu", &pages, &resident) == 2) {
         long page_bytes = sysconf(_SC_PAGESIZE);
         if (page_bytes > 0) {
            s->virtual_bytes = pages * (uint64_t)page_bytes;
            s->rss_bytes = resident * (uint64_t)page_bytes;
            s->memory_known = true;
         }
      }
      fclose(statm);
   }

   DIR *fds = opendir("/proc/self/fd");
   if (fds) {
      struct dirent *item;
      int directory_fd = dirfd(fds);
      s->fd_known = true;
      while ((item = readdir(fds))) {
         if (item->d_name[0] == '.') continue;
         char *tail;
         long fd = strtol(item->d_name, &tail, 10);
         if (*tail || fd < 0 || fd == directory_fd) continue;
         s->fds++;
         char link[80], target[256];
         int n = snprintf(link, sizeof(link), "/proc/self/fd/%ld", fd);
         if (n < 0 || (size_t)n >= sizeof(link)) continue;
         ssize_t size = readlink(link, target, sizeof(target) - 1);
         if (size < 0) continue;
         target[size] = 0;
         if (!strncmp(target, "/dev/pts/", 9) || !strcmp(target, "/dev/ptmx"))
            s->ptys++;
      }
      closedir(fds);
   }
#endif
}

void rrserver_performance_sample(void) {
   const perf_sample_t old = performance;
   perf_sample_t current = {0};
   current.at = time(NULL);
   process_snapshot(&current);
   current.event_enabled = event_profile_summary(&current.events);
   current.wire_enabled = rr_wire_profile_summary(&current.wire);
   current.catalog_enabled = rr_relay_catalog_profile_summary(&current.catalog);
   if (current.event_enabled) {
      current.event_delta.emits = positive_difference(current.events.emits, old.events.emits);
      current.event_delta.payload_bytes = positive_difference(current.events.payload_bytes, old.events.payload_bytes);
      current.event_delta.inline_calls = positive_difference(current.events.inline_calls, old.events.inline_calls);
      current.event_delta.inline_us = positive_difference(current.events.inline_us, old.events.inline_us);
      current.event_delta.queued = positive_difference(current.events.queued, old.events.queued);
      current.event_delta.completed = positive_difference(current.events.completed, old.events.completed);
      current.event_delta.deferred_us = positive_difference(current.events.deferred_us, old.events.deferred_us);
      current.event_delta.queue_wait_us = positive_difference(current.events.queue_wait_us, old.events.queue_wait_us);
   }
   if (current.wire_enabled) {
      current.wire_delta.messages = positive_difference(current.wire.messages, old.wire.messages);
      current.wire_delta.bytes = positive_difference(current.wire.bytes, old.wire.bytes);
      current.wire_delta.rejected = positive_difference(current.wire.rejected, old.wire.rejected);
      current.wire_delta.processing_us = positive_difference(current.wire.processing_us, old.wire.processing_us);
      current.wire_delta.json_messages = positive_difference(current.wire.json_messages, old.wire.json_messages);
      current.wire_delta.binary_messages = positive_difference(current.wire.binary_messages, old.wire.binary_messages);
   }
   performance = current;
   performance_sampled = true;
#ifdef USE_MONGOOSE
   /* Refresh locally authoritative catalog services. Existing delta comparison
    * disseminates only changed rows to directly connected neighbors. */
   extern bool rr_relay_catalog_dirty;
   rr_relay_catalog_dirty = true;
#endif
}

static void emit_performance(rr_performance_emit_fn emit, void *ctx,
                             const char *name, const char *description, bool ready) {
   dict *row = rr_inventory_row("", 1, "performance", name, NULL);
   if (!row) return;
   dict_add(row, "inventory.description", description);
   dict_add(row, "inventory.state", ready ? "Sampled" : "Unavailable");
   dict_add(row, "inventory.access", "read-only");
   emit(row, ctx); /* Takes ownership and frees the row. */
}

typedef struct {
   rr_performance_emit_fn emit;
   void *context;
} neighbor_health_emit_t;
static void emit_neighbor_health(const char *subject, const char *site, bool online,
                                 unsigned flaps, unsigned auth_errors, long long rtt_ms,
                                 long long avg_rtt_ms, long long max_rtt_ms,
                                 unsigned samples, unsigned high_streak, bool latency_degraded, void *context) {
   neighbor_health_emit_t *target = context;
   unsigned severity = !online ? 1 : 0;
   if (!online) severity = 2;
   else if (flaps >= 3 || auth_errors >= 5 || (latency_degraded && rtt_ms >= 1000)) severity = 2;
   else if (flaps || auth_errors || latency_degraded) severity = 1;
   char name[160], description[512];
   snprintf(name, sizeof(name), "Neighbor %s", site);
   snprintf(description, sizeof(description),
      "Subject: %s\nDirect link: %s\nLast RTT: %lld ms\nRTT rolling average: %lld ms\nRTT rolling maximum: %lld ms\nSamples: %u (last 30 PONGs, about five minutes)\nConsecutive high RTTs: %u\nLatency condition: %s\nUnexpected disconnects (5m): %u\nLink auth failures (5m): %u\nAssessment: %s\nWarning thresholds: two RTTs >=250 ms; recover after three RTTs <200 ms.",
      subject, online ? "connected" : "offline", rtt_ms, avg_rtt_ms, max_rtt_ms,
      samples, high_streak, latency_degraded ? "degraded" : "normal",
      flaps, auth_errors, severity == 2 ? "Critical" : severity == 1 ? "Warning" : "Healthy");
   dict *row = rr_inventory_row("", 1, "performance", name, NULL);
   if (!row) return;
   dict_add(row, "inventory.description", description);
   dict_add(row, "inventory.state", severity == 2 ? "Critical" : severity == 1 ? "Warning" : "Healthy");
   dict_add(row, "inventory.access", "read-only");
   target->emit(row, target->context);
   char causes[1024] = {0};
   rrserver_links_cause_counts(subject, causes, sizeof(causes));
   if (*causes) {
      char cause_name[192];
      snprintf(cause_name, sizeof(cause_name), "Neighbor %s failure causes (5m)", site);
      dict *counts = rr_inventory_row("", 1, "performance", cause_name, NULL);
      if (counts) {
         dict_add(counts, "inventory.description", causes);
         dict_add(counts, "inventory.state", "Statistics");
         dict_add(counts, "inventory.access", "read-only");
         target->emit(counts, target->context);
      }
   }
   /* A separate diagnostic row keeps the standard five-minute summary short
    * while exposing durable, timestamped link causes in the catalog. */
   char history[2048] = {0};
   rrserver_links_recent_events(subject, history, sizeof(history));
   if (*history) {
      char history_name[192];
      snprintf(history_name, sizeof(history_name), "Neighbor %s recent events", site);
      dict *events = rr_inventory_row("", 1, "performance", history_name, NULL);
      if (events) {
         dict_add(events, "inventory.description", history);
         dict_add(events, "inventory.state", "History");
         dict_add(events, "inventory.access", "read-only");
         target->emit(events, target->context);
      }
   }
}

void rrserver_performance_collect(rr_performance_emit_fn emit, void *context) {
   if (!emit) return;
   if (!performance_sampled) rrserver_performance_sample();
   const perf_sample_t *s = &performance;
   char at[64] = "not yet sampled";
   if (s->at > 0) {
      struct tm local;
      if (localtime_r(&s->at, &local))
         strftime(at, sizeof(at), "%Y-%m-%d %H:%M:%S %Z", &local);
   }

   char description[1300];
   snprintf(description, sizeof(description),
       "Sampled: %s\nCurrent RSS: %s\nCurrent RSS: %.2f MiB\nVirtual memory: %.2f MiB\nPeak RSS: %.2f MiB\nCPU user: %.3f s; system: %.3f s",
       at, s->memory_known ? "available" : "unavailable (cannot read /proc/self/statm)",
       s->rss_bytes / 1048576.0, s->virtual_bytes / 1048576.0,
       s->peak_rss_bytes / 1048576.0, s->cpu_user_s, s->cpu_system_s);
   emit_performance(emit, context, "Process memory / CPU", description, s->memory_known);

   snprintf(description, sizeof(description),
       "Sampled: %s\nOpen file descriptors: %s\nOpen descriptors: %lu\nOpen PTY-related descriptors: %lu\nCounts refer to this server process only; PTY descriptors are not necessarily separate sessions.",
       at, s->fd_known ? "available" : "unavailable (cannot inspect /proc/self/fd)",
       s->fds, s->ptys);
   emit_performance(emit, context, "Files / PTYs", description, s->fd_known);

   if (s->event_enabled) {
      const event_profile_summary_t *t = &s->events, *w = &s->event_delta;
      snprintf(description, sizeof(description),
          "Sampled: %s\nLast interval: %" PRIu64 " emits, %" PRIu64 " payload bytes; %" PRIu64 " inline handlers (%.1f us avg), %" PRIu64 " deferred completions (%.1f us avg), queue wait %.1f us avg\nLifetime: %" PRIu64 " emits, %" PRIu64 " inline handlers, %" PRIu64 " deferred completions\nMax inline handler: %" PRIu64 " us (%s); max deferred handler: %" PRIu64 " us",
          at, w->emits, w->payload_bytes, w->inline_calls,
          w->inline_calls ? (double)w->inline_us / w->inline_calls : 0.0,
          w->completed, w->completed ? (double)w->deferred_us / w->completed : 0.0,
          w->completed ? (double)w->queue_wait_us / w->completed : 0.0,
          t->emits, t->inline_calls, t->completed, t->max_inline_us,
          *t->slowest_event ? t->slowest_event : "none", t->max_deferred_us);
   } else {
      snprintf(description, sizeof(description),
          "Handler timing is disabled in this build. Enable features.profiling and rebuild to collect per-event timings.");
   }
   emit_performance(emit, context, "Event handlers", description, s->event_enabled);

   if (s->wire_enabled) {
      const rr_wire_profile_summary_t *t = &s->wire, *w = &s->wire_delta;
      snprintf(description, sizeof(description),
          "Sampled: %s\nLast interval: %" PRIu64 " messages (%" PRIu64 " JSON, %" PRIu64 " binary), %" PRIu64 " bytes, %" PRIu64 " rejected, %.1f us average processing\nLifetime: %" PRIu64 " messages, %" PRIu64 " bytes, %" PRIu64 " rejected",
          at, w->messages, w->json_messages, w->binary_messages, w->bytes, w->rejected,
          w->messages ? (double)w->processing_us / w->messages : 0.0,
          t->messages, t->bytes, t->rejected);
   } else {
      snprintf(description, sizeof(description),
          "Wire processing counters are disabled in this build. Enable features.profiling and rebuild.");
   }
   emit_performance(emit, context, "WebSocket processing", description, s->wire_enabled);

   if (s->catalog_enabled) {
      const rr_catalog_profile_info_t *p = &s->catalog;
      snprintf(description, sizeof(description),
          "Sampled: %s\nCatalog entries: %" PRIu64 "; queued snapshots: %" PRIu64 "; pending ACKs: %" PRIu64 "\nLast %" PRIu64 " seconds: snapshots %" PRIu64 ", completed queues %" PRIu64 ", successful ACKs %" PRIu64 ", partial ACKs %" PRIu64 ", timeouts %" PRIu64 "\nInbound descriptors: %" PRIu64 " received, %" PRIu64 " accepted, %" PRIu64 " rejected (%" PRIu64 " bytes)\nSnapshot build avg %.2f ms, max %.2f ms; ACK avg %.2f ms, max %.2f ms; descriptor apply avg %.1f us, max %" PRIu64 " us",
          at, p->catalog_entries, p->pending_snapshots, p->pending_acks,
          p->sample_seconds, p->snapshots, p->queue_complete, p->ack_complete, p->ack_partial, p->ack_timeouts,
          p->descriptors_received, p->descriptors_accepted, p->descriptors_rejected, p->payload_bytes,
          p->snapshots ? (double)p->snapshot_build_us / p->snapshots / 1000.0 : 0.0,
          p->max_build_us / 1000.0,
          p->ack_complete + p->ack_partial ? (double)p->ack_us / (p->ack_complete + p->ack_partial) / 1000.0 : 0.0,
          p->max_ack_us / 1000.0,
          p->descriptors_received ? (double)p->apply_us / p->descriptors_received : 0.0,
          p->max_apply_us);
   } else {
      snprintf(description, sizeof(description),
          "Catalog profiling is disabled. Enable features.profiling to collect synchronized catalog timings.");
   }
   emit_performance(emit, context, "Catalog synchronization", description, s->catalog_enabled);
   neighbor_health_emit_t neighbor = { emit, context };
   rrserver_links_health_foreach(emit_neighbor_health, &neighbor);
}
