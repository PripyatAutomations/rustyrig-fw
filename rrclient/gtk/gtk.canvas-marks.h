// SPDX-License-Identifier: GPL-2.0-only
/* Reusable canvas annotations, independent of audio and network code. Store world coordinates; callers own persistence and transforms. */
#ifndef RR_CANVAS_MARKS_H
#define RR_CANVAS_MARKS_H
#include <gtk/gtk.h>
#include <math.h>
typedef enum {
   RR_MARK_SELECT, RR_MARK_PEN, RR_MARK_DIVIDER, RR_MARK_RECT,
   RR_MARK_TEXT, RR_MARK_SECTION, RR_MARK_ERASER
} RRMarkTool;
typedef enum {
   RR_MARK_PATH, RR_MARK_LINE, RR_MARK_BOX, RR_MARK_LABEL,
   RR_MARK_GROUP
} RRMarkKind;
typedef struct {
   char *uuid, *text;
   RRMarkKind kind;
   GArray *xy;                  /* array of doubles: x0,y0,x1,y1,... */
   GdkRGBA color;
   double width;
   gboolean selected;
} RRMark;
typedef struct {
   char *snapshot;
   gint64 at;
} RRMarkHistory;
typedef struct {
   GPtrArray *items, *undo, *redo;
   RRMarkTool tool;
   GdkRGBA default_color;
   double default_width;
   RRMark *active;
   gboolean dragging;
   double px, py;
   void (*notify)(gpointer);
   gpointer owner;
} RRMarks;
static void rr_mark_free(gpointer p) {
   RRMark *s = p;
   g_free(s->uuid);
   g_free(s->text);
   g_array_unref(s->xy);
   g_free(s);
}
static void rr_mark_history_free(gpointer p) {
   RRMarkHistory *h = p;
   g_free(h->snapshot);
   g_free(h);
}
static RRMarks *rr_marks_new(void (*notify) (gpointer), gpointer owner) {
   RRMarks *m = g_new0(RRMarks, 1);
   m->items = g_ptr_array_new_with_free_func(rr_mark_free);
   m->undo = g_ptr_array_new_with_free_func(rr_mark_history_free);
   m->redo = g_ptr_array_new_with_free_func(rr_mark_history_free);
   m->notify = notify;
   m->owner = owner;
   gdk_rgba_parse(&m->default_color, "#56b9e8");
   m->default_width = 2.5;

   return m;
}
static void rr_marks_free(RRMarks *m) {
   if (!m) {
      return;
   }
   g_ptr_array_unref(m->items);
   g_ptr_array_unref(m->undo);
   g_ptr_array_unref(m->redo);
   g_free(m);
}
static void rr_mark_add_xy(RRMark *s, double x, double y) {
   double xy[] = {
      x, y
   };
   g_array_append_vals(s->xy, xy, 2);
}
static RRMark *rr_mark_new(RRMarkKind kind, double x, double y) {
   RRMark *s = g_new0(RRMark, 1);
   s->uuid = g_uuid_string_random();
   s->kind = kind;
   s->xy = g_array_new(FALSE, FALSE, sizeof(double));
   s->width = 2.5;
   gdk_rgba_parse(&s->color, "#56b9e8");
   rr_mark_add_xy(s, x, y);

   if (kind != RR_MARK_PATH && kind != RR_MARK_LABEL) {
      rr_mark_add_xy(s, x, y);
   }

   return s;
}
static char *rr_marks_dump(const RRMarks *m) {
   GKeyFile *k = g_key_file_new();
   g_key_file_set_integer(k, "marks", "version", 1);
   g_key_file_set_integer(k, "marks", "count", m->items->len);

   for (guint i = 0 ; i < m->items->len ; i++) {
      RRMark *s = g_ptr_array_index(m->items, i);
      char *name = g_strdup_printf("item%u", i);
      char *color = gdk_rgba_to_string(&s->color);
      g_key_file_set_string(k, name, "uuid", s->uuid);
      g_key_file_set_string(k, name, "text", s->text ? s->text : "");
      g_key_file_set_integer(k, name, "kind", s->kind);
      g_key_file_set_double(k, name, "width", s->width);
      g_key_file_set_string(k, name, "color", color);
      g_key_file_set_double_list(k, name, "xy", (double *)s->xy->data, s->xy->len);
      g_free(color);
      g_free(name);
   }

   char *out = g_key_file_to_data(k, NULL, NULL);
   g_key_file_unref(k);

   return out;
}
static gboolean rr_marks_load(RRMarks *m, const char *data) {
   if (!data) {
      return FALSE;
   }
   GKeyFile *k = g_key_file_new();

   if (!g_key_file_load_from_data(k, data, -1, G_KEY_FILE_NONE, NULL)) {
      g_key_file_unref(k);

      return FALSE;
   }
   int version = g_key_file_get_integer(k, "marks", "version", NULL);
   int count = g_key_file_get_integer(k, "marks", "count", NULL);

   if (version != 1 || count < 0 || count > 5000) {
      g_key_file_unref(k);

      return FALSE;
   }
   GPtrArray *items = g_ptr_array_new_with_free_func(rr_mark_free);

   for (int i = 0 ; i < count ; i++) {
      char *name = g_strdup_printf("item%d", i);
      int kind = g_key_file_get_integer(k, name, "kind", NULL);
      gsize n = 0;
      double *xy = g_key_file_get_double_list(k, name, "xy", &n, NULL);
      gboolean valid = n >= 2 && n <= 20000 && !(n % 2) && kind >= 0 && kind <= RR_MARK_GROUP;

      for (gsize j = 0 ; valid && j < n ; j++) {
         if (!isfinite(xy[j]) || fabs(xy[j]) > 1e7) {
            valid = FALSE;
         }
      }

      if (valid) {
         RRMark *s = rr_mark_new(kind, xy[0], xy[1]);
         g_array_set_size(s->xy, 0);
         g_array_append_vals(s->xy, xy, n);
         g_free(s->uuid);
         s->uuid = g_key_file_get_string(k, name, "uuid", NULL);

         if (!s->uuid) {
            s->uuid = g_uuid_string_random();
         }
         s->text = g_key_file_get_string(k, name, "text", NULL);
         s->width = g_key_file_get_double(k, name, "width", NULL);

         if (!isfinite(s->width) || s->width < .5 || s->width > 30) {
            s->width = 2.5;
         }
         char *color = g_key_file_get_string(k, name, "color", NULL);

         if (color) {
            gdk_rgba_parse(&s->color, color);
         }
         g_free(color);
         g_ptr_array_add(items, s);
      }
      g_free(xy);
      g_free(name);
   }

   g_ptr_array_unref(m->items);
   m->items = items;
   m->active = NULL;
   g_key_file_unref(k);

   return TRUE;
}
static void rr_marks_changed(RRMarks *m) {
   if (m->notify) {
      m->notify(m->owner);
   }
}
static void rr_marks_checkpoint(RRMarks *m) {
   gint64 now = g_get_monotonic_time();

   for (gint i = (gint) m->undo->len - 1 ; i >= 0 ; i--) {
      if (now - ((RRMarkHistory *) g_ptr_array_index(m->undo, i))->at > 300 * G_USEC_PER_SEC) {
         g_ptr_array_remove_index(m->undo, i);
      }
   }

   RRMarkHistory *h = g_new0(RRMarkHistory, 1);
   h->snapshot = rr_marks_dump(m);
   h->at = now;
   g_ptr_array_add(m->undo, h);

   if (m->undo->len > 100) {
      g_ptr_array_remove_index(m->undo, 0);
   }
   g_ptr_array_set_size(m->redo, 0);
}
static gboolean rr_marks_history(RRMarks *m, gboolean redo) {
   GPtrArray *src = redo ? m->redo : m->undo, *dst = redo ? m->undo : m->redo;

   if (!src->len) {
      return FALSE;
   }
   RRMarkHistory *h = g_ptr_array_steal_index(src, src->len - 1);
   RRMarkHistory *current = g_new0(RRMarkHistory, 1);
   current->snapshot = rr_marks_dump(m);
   current->at = g_get_monotonic_time();
   g_ptr_array_add(dst, current);
   rr_marks_load(m, h->snapshot);
   rr_mark_history_free(h);
   rr_marks_changed(m);

   return TRUE;
}
static gboolean rr_mark_hit(const RRMark *s, double x, double y, double tolerance) {
   const double *a = (double *)s->xy->data;

   if (s->kind == RR_MARK_LABEL) {
      return fabs(x - a[0]) < MAX(90., s->text ? strlen(s->text) * 8. : 0.) && y >= a[1] - 20 && y <= a[1] + 12;
   }

   if (s->kind == RR_MARK_BOX || s->kind == RR_MARK_GROUP) {
      if (s->xy->len < 4) {
         return FALSE;
      }
      double left = MIN(a[0], a[2]), right = MAX(a[0], a[2]);
      double top = MIN(a[1], a[3]), bottom = MAX(a[1], a[3]);

      if (x < left - tolerance || x > right + tolerance || y < top - tolerance || y > bottom + tolerance) {
         return FALSE;
      }

      /* Sections must not hijack clicks on the routing blocks they surround. Select only the border or section title band, never its interior. */
      return s->kind == RR_MARK_GROUP ? x <= left + tolerance || x >= right - tolerance || y <= top + MAX(24., tolerance) || y >= bottom - tolerance : TRUE;
   }

   for (guint j = 2 ; j < s->xy->len ; j += 2) {
      double ax = a[j - 2], ay = a[j - 1], bx = a[j], by = a[j + 1];
      double dx = bx - ax, dy = by - ay, den = dx * dx + dy * dy;
      double t = den > 0 ? CLAMP(((x - ax) * dx + (y - ay) * dy) / den, 0., 1.) : 0.;

      if (hypot(x - ax - t * dx, y - ay - t * dy) <= tolerance + s->width) {
         return TRUE;
      }
   }

   return hypot(x - a[0], y - a[1]) <= tolerance + s->width;
}
static gboolean rr_marks_press(RRMarks *m, double x, double y, gboolean additive) {
   m->px = x;
   m->py = y;
   m->active = NULL;

   if (m->tool == RR_MARK_SELECT || m->tool == RR_MARK_ERASER) {
      RRMark *hit = NULL;

      for (gint i = (gint) m->items->len - 1 ; i >= 0 ; i--) {
         RRMark *s = g_ptr_array_index(m->items, i);

         if (rr_mark_hit(s, x, y, 8.)) {
            hit = s;
            break;
         }
      }

      if (m->tool == RR_MARK_ERASER) {
         if (!hit) {
            return TRUE;
         }
         rr_marks_checkpoint(m);

         for (guint i = 0 ; i < m->items->len ; i++) {
            if (g_ptr_array_index(m->items, i) == hit) {
               g_ptr_array_remove_index(m->items, i);
               break;
            }
         }

         rr_marks_changed(m);

         return TRUE;
      }

      if (!additive && (!hit || !hit->selected)) {
         for (guint i = 0 ; i < m->items->len ; i++) {
            ((RRMark *) g_ptr_array_index(m->items, i))->selected = FALSE;
         }
      }

      if (hit) {
         if (additive) {
            hit->selected = !hit->selected;
         } else {
            hit->selected = TRUE;
         }
         m->active = hit;
         m->dragging = hit->selected;

         if (m->dragging) {
            rr_marks_checkpoint(m);
         }
      }
      rr_marks_changed(m);

      return hit != NULL;
   }
   RRMarkKind kind = m->tool == RR_MARK_PEN ? RR_MARK_PATH :
      m->tool == RR_MARK_DIVIDER ? RR_MARK_LINE : m->tool == RR_MARK_RECT ? RR_MARK_BOX : m->tool == RR_MARK_TEXT ? RR_MARK_LABEL : RR_MARK_GROUP;
   rr_marks_checkpoint(m);
   RRMark *s = rr_mark_new(kind, x, y);
   s->color = m->default_color;
   s->width = m->default_width;

   if (kind == RR_MARK_LABEL) {
      s->text = g_strdup("Note");
   }

   if (kind == RR_MARK_GROUP) {
      s->text = g_strdup("Section");
   }
   g_ptr_array_add(m->items, s);
   m->active = s;
   m->dragging = TRUE;
   rr_marks_changed(m);

   return TRUE;
}
static gboolean rr_marks_motion(RRMarks *m, double x, double y, gboolean constrain) {
   if (!m->dragging || !m->active) {
      return FALSE;
   }
   RRMark *s = m->active;

   if (m->tool == RR_MARK_SELECT) {
      double dx = x - m->px, dy = y - m->py;

      for (guint i = 0 ; i < m->items->len ; i++) {
         RRMark *item = g_ptr_array_index(m->items, i);

         if (!item->selected) {
            continue;
         }
         double *p = (double *)item->xy->data;

         for (guint j = 0 ; j < item->xy->len ; j += 2) {
            p[j] += dx;
            p[j + 1] += dy;
         }
      }
   } else if (s->kind == RR_MARK_PATH) {
      if (hypot(x - m->px, y - m->py) > 1.5) {
         rr_mark_add_xy(s, x, y);
      }
   } else if (s->xy->len >= 4) {
      double *p = (double *)s->xy->data;

      if (constrain && s->kind == RR_MARK_LINE) {
         if (fabs(x - p[0]) > fabs(y - p[1])) {
            y = p[1];
         } else {
            x = p[0];
         }
      }
      p[2] = x;
      p[3] = y;
   }
   m->px = x;
   m->py = y;
   rr_marks_changed(m);

   return TRUE;
}
static void rr_marks_release(RRMarks *m) {
   m->dragging = FALSE;
   m->active = NULL;
}
static gboolean rr_marks_delete_selected(RRMarks *m) {
   gboolean found = FALSE;

   for (guint i = 0 ; i < m->items->len ; i++) {
      if (((RRMark *) g_ptr_array_index(m->items, i))->selected) {
         found = TRUE;
      }
   }

   if (!found) {
      return FALSE;
   }
   rr_marks_checkpoint(m);

   for (gint i = (gint) m->items->len - 1 ; i >= 0 ; i--) {
      if (((RRMark *) g_ptr_array_index(m->items, i))->selected) {
         g_ptr_array_remove_index(m->items, i);
      }
   }

   rr_marks_changed(m);

   return TRUE;
}
static void rr_marks_bounds(RRMarks *m, double *width, double *height) {
   if (!m) {
      return;
   }

   for (guint i = 0 ; i < m->items->len ; i++) {
      RRMark *s = g_ptr_array_index(m->items, i);
      double *a = (double *)s->xy->data;

      for (guint j = 0 ; j < s->xy->len ; j += 2) {
         *width = MAX(*width, a[j] + 40);
         *height = MAX(*height, a[j + 1] + 40);
      }
   }
}
static void rr_marks_draw(RRMarks *m, cairo_t *cr) {
   if (!m) {
      return;
   }

   for (guint i = 0 ; i < m->items->len ; i++) {
      RRMark *s = g_ptr_array_index(m->items, i);
      const double *p = (double *)s->xy->data;
      cairo_save(cr);
      cairo_set_source_rgba(cr, s->color.red, s->color.green, s->color.blue, s->color.alpha);
      cairo_set_line_width(cr, s->width);
      cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
      cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

      if (s->kind == RR_MARK_BOX || s->kind == RR_MARK_GROUP) {
         if (s->xy->len >= 4) {
            double x = MIN(p[0], p[2]), y = MIN(p[1], p[3]);
            double w = fabs(p[2] - p[0]), h = fabs(p[3] - p[1]);
            cairo_rectangle(cr, x, y, w, h);

            if (s->kind == RR_MARK_GROUP) {
               cairo_set_source_rgba(cr, s->color.red, s->color.green, s->color.blue, .10);
               cairo_fill_preserve(cr);
               cairo_set_source_rgba(cr, s->color.red, s->color.green, s->color.blue, .9);
            }
            cairo_stroke(cr);
         }
      } else if (s->kind == RR_MARK_LABEL) {
         cairo_move_to(cr, p[0], p[1]);
         cairo_set_font_size(cr, 17.);
         cairo_show_text(cr, s->text ? s->text : "");
      } else {
         cairo_move_to(cr, p[0], p[1]);

         for (guint j = 2 ; j < s->xy->len ; j += 2) {
            cairo_line_to(cr, p[j], p[j + 1]);
         }

         cairo_stroke(cr);
      }

      if (s->kind == RR_MARK_GROUP && s->text) {
         cairo_move_to(cr, p[0] + 9, MIN(p[1], p[3]) + 21);
         cairo_set_font_size(cr, 16.);
         cairo_show_text(cr, s->text);
      }

      if (s->selected) {
         cairo_set_source_rgba(cr, 1., .8, .2, .85);
         cairo_arc(cr, p[0], p[1], 5, 0, 2 * G_PI);
         cairo_fill(cr);
      }
      cairo_restore(cr);
   }
}
#endif
