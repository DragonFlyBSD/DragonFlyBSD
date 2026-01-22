#ifndef _NETINET_UDP_VAR_H_
#define _NETINET_UDP_VAR_H_

#include <netinet/ip.h>
#include <netinet/udp.h>

/*
 * UDP+IP header for checksum calculation
 */
struct udpiphdr {
	struct	in_addr ui_src;
	struct	in_addr ui_dst;
	u_char	ui_x1;		/* zero */
	u_char	ui_pr;		/* protocol */
	u_short	ui_len;		/* udp length */
	u_short	ui_sport;
	u_short	ui_dport;
	u_short	ui_ulen;
	u_short	ui_sum;
};

#endif
