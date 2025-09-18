#if	!defined(__gtk_alertdialog_h)
#define	__gtk_alertdialog_h

typedef enum {
   MSG_ERROR,
   MSG_WARNING,
   MSG_INFO,
   MSG_QUESTION,
   MSG_LAST
} AlertType;

typedef struct {
   AlertType kind;
   GtkMessageType gtk_type;
   const char *title;
} AlertDialogStyle;

extern void alert_dialog_register(AlertType kind, GtkMessageType gtk_type, const char *title);
extern void alert_dialogs_init(void);
extern void alert_dialog(GtkWindow *parent, AlertType kind, const char *msg);

#endif	// !defined(__gtk_alertdialog_h)
