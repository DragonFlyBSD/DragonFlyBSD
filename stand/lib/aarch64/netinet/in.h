#ifndef _NETINET_IN_H_
#define _NETINET_IN_H_

#include <sys/types.h>

typedef u_int32_t in_addr_t;
typedef u_int16_t in_port_t;

struct in_addr {
	in_addr_t s_addr;
};

struct sockaddr_in {
	__uint8_t	sin_len;
	__uint8_t	sin_family;
	in_port_t	sin_port;
	struct in_addr	sin_addr;
	__uint8_t	sin_zero[8];
};

/* Internet address constants */
#define	INADDR_ANY		((__uint32_t)0x00000000)
#define	INADDR_BROADCAST	((__uint32_t)0xffffffff)
#define	INADDR_NONE		((__uint32_t)0xffffffff)

/* IP protocols */
#define	IPPROTO_IP		0
#define	IPPROTO_ICMP		1
#define	IPPROTO_TCP		6
#define	IPPROTO_UDP		17

#endif
