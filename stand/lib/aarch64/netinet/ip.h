#ifndef _NETINET_IP_H_
#define _NETINET_IP_H_

#include <sys/types.h>
#include <netinet/in.h>

/* IP header */
struct ip {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	ip_hl:4,
		ip_v:4;
#else
	u_int	ip_v:4,
		ip_hl:4;
#endif
	u_char	ip_tos;
	u_short	ip_len;
	u_short	ip_id;
	u_short	ip_off;
#define	IP_RF 0x8000
#define	IP_DF 0x4000
#define	IP_MF 0x2000
#define	IP_OFFMASK 0x1fff
	u_char	ip_ttl;
	u_char	ip_p;
	u_short	ip_sum;
	struct	in_addr ip_src, ip_dst;
};

#define	IPVERSION	4

/* Max time to live (seconds) */
#define	MAXTTL		255
#define	IPDEFTTL	64

#endif
