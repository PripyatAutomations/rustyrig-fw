extern bool parse_chat_input(GtkButton *button, gpointer entry);	// chat.cmd.c
extern GtkWidget *chat_textview;
extern GtkWidget *chat_entry;
extern GtkTextBuffer *text_buffer;
extern const char *get_chat_ts(void);
#if	defined(USE_GTK)
extern GtkWidget *create_chat_box(void);
#endif	// defined(USE_GTK)
