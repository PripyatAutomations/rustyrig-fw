// rrserver/backend.hamlib.h
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if !defined(__rrserver_backend_hamlib_h)
#define __rrserver_backend_hamlib_h

#if defined(USE_HAMLIB)
extern const rr_backend_type_t rr_backend_hamlib;
#endif
extern void rr_backend_hamlib_register(void);

#endif // !defined(__rrserver_backend_hamlib_h)
