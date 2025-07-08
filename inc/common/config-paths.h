#if	!defined(__rr_config_paths_h)
#define __rr_config_paths_h

static const char *configs[] = { 
#if	!defined(__RRCLIENT)
   "/etc/rustyrig/rrserver.cfg"
   "~/.rustyrig/rrserver.cfg",
   "config/rrserver.cfg",
   "rrserver.cfg",
#else	// __RRCLIENT
#ifndef _WIN32
   "~/.config/rrclient.cfg",
   "~/.rrclient.cfg",
   "/etc/rrclient.cfg"
#else
   "%APPDATA%\\rrclient\\rrclient.cfg",
   ".\\rrclient.cfg"
#endif
#endif
};

#endif // !defined(__rr_config_paths_h)
