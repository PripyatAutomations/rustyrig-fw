#if	!defined(__gtk_chat_h)
#define	__gtk_chat_h

extern bool parse_chat_input(GtkButton *button, gpointer entry);	// chat.cmd.c
extern GtkWidget *chat_textview;
extern GtkWidget *chat_entry;
extern GtkTextBuffer *text_buffer;

#if	defined(USE_GTK)
extern GtkWidget *create_chat_box(void);
#endif	// defined(USE_GTK)

#endif	// !defined(__gtk_chat_h)
