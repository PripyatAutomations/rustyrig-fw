#if	!defined(_network_h)
#define	_network_h
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern void show_network_info(void);
extern void net_print_listeners(const char *listenaddr);

#endif	// !defined(_network_h)