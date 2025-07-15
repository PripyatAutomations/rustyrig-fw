#if	!defined(__rr_config_paths_h)
#define __rr_config_paths_h

static const char *configs[] = { 
#if	!defined(__RRCLIENT)
   "~/.config/rrserver.cfg",
   "config/rrserver.cfg",
   "rrserver.cfg",
   "/etc/rustyrig/rrserver.cfg"
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
