//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Hardware callbacks execute on one worker per instance. Model, wire and lifecycle
// notifications are replayed exclusively by drain() on the owning server thread.
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <rrserver/backend.worker.h>
#include <rrserver/rig.registry.h>

#define BACKEND_QUEUE_LIMIT 64

static long long worker_ms(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


typedef struct observation {
   struct observation *next;
   rr_server_vfo_t *vfo;
   char *name, *text;
   val_type_t type;
   dict_value_t value;
   bool unavailable;
} observation_t;

typedef struct backend_log {
   struct backend_log *next;
   logpriority_t priority;
   char *subsystem, *text;
} backend_log_t;

typedef struct backend_job {
   struct backend_job *next, *followers;
   rr_server_vfo_t *vfo;
   rr_vfo_t index, active;
   rr_backend_operation_t operation;
   dict_value_t value;
   char *text;
   rr_backend_completion_fn complete;
   void *user;
   bool failed, cancelled, observed, ptt;
   long long queued_ms;
   float power;
   rr_vfo_data_t state;
   int widths[3], width_count, shutdown_code;
   observation_t *observations;
   backend_log_t *logs;
} backend_job_t;

typedef struct backend_cache {
   bool supported, known, ptt;
   float power;
   rr_vfo_data_t state;
   int widths[3], width_count;
} backend_cache_t;

struct rr_backend_worker {
   pthread_t thread;
   pthread_mutex_t mutex;
   pthread_cond_t condition;
   rr_backend_t *owner;
   rr_backend_t io;
   bool stopping;
   unsigned queued, outstanding;
   backend_job_t *head, *tail, *done_head, *done_tail;
   bool polls[MAX_VFOS];
   backend_job_t *off_jobs[MAX_VFOS];
   bool keyed[MAX_VFOS];
   backend_cache_t cache[MAX_VFOS]; // Written/read only by the server thread.
   rr_backend_observation_fn publish;
};

static _Thread_local backend_job_t *running_job;
static _Thread_local rr_backend_t *current_worker_backend;

rr_backend_t *rr_backend_worker_current(void) {
   return current_worker_backend;
}

static bool vfo_index(rr_server_vfo_t *vfo, rr_vfo_t *index) {
   return rr_server_vfo_native_index(vfo, index) && *index >= VFO_A && *index < MAX_VFOS;
}

static void finish_job_locked(struct rr_backend_worker *worker, backend_job_t *job) {
   if (worker->off_jobs[job->index] == job) {
      worker->off_jobs[job->index] = NULL;
   }
   backend_job_t *followers = job->followers;
   job->followers = NULL;
   job->next = NULL;

   if (worker->done_tail) {
      worker->done_tail->next = job;
   } else {
      worker->done_head = job;
   }
   worker->done_tail = job;
   while (followers) {
      backend_job_t *next = followers->next;
      followers->failed = job->failed;
      followers->cancelled = job->cancelled;
      followers->ptt = job->ptt;
      finish_job_locked(worker, followers);
      followers = next;
   }
}

static void free_job(backend_job_t *job) {
   while (job->observations) {
      observation_t *item = job->observations;
      job->observations = item->next;
      free(item->name);
      free(item->text);
      free(item);
   }
   while (job->logs) {
      backend_log_t *item = job->logs;
      job->logs = item->next;
      free(item->subsystem);
      free(item->text);
      free(item);
   }
   free(job->text);
   free(job);
}

rr_property_update_t rr_backend_property_observe(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *name, val_type_t type, const dict_value_t *value) {
   if (!running_job) {
      return rr_vfo_property_observe(vfo, name, type, value);
   }
   observation_t *item = calloc(1, sizeof(*item));

   if (!item || !(item->name = strdup(name))) {
      abort();
   }
   item->vfo = vfo;
   item->type = type;
   item->value = *value;

   if (type == VAL_STR) {
      if (!(item->text = strdup(value->s))) {
         abort();
      }
      item->value.s = item->text;
   }
   observation_t **tail = &running_job->observations;
   while (*tail) {
      tail = &(*tail)->next;
   }
   *tail = item;

   return RR_PROPERTY_CHANGED;
}

rr_property_update_t rr_backend_property_unavailable(rr_backend_t *backend, rr_server_vfo_t *vfo, const char *name) {
   if (!running_job) {
      return rr_vfo_property_unavailable(vfo, name);
   }
   dict_value_t empty = {
      0
   };
   rr_property_update_t result = rr_backend_property_observe(backend, vfo, name, VAL_INT, &empty);
   observation_t *item = running_job->observations;
   while (item->next) {
      item = item->next;
   }
   item->unavailable = true;

   return result;
}

bool rr_backend_worker_shutdown(rr_backend_t *backend, int code) {
   if (!running_job) {
      return false;
   }
   running_job->shutdown_code = code;

   return true;
}

void rr_backend_log(rr_backend_t *backend, logpriority_t priority, const char *subsystem, const char *format, ...) {
   va_list args;
   va_start(args, format);
   char *text = NULL;

   if (vasprintf(&text, format, args) < 0) {
      abort();
   }
   va_end(args);

   if (!running_job) {
      Log(priority, subsystem, "%s", text);
      free(text);

      return;
   }
   backend_log_t *item = calloc(1, sizeof(*item));

   if (!item || !(item->subsystem = strdup(subsystem))) {
      abort();
   }
   item->text = text;
   item->priority = priority;
   backend_log_t **tail = &running_job->logs;
   while (*tail) {
      tail = &(*tail)->next;
   }
   *tail = item;
}

static void execute(struct rr_backend_worker *worker, backend_job_t *job) {
   rr_backend_t *backend = &worker->io;
   const rr_backend_funcs_t *api = backend->type->api;
   backend->active_vfo = job->active;
   running_job = job;
   long long started_ms = worker_ms();
   long long queue_ms = started_ms - job->queued_ms;

   switch (job->operation) {
      case RR_BACKEND_FREQ: {
         job->failed = !api->freq_set || api->freq_set(backend, job->vfo, job->value.i);
         break;
      }
      case RR_BACKEND_MODE: {
         job->failed = !api->mode_set || api->mode_set(backend, job->vfo, job->value.i);
         break;
      }
      case RR_BACKEND_WIDTH: {
         job->failed = !api->width_set || api->width_set(backend, job->vfo, job->text);
         break;
      }
      case RR_BACKEND_POWER: {
         job->failed = !api->power_set || api->power_set(backend, job->vfo, job->value.f);
         break;
      }
      case RR_BACKEND_SWAP: {
         job->failed = !api->vfo_swap || api->vfo_swap(backend, job->vfo);
         break;
      }
      case RR_BACKEND_SPLIT: {
         job->failed = !api->split_mode ||
            api->split_mode(backend, job->vfo, job->value.i ? "1" : "0");
         break;
      }
      case RR_BACKEND_PTT: {
         job->failed = !api->ptt_set || api->ptt_set(backend, job->vfo, job->value.i);
         break;
      }
      default: {
         break;
      }
   }

   long long control_ms = worker_ms() - started_ms;
   if (queue_ms >= 250 || control_ms >= 250) {
      rr_backend_log(backend, LOG_CRAZY, "backend.latency",
         "%s VFO %d: queue=%lldms operation=%lldms",
         job->operation == RR_BACKEND_FREQ ? "freq" :
         job->operation == RR_BACKEND_POLL ? "poll" :
         job->operation == RR_BACKEND_SELECT ? "select" : "control",
         (int)job->index, queue_ms, control_ms);
   }

   /* SELECT always polls its targeted VFO to drive the backend's
    * active-VFO transition; unlike ordinary polls, it is not coalesced. */
   // PTT completion must not wait for an additional full polling sweep.
   if (job->operation != RR_BACKEND_PTT && !job->failed && api->poll_state) {
      rr_vfo_data_t *state = api->poll_state(backend, job->vfo);

      if (state) {
         job->state = *state;
         job->observed = true;
         free(state);

         if (api->power_get) {
            job->power = api->power_get(backend, job->vfo);
         }

         if (api->widths_get) {
            job->width_count = api->widths_get(backend, job->vfo, job->widths, 3);
         }
      }
   }
   long long total_ms = worker_ms() - started_ms;
   if (total_ms - control_ms >= 250) {
      rr_backend_log(backend, LOG_CRAZY, "backend.latency",
         "%s VFO %d: state-refresh=%lldms total=%lldms",
         job->operation == RR_BACKEND_FREQ ? "freq" :
         job->operation == RR_BACKEND_POLL ? "poll" : "control",
         (int)job->index, total_ms - control_ms, total_ms);
   }
   job->ptt = api->ptt_get && api->ptt_get(backend, job->vfo);
   worker->keyed[job->index] = job->ptt;
   running_job = NULL;
}

static void *worker_main(void *arg) {
   struct rr_backend_worker *worker = arg;
   current_worker_backend = &worker->io;

   for ( ;; ) {
      pthread_mutex_lock(&worker->mutex);
      while (!worker->head && !worker->stopping) {
         pthread_cond_wait(&worker->condition, &worker->mutex);
      }

      if (worker->stopping && !worker->head) {
         pthread_mutex_unlock(&worker->mutex);
         break;
      }
      backend_job_t *job = worker->head;
      worker->head = job->next;

      if (!worker->head) {
         worker->tail = NULL;
      }
      worker->queued--;
      pthread_mutex_unlock(&worker->mutex);
      execute(worker, job);
      pthread_mutex_lock(&worker->mutex);
      finish_job_locked(worker, job);
      pthread_mutex_unlock(&worker->mutex);
   }

   // Shutdown never abandons an already keyed transmitter.
   const rr_backend_funcs_t *api = worker->io.type->api;

   for (int i = 0 ; i < MAX_VFOS ; i++) {
      if (worker->keyed[i] && api->ptt_set) {
         char alias[] = {
            'A' + i, 0
         };
         rr_server_vfo_t *vfo = rr_server_vfo_find_alias(worker->io.owner, alias);
         backend_job_t final = {
            0
         };
         running_job = &final;

         if (vfo && api->ptt_set(&worker->io, vfo, false)) {
            rr_backend_log(&worker->io, LOG_CRIT, "backend", "%s: shutdown PTT release failed on VFO %c", rr_backend_instance_alias(worker->owner), 'A' + i);
         }
         running_job = NULL;
         // Retain shutdown errors/observations for the main thread too.
         backend_job_t *result = calloc(1, sizeof(*result));

         if (!result) {
            abort();
         }
         *result = final;
         result->vfo = vfo;
         result->index = i;
         pthread_mutex_lock(&worker->mutex);
         worker->outstanding++;
         finish_job_locked(worker, result);
         pthread_mutex_unlock(&worker->mutex);
      }
   }

   if (api->destroy) {
      api->destroy(&worker->io);
   }
   current_worker_backend = NULL;

   return NULL;
}

bool rr_backend_worker_submit(rr_backend_t *backend, rr_server_vfo_t *vfo, rr_backend_operation_t operation, dict_value_t value, rr_backend_completion_fn
   complete, void *user) {
   struct rr_backend_worker *worker = backend->worker;
   rr_vfo_t index;

   if (!worker || !vfo_index(vfo, &index) || rr_server_vfo_owner(vfo) != backend->owner) {
      return true;
   }
   bool release = operation == RR_BACKEND_PTT && !value.i;
   pthread_mutex_lock(&worker->mutex);

   if (worker->stopping || (worker->outstanding >= BACKEND_QUEUE_LIMIT + MAX_VFOS) || (!release && worker->outstanding >= BACKEND_QUEUE_LIMIT)) {
      pthread_mutex_unlock(&worker->mutex);

      return true;
   }

   if (operation == RR_BACKEND_POLL && worker->polls[index]) {
      pthread_mutex_unlock(&worker->mutex);

      return false;
   }

   // A release supersedes queued key-downs; it also gets ahead of normal I/O.
   if (release) {
      backend_job_t **link = &worker->head;
      worker->tail = NULL;
      while (*link) {
         backend_job_t *pending = *link;

         if (pending->operation == RR_BACKEND_PTT && pending->index == index && pending->value.i) {
            *link = pending->next;
            worker->queued--;
            pending->failed = pending->cancelled = true;
            finish_job_locked(worker, pending);
         } else {
            worker->tail = pending;
            link = &pending->next;
         }
      }
   }
   backend_job_t *job = calloc(1, sizeof(*job));

   if (!job) {
      pthread_mutex_unlock(&worker->mutex);

      return true;
   }
   *job = (backend_job_t) {
      .vfo = vfo, .index = index, .active = backend->active_vfo, .operation = operation, .value = value, .complete = complete, .user = user, .queued_ms = worker_ms()
   };

   if (operation == RR_BACKEND_WIDTH && !(job->text = strdup(value.s))) {
      free(job);
      pthread_mutex_unlock(&worker->mutex);

      return true;
   }

   if (release && worker->off_jobs[index]) {
      backend_job_t *parent = worker->off_jobs[index];
      job->next = parent->followers;
      parent->followers = job;
      worker->outstanding++;
      pthread_mutex_unlock(&worker->mutex);

      return false;
   }

   if (release) {
      job->next = worker->head;
      worker->head = job;

      if (!worker->tail) {
         worker->tail = job;
      }
   } else if (operation != RR_BACKEND_POLL) {
      backend_job_t **link = &worker->head;
      while (*link && (*link)->operation != RR_BACKEND_POLL) {
         link = &(*link)->next;
      }
      job->next = *link;
      *link = job;

      if (!job->next) {
         worker->tail = job;
      }
   } else {
      if (worker->tail) {
         worker->tail->next = job;
      } else {
         worker->head = job;
      }
      worker->tail = job;
   }
   worker->queued++;
   worker->outstanding++;

   if (operation == RR_BACKEND_POLL) {
      worker->polls[index] = true;
   }

   if (release) {
      worker->off_jobs[index] = job;
   }
   pthread_cond_signal(&worker->condition);
   pthread_mutex_unlock(&worker->mutex);

   return false;
}

static backend_cache_t *cached(rr_backend_t *backend, rr_server_vfo_t *vfo) {
   rr_vfo_t index;

   return vfo_index(vfo, &index) ? &backend->worker->cache[index] : NULL;
}

static bool cached_supported(rr_backend_t *b, rr_server_vfo_t *v) {
   backend_cache_t *c = cached(b, v);

   return c && c->supported;
}

static bool cached_ptt(rr_backend_t *b, rr_server_vfo_t *v) {
   backend_cache_t *c = cached(b, v);

   return c && c->ptt;
}

static float cached_freq(rr_backend_t *b, rr_server_vfo_t *v) {
   backend_cache_t *c = cached(b, v);

   return c && c->known ? c->state.freq : 0;
}

static float cached_power(rr_backend_t *b, rr_server_vfo_t *v) {
   backend_cache_t *c = cached(b, v);

   return c && c->known ? c->power : 0;
}

static uint16_t cached_width(rr_backend_t *b, rr_server_vfo_t *v) {
   backend_cache_t *c = cached(b, v);

   return c && c->known ? c->state.width : 0;
}

static rr_mode_t cached_mode(rr_backend_t *b, rr_server_vfo_t *v) {
   backend_cache_t *c = cached(b, v);

   return c && c->known ? c->state.mode : MODE_NONE;
}

static const char *cached_mode_text(rr_backend_t *b, rr_server_vfo_t *v) {
   return vfo_mode_name(cached_mode(b, v));
}

static int cached_widths(rr_backend_t *b, rr_server_vfo_t *v, int *widths, int max) {
   backend_cache_t *c = cached(b, v);

   if (!c || max < c->width_count) {
      return 0;
   }
   memcpy(widths, c->widths, c->width_count * sizeof(*widths));

   return c->width_count;
}

static rr_vfo_data_t *queue_poll(rr_backend_t *b, rr_server_vfo_t *v) {
   rr_backend_worker_submit(b, v, RR_BACKEND_POLL, (dict_value_t) {
      0
   }, NULL, NULL);

   return NULL;
}

static bool queue_freq(rr_backend_t *b, rr_server_vfo_t *v, int f) {
   return rr_backend_worker_submit(b, v, RR_BACKEND_FREQ, (dict_value_t) {
      .i = f
   }
      , NULL, NULL);
}

static bool queue_mode(rr_backend_t *b, rr_server_vfo_t *v, rr_mode_t m) {
   return rr_backend_worker_submit(b, v, RR_BACKEND_MODE, (dict_value_t) {
      .i = m
   }
      , NULL, NULL);
}

static bool queue_width(rr_backend_t *b, rr_server_vfo_t *v, const char *w) {
   return rr_backend_worker_submit(b, v, RR_BACKEND_WIDTH, (dict_value_t) {
      .s = w
   }
      , NULL, NULL);
}

static bool queue_power(rr_backend_t *b, rr_server_vfo_t *v, float p) {
   return rr_backend_worker_submit(b, v, RR_BACKEND_POWER, (dict_value_t) {
      .f = p
   }
      , NULL, NULL);
}

static bool queue_ptt(rr_backend_t *b, rr_server_vfo_t *v, bool p) {
   return rr_backend_worker_submit(b, v, RR_BACKEND_PTT, (dict_value_t) {
      .i = p
   }
      , NULL, NULL);
}

static const rr_backend_funcs_t worker_api = {
   .poll_state = queue_poll, .vfo_supported = cached_supported, .ptt_set = queue_ptt, .ptt_get = cached_ptt,
   .freq_set = queue_freq, .freq_get = cached_freq, .mode_set = queue_mode, .mode_get = cached_mode, .mode_get_str = cached_mode_text,
   .width_set = queue_width, .width_get = cached_width, .widths_get = cached_widths, .power_set = queue_power, .power_get = cached_power
};

static bool initialize_vfo(rr_server_vfo_t *vfo, void *arg) {
   struct rr_backend_worker *worker = arg;
   rr_vfo_t index;

   if (vfo_index(vfo, &index)) {
      worker->cache[index].supported = !worker->io.type->api->vfo_supported || worker->io.type->api->vfo_supported(&worker->io, vfo);
   }

   return false;
}

bool rr_backend_worker_start(rr_backend_t *backend, rr_backend_observation_fn publish) {
   if (backend->worker || backend->type->event_driven) {
      return false;
   }
   struct rr_backend_worker *worker = calloc(1, sizeof(*worker));

   if (!worker) {
      return true;
   }
   worker->owner = backend;
   worker->io = *backend;
   worker->io.worker = worker;
   worker->publish = publish;
   pthread_mutex_init(&worker->mutex, NULL);
   pthread_cond_init(&worker->condition, NULL);
   rr_server_vfo_foreach(backend->owner, initialize_vfo, worker);

   if (pthread_create(&worker->thread, NULL, worker_main, worker)) {
      pthread_cond_destroy(&worker->condition);
      pthread_mutex_destroy(&worker->mutex);
      free(worker);

      return true;
   }
   backend->worker = worker;
   backend->dispatch = &worker_api;

   return false;
}

void rr_backend_worker_drain(rr_backend_t *backend) {
   struct rr_backend_worker *worker = backend->worker;

   if (!worker) {
      return;
   }
   pthread_mutex_lock(&worker->mutex);
   backend_job_t *jobs = worker->done_head;
   worker->done_head = worker->done_tail = NULL;
   pthread_mutex_unlock(&worker->mutex);
   while (jobs) {
      backend_job_t *job = jobs;
      jobs = job->next;

      if (worker->stopping && !(job->operation == RR_BACKEND_PTT && !job->value.i)) {
         job->failed = job->cancelled = true;
      }
      backend_cache_t *cache = &worker->cache[job->index];

      if (!job->cancelled) {
         cache->ptt = job->ptt;

         if (job->observed) {
            cache->known = true;
            cache->state = job->state;
            cache->power = job->power;
            cache->width_count = job->width_count;
            memcpy(cache->widths, job->widths, sizeof(cache->widths));
         }

         for (observation_t *o = job->observations ; o ; o = o->next) {
            if (o->unavailable) {
               rr_vfo_property_unavailable(o->vfo, o->name);
            } else {
               rr_vfo_property_observe(o->vfo, o->name, o->type, &o->value);
            }
         }

         if (job->observed && worker->publish) {
            worker->publish(backend, job->vfo, &job->state);
         }
      }

      for (backend_log_t *log = job->logs ; log ; log = log->next) {
         Log(log->priority, log->subsystem, "%s", log->text);
      }

      pthread_mutex_lock(&worker->mutex);
      worker->outstanding--;

      if (job->operation == RR_BACKEND_POLL) {
         worker->polls[job->index] = false;
      }

      pthread_mutex_unlock(&worker->mutex);

      if (job->complete) {
         job->complete(backend, job->vfo, job->failed, job->user);
      } else if (job->failed && !job->cancelled) {
         Log(LOG_WARN, "backend", "%s: queued operation %u failed", rr_backend_instance_alias(backend), job->operation);
      }

      if (job->shutdown_code) {
         dict *d = dict_new();
         dict_add_int(d, "backend.shutdown-code", job->shutdown_code);
         event_emit_dict("backend.shutdown.request", NULL, d);
         dict_free(d);
      }
      free_job(job);
   }
}

bool rr_backend_worker_busy(rr_backend_t *backend) {
   struct rr_backend_worker *worker = backend->worker;

   if (!worker) {
      return false;
   }
   pthread_mutex_lock(&worker->mutex);
   bool busy = worker->outstanding != 0;
   pthread_mutex_unlock(&worker->mutex);

   return busy;
}

bool rr_backend_worker_yield(rr_backend_t *backend) {
   struct rr_backend_worker *worker = backend->worker;

   if (!running_job || !worker) {
      return false;
   }
   pthread_mutex_lock(&worker->mutex);
   backend_job_t *pending = worker->head;
   bool urgent = pending && pending->operation != RR_BACKEND_POLL &&
      (running_job->operation == RR_BACKEND_POLL || (pending->operation == RR_BACKEND_PTT && !pending->value.i));
   pthread_mutex_unlock(&worker->mutex);

   return urgent;
}

void rr_backend_worker_stop(rr_backend_t *backend) {
   struct rr_backend_worker *worker = backend->worker;

   if (!worker) {
      return;
   }
   pthread_mutex_lock(&worker->mutex);
   worker->stopping = true;
   backend_job_t **link = &worker->head;
   worker->tail = NULL;
   while (*link) {
      backend_job_t *job = *link;

      if (job->operation == RR_BACKEND_PTT && !job->value.i) {
         worker->tail = job;
         link = &job->next;
      } else {
         *link = job->next;
         worker->queued--;
         job->failed = job->cancelled = true;
         finish_job_locked(worker, job);
      }
   }
   pthread_cond_signal(&worker->condition);
   pthread_mutex_unlock(&worker->mutex);
   pthread_join(worker->thread, NULL);
   rr_backend_worker_drain(backend);
   backend->data = NULL;
   backend->dispatch = NULL;
   backend->worker = NULL;
   pthread_cond_destroy(&worker->condition);
   pthread_mutex_destroy(&worker->mutex);
   free(worker);
}
