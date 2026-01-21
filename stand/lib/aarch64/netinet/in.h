#ifndef _NETINET_IN_H_
#define _NETINET_IN_H_

#include <sys/types.h>

typedef u_int32_t in_addr_t;

struct in_addr {
	in_addr_t s_addr;
};

#endif
