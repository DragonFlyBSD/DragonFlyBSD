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

/* IP address class macros */
#define	IN_CLASSA(i)		(((uint32_t)(i) & 0x80000000) == 0)
#define	IN_CLASSA_NET		0xff000000
#define	IN_CLASSA_NSHIFT	24
#define	IN_CLASSA_HOST		0x00ffffff
#define	IN_CLASSA_MAX		128

#define	IN_CLASSB(i)		(((uint32_t)(i) & 0xc0000000) == 0x80000000)
#define	IN_CLASSB_NET		0xffff0000
#define	IN_CLASSB_NSHIFT	16
#define	IN_CLASSB_HOST		0x0000ffff
#define	IN_CLASSB_MAX		65536

#define	IN_CLASSC(i)		(((uint32_t)(i) & 0xe0000000) == 0xc0000000)
#define	IN_CLASSC_NET		0xffffff00
#define	IN_CLASSC_NSHIFT	8
#define	IN_CLASSC_HOST		0x000000ff

#define	IN_CLASSD(i)		(((uint32_t)(i) & 0xf0000000) == 0xe0000000)

#endif
