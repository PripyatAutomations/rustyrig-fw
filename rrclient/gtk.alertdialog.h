//
// rrclient/gtk.alertdialog.c: Alert dialogs in GTK
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
#if     !defined(__gtk_alertdialog_h)
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

#endif // !defined(__gtk_alertdialog_h)
