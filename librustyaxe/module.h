#if	!defined(__librustyaxe_module_h)
#define __librustyaxe_module_h

typedef struct rr_module_event {
   const char             *evt_name;
   bool                  (*evt_callback)();
   struct rr_module_event *next;
} rr_module_event_t;

typedef struct rr_module {
  const char        *mod_path;
  const char        *mod_name;
  const char        *mod_description;
  const char        *mod_version;
  const char        *mod_copyright;
  rr_module_event_t *mod_events;		// events
  void              *dlptr;
  struct rr_module  *next;			// next in list
} rr_module_t;

#endif	// !defined(__librustyaxe_module_h)
