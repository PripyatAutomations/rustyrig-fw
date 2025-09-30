#if	!defined(__librustyaxe_tui_h)
#define __librustyaxe_tui_h

#include <readline/readline.h>
#include <readline/history.h>

#define LOG_LINES 10
#define STATUS_LINES 1
#define STATUS_LEN 256

extern void add_log(const char *msg);
extern bool update_status(const char *status);
extern bool tui_init(void);

#endif	// !defined(__librustyaxe_tui_h)
