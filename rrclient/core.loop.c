//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "core.loop.h"
#include <glib.h>

/* This context is separate from the default context used by GTK. Do not put connection processing here until all UI -> core calls have been converted to owned
 * commands. */
static GMainContext *core_context;
static GMainLoop *core_loop;
static GThread *core_thread;
static GMutex core_lock;
static gsize core_once;

typedef struct {
   GSourceFunc callback;
   gpointer data;
   GDestroyNotify destroy;
} CoreCommand;

static gboolean run_command(gpointer data) {
   CoreCommand *cmd = data;

   return cmd->callback(cmd->data);
}
static void free_command(gpointer data) {
   CoreCommand *cmd = data;

   if (cmd->destroy) {
      cmd->destroy(cmd->data);
   }
   g_free(cmd);
}
static gpointer core_run(gpointer unused) {
   (void)unused;
   g_main_context_push_thread_default(core_context);
   g_main_loop_run(core_loop);
   g_main_context_pop_thread_default(core_context);

   return NULL;
}
bool rrclient_core_loop_start(void) {
   if (g_once_init_enter(&core_once)) {
      g_mutex_init(&core_lock);
      g_once_init_leave(&core_once, 1);
   }
   g_mutex_lock(&core_lock);

   if (core_thread) {
      g_mutex_unlock(&core_lock);

      return true;
   }
   core_context = g_main_context_new();
   core_loop = g_main_loop_new(core_context, FALSE);
   core_thread = g_thread_new("rrclient-core", core_run, NULL);
   bool ok = core_thread != NULL;

   if (!ok) {
      g_main_loop_unref(core_loop);
      g_main_context_unref(core_context);
      core_loop = NULL;
      core_context = NULL;
   }
   g_mutex_unlock(&core_lock);

   return ok;
}
bool rrclient_core_loop_post(GSourceFunc callback, gpointer payload, GDestroyNotify destroy) {
   if (!callback) {
      if (destroy) {
         destroy(payload);
      }

      return false;
   }
   CoreCommand *cmd = g_new(CoreCommand, 1);
   *cmd = (CoreCommand) {
      callback, payload, destroy
   };
   g_mutex_lock(&core_lock);

   if (!core_context || !core_thread) {
      g_mutex_unlock(&core_lock);
      free_command(cmd);

      return false;
   }
   GSource *source = g_idle_source_new();
   g_source_set_callback(source, run_command, cmd, free_command);
   g_source_attach(source, core_context);
   g_source_unref(source);
   g_mutex_unlock(&core_lock);

   return true;
}
void rrclient_core_loop_stop(void) {
   g_mutex_lock(&core_lock);
   GThread *thread = core_thread;

   if (!thread) {
      g_mutex_unlock(&core_lock);

      return;
   }
   /* Stop is called by the main thread after frontend shutdown. */
   g_main_loop_quit(core_loop);
   g_main_context_wakeup(core_context);
   g_mutex_unlock(&core_lock);
   g_thread_join(thread);
   g_mutex_lock(&core_lock);
   core_thread = NULL;
   g_main_loop_unref(core_loop);
   g_main_context_unref(core_context);
   core_loop = NULL;
   core_context = NULL;
   g_mutex_unlock(&core_lock);
}
bool rrclient_core_loop_is_owner(void) {
   g_mutex_lock(&core_lock);
   GThread *thread = core_thread;
   bool result = thread && thread == g_thread_self();
   g_mutex_unlock(&core_lock);

   return result;
}
