#if     !defined(__rrclient_gtk_vfo_box_h)
#define	__rrclient_gtk_vfo_box_h
#include <librustyaxe/config.h>

extern GtkWidget *create_vfo_box(void);
extern gui_window_t *create_vfo_window(GtkWidget *vfo_box, char vfo);
extern bool vfo_set_dict(dict *d);

#endif // !defined(__rrclient_gtk_vfo_box_h)
