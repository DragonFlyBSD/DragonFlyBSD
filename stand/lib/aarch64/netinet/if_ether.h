#ifndef _NETINET_IF_ETHER_H_
#define _NETINET_IF_ETHER_H_

#include <sys/types.h>
#include <net/if.h>
#include <netinet/in.h>

#define ETHER_ADDR_LEN		6
#define ETHER_TYPE_LEN		2
#define ETHER_CRC_LEN		4
#define ETHER_HDR_LEN		(ETHER_ADDR_LEN*2+ETHER_TYPE_LEN)
#define ETHER_MIN_LEN		64
#define ETHER_MAX_LEN		1518

#define	ETHERTYPE_IP		0x0800
#define	ETHERTYPE_ARP		0x0806
#define	ETHERTYPE_REVARP	0x8035
#define	ETHERTYPE_IPV6		0x86dd

struct ether_header {
	u_char	ether_dhost[ETHER_ADDR_LEN];
	u_char	ether_shost[ETHER_ADDR_LEN];
	u_short	ether_type;
};

struct ether_addr {
	u_char octet[ETHER_ADDR_LEN];
};

/* ARP protocol */
#define ARPHRD_ETHER	1

#define ARPOP_REQUEST	1
#define ARPOP_REPLY	2
#define ARPOP_REVREQUEST	3
#define ARPOP_REVREPLY	4

struct arphdr {
	u_short	ar_hrd;
	u_short	ar_pro;
	u_char	ar_hln;
	u_char	ar_pln;
	u_short	ar_op;
};

struct ether_arp {
	struct	arphdr ea_hdr;
	u_char	arp_sha[ETHER_ADDR_LEN];
	u_char	arp_spa[4];
	u_char	arp_tha[ETHER_ADDR_LEN];
	u_char	arp_tpa[4];
};
#define	arp_hrd	ea_hdr.ar_hrd
#define	arp_pro	ea_hdr.ar_pro
#define	arp_hln	ea_hdr.ar_hln
#define	arp_pln	ea_hdr.ar_pln
#define	arp_op	ea_hdr.ar_op

#endif
