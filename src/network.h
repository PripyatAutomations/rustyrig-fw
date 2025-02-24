#if	!defined(__rr_network_h)
#define	__rr_network_h
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern void show_network_info(void);
extern void net_print_listeners(const char *listenaddr);

#endif	// !defined(__rr_network_h)