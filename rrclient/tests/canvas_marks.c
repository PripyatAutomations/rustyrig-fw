// SPDX-License-Identifier: GPL-2.0-only
/* Compile with GTK3, e.g. cc -std=gnu23 -I. $(pkg-config --cflags --libs gtk+-3.0) */
#include <assert.h>
#include <rrclient/gtk/gtk.canvas-marks.h>
int main(void) {
   RRMarks *m = rr_marks_new(NULL, NULL);
   m->tool = RR_MARK_SECTION;
   assert(rr_marks_press(m, 100, 100, FALSE));
   assert(rr_marks_motion(m, 400, 300, FALSE));
   rr_marks_release(m);
   assert(m->items->len == 1);
   assert(!rr_mark_hit(g_ptr_array_index(m->items, 0), 200, 200, 5));
   assert(rr_mark_hit(g_ptr_array_index(m->items, 0), 100, 110, 5));
   char *saved = rr_marks_dump(m);
   RRMarks *copy = rr_marks_new(NULL, NULL);
   assert(rr_marks_load(copy, saved));
   assert(copy->items->len == 1);
   assert(rr_marks_history(m, FALSE));
   assert(m->items->len == 0);
   assert(rr_marks_history(m, TRUE));
   assert(m->items->len == 1);
   m->tool = RR_MARK_SELECT;
   assert(rr_marks_press(m, 100, 110, FALSE));
   rr_marks_release(m);
   assert(rr_marks_delete_selected(m));
   assert(m->items->len == 0);
   g_free(saved);
   rr_marks_free(copy);
   rr_marks_free(m);

   return 0;
}
