//
// rrclient/gtk.chat.h: Chat stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__gtk_chat_h)
#define	__gtk_chat_h

extern bool parse_chat_input(GtkButton *button, gpointer entry);         // chat.cmd.c
extern GtkWidget *chat_textview;
extern GtkWidget *chat_entry;
extern GtkTextBuffer *text_buffer;
extern GtkWidget *create_chat_box(void);
extern void gtk_chat_room_add(const char *room);
extern void gtk_chat_room_remove(const char *room);
extern void gtk_chat_set_authoritative_room(const char *room);
extern const char *gtk_chat_current_room(void);

#endif // !defined(__gtk_chat_h)
