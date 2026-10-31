#include <glib.h>
#include <rrclient/thread.dispatch.h>

typedef struct {
   GMainContext *context;
   GMainLoop *loop;
   GThread *owner;
   gint ran;
   gint freed;
} test_state;
static gboolean delivered(gpointer data) {
   test_state *t = data;
   g_assert_true(g_thread_self() == t->owner);
   g_atomic_int_inc(&t->ran);
   g_main_loop_quit(t->loop);
   return G_SOURCE_REMOVE;
}
static gpointer run_loop(gpointer data) {
   test_state *t = data;
   t->owner = g_thread_self();
   g_main_context_push_thread_default(t->context);
   g_main_loop_run(t->loop);
   g_main_context_pop_thread_default(t->context);
   return NULL;
}
int main(void) {
   test_state t = {0};
   t.context = g_main_context_new();
   t.loop = g_main_loop_new(t.context, FALSE);
   rr_thread_mailbox_t *box = rr_thread_mailbox_new(t.context);
   GThread *worker = g_thread_new("mailbox-test", run_loop, &t);
   /* Worker owns a distinct GLib context. */
   g_assert_true(rr_thread_mailbox_post(box, delivered, &t, NULL));
   g_thread_join(worker);
   g_assert_cmpint(g_atomic_int_get(&t.ran), ==, 1);
   rr_thread_mailbox_close(box);
   g_assert_false(rr_thread_mailbox_post(box, delivered, &t, NULL));
   rr_thread_mailbox_free(box);
   g_main_loop_unref(t.loop);
   g_main_context_unref(t.context);
   return 0;
}
