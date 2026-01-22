#ifndef _SYS_SOCKET_H_
#define _SYS_SOCKET_H_

#include <sys/types.h>

/* socklen_t */
typedef __uint32_t socklen_t;

/* Address families */
#define	AF_UNSPEC	0
#define	AF_UNIX		1
#define	AF_LOCAL	AF_UNIX
#define	AF_INET		2
#define	AF_INET6	28

/* Socket types */
#define	SOCK_STREAM	1
#define	SOCK_DGRAM	2
#define	SOCK_RAW	3

/* For sa_family_t */
typedef __uint8_t sa_family_t;

struct sockaddr {
	__uint8_t	sa_len;
	sa_family_t	sa_family;
	char		sa_data[14];
};

#endif
