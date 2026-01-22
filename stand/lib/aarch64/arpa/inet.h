#ifndef _ARPA_INET_H_
#define _ARPA_INET_H_

#include <sys/types.h>
#include <netinet/in.h>

/* Functions implemented in libstand */
char *inet_ntoa(struct in_addr);
in_addr_t inet_addr(const char *);
int inet_aton(const char *, struct in_addr *);

#endif
