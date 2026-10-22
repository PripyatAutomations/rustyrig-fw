# Drop-in GTK3 Matrix widget

Copy `rrclient/gtk/gtk.matrix.c` and `gtk.matrix.h` into your project.
Only GTK3 (3.8+) and its normal GLib/Pango/Cairo dependencies are needed;
there are no RustyRig headers, libraries, config keys, or initialization hooks.
Call the API on the GTK main thread after `gtk_init()`.

Embed in any GTK container, including a notebook page:

```c
GtkWidget *rain = rr_gtk_matrix_new();
gtk_notebook_append_page(GTK_NOTEBOOK(notebook), rain,
                         gtk_label_new("Matrix"));
rr_gtk_matrix_set_options(rain, 85, 100, 100);
gtk_widget_show(rain);
```

For an overlay, add the widget with `gtk_overlay_add_overlay()` to an existing
GtkOverlay. Opacity applies to the black canvas and its glyphs together,
revealing whatever is underneath. Controls remain opaque and usable at 0%.
The widget receives pointer input; it is not a click-through background.
In an ordinary window, reduced opacity reveals the window's themed background,
not the desktop. No compositor transparency is required.

A standalone program:

```c
#include "gtk.matrix.h"

int main(int argc, char **argv) {
   gtk_init(&argc, &argv);
   GtkWidget *window = rr_gtk_matrix_window_new(NULL, TRUE);
   g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
   gtk_main();
   return 0;
}
```

Build with:

```sh
cc demo.c gtk.matrix.c $(pkg-config --cflags --libs gtk+-3.0) -o matrix-demo
```

Pass `FALSE` for an ordinary resizable window; pass your existing GtkWindow
as the first argument for a child window that closes with its parent.
Escape closes only helper-created windows. Embedded instances leave keyboard
and activation policy to their host. Nothing installs a global shortcut,
starts automatically, or quits the host application.

Hover the bottom-left 48×48 pixels to open the controls. Move away from the
popup to dismiss it. Opacity and brightness range from 0–100%; speed ranges
from 0–300%, with 0 pausing. Options are per instance and are not persisted.
Hidden widgets stop advancing; destruction releases the animation resources.
Pango uses installed fallback fonts for Unicode halfwidth katakana and digits.
