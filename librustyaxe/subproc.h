#if	!defined(_subproc_h)
#define	_subproc_h
#include <limits.h>
#include <stdlib.h>
//#include <ev.h>

#define	MAX_SUBPROC	128			// i doubt we'll ever reach this limit, but it'd be cool if we could (that's a lot of bands!)

typedef struct subproc subproc_t;
struct subproc {
   int 		pid;			// host process id
   char		name[91];		// subprocess name (for subproc_list() etc)
   char		path[PATH_MAX];		// path to executable
   char		*argv[128];		// pointer to the arguments needed to execute the process
   int		argc;			// arguments counter
//   ev_child	watcher;		// ev_child watcher for process
   time_t	restart_time;		// when should we restart?
   time_t	watchdog_start;		// watchdog is started if the process crashes, it expires after cfg:supervisor/max-crash-time.
                                             // when active, watchdog_events tracks the number of crashes
   int		watchdog_events;	// during watchdog, this is incremented with the number of crashes
   int		needs_restarted;	// does it need restarted by the periodic thread?
   ///////////////////////
   // stdio redirection //
   ///////////////////////
   int		_stdin[2];		// stdin of process
   int		_stdout[2];		// stdout of process
   int		_stderr[2];		// stderr of process
//   ev_io	stdin_watcher;
//   ev_io	stdout_watcher;
//   ev_io	stderr_watcher;
};
extern bool subproc_init(void);
extern int subproc_killall(int signum);
extern void subproc_shutdown_all(void);
extern int subproc_check_all(void);
extern int subproc_respawn_corpses(void);
extern bool subproc_start(int slot);
extern int subproc_create(const char *name, const char *path, const char **argv, int argc);

#endif
#endif	// !defined(_subproc_h)
