#define IFCAP_TSO4		0x00100	/* can do TCP Segmentation Offload */
#define IFCAP_TSO6		0x00200	/* can do TCP6 Segmentation Offload */
#define IFCAP_LRO		0x00400	/* can do Large Receive Offload */
#define IFCAP_WOL_UCAST		0x00800	/* wake on any unicast frame */
#define IFCAP_WOL_MCAST		0x01000	/* wake on any multicast frame */
#define IFCAP_WOL_MAGIC		0x02000	/* wake on any Magic Packet */
#define IFCAP_TOE4		0x04000	/* interface can offload TCP */
#define IFCAP_TOE6		0x08000	/* interface can offload TCP6 */
#define IFCAP_VLAN_HWFILTER	0x10000 /* interface hw can filter vlan tag */
#define IFCAP_POLLING_NOCOUNT	0x20000 /* polling ticks cannot be fragmented */
#define IFCAP_VLAN_HWTSO	0x40000 /* can do IFCAP_TSO on VLANs */

#define IFCAP_TSO	(IFCAP_TSO4 | IFCAP_TSO6)
#define IFCAP_WOL	(IFCAP_WOL_UCAST | IFCAP_WOL_MCAST | IFCAP_WOL_MAGIC)
#define IFCAP_TOE	(IFCAP_TOE4 | IFCAP_TOE6)
