//
// network.h
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if	!defined(__rr_network_h)
#define	__rr_network_h
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern void show_network_info(void);

#endif	// !defined(__rr_network_h)
