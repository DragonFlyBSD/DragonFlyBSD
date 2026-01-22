#ifndef _NETINET_UDP_H_
#define _NETINET_UDP_H_

#include <sys/types.h>

struct udphdr {
	u_short	uh_sport;
	u_short	uh_dport;
	u_short	uh_ulen;
	u_short	uh_sum;
};

#endif
