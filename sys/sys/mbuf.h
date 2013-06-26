/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mbuf.h	8.5 (Berkeley) 2/19/95
 * $FreeBSD: src/sys/sys/mbuf.h,v 1.44.2.17 2003/04/15 06:15:02 silby Exp $
 * $DragonFly: src/sys/sys/mbuf.h,v 1.54 2008/10/19 08:39:55 sephe Exp $
 */

#ifndef _SYS_MBUF_H_
#define	_SYS_MBUF_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif
#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _NET_NETISR_H_
#include <net/netisr.h>
#endif
#ifndef _NET_ETHERNET_H_
#include <net/ethernet.h>
#endif

/*
 * Mbufs are of a single size, MSIZE (machine/param.h), which
 * includes overhead.  An mbuf may add a single "mbuf cluster" of size
 * MCLBYTES (also in machine/param.h), which has no additional overhead
 * and is used instead of the internal data area; this is done when
 * at least MINCLSIZE of data must be stored.
 */
#define	MLEN		(MSIZE - sizeof(struct m_hdr))	/* normal data len */
#define	MHLEN		(MLEN - sizeof(struct pkthdr))	/* data len w/pkthdr */
#define	MINCLSIZE	(MHLEN + 1)	/* smallest amount to put in cluster */
#define	M_MAXCOMPRESS	(MHLEN / 2)	/* max amount to copy for compression */

/*
 * Macros for type conversion:
 * mtod(m, t)		-- Convert mbuf pointer to data pointer of correct type.
 * mtodoff(m, t, off)	-- Convert mbuf pointer at the specified offset to data
 *			   pointer of correct type.
 */
#define	mtod(m, t)		((t)((m)->m_data))
#define	mtodoff(m, t, off)	((t)((m)->m_data + (off)))

/*
 * Header present at the beginning of every mbuf.
 */
struct m_hdr {
	struct	mbuf *mh_next;		/* next buffer in chain */
	struct	mbuf *mh_nextpkt;	/* next chain in queue/record */
	caddr_t	mh_data;		/* location of data */
	int	mh_len;			/* amount of data in this mbuf */
	int	mh_flags;		/* flags; see below */
	short	mh_type;		/* type of data in this mbuf */
	short	mh_pad;			/* padding */
	/* XXX implicit 4 bytes padding on x86_64 */
#ifdef MBUF_DEBUG
	const char *mh_lastfunc;
#endif
	union {
		struct netmsg_packet mhm_pkt;	/* hardware->proto stack msg */
		struct netmsg_pru_send mhm_snd;	/* usrspace->proto stack msg */
		struct netmsg_inarp mhm_arp;	/* proto stack<->route msg */
	} mh_msgu;
};
#define mh_netmsg	mh_msgu.mhm_pkt
#define mh_sndmsg	mh_msgu.mhm_snd
#define mh_arpmsg	mh_msgu.mhm_arp

/* pf stuff */
struct pkthdr_pf {
	void		*hdr;		/* saved hdr pos in mbuf, for ECN */
	void		*statekey;	/* pf stackside statekey */
	u_int		rtableid;	/* alternate routing table id */
	uint32_t	qid;		/* queue id */
	uint16_t	tag;		/* tag id */
	uint8_t		flags;
	uint8_t		routed;
	uint32_t	state_hash;	/* identifies 'connections' */
	uint8_t		ecn_af;		/* for altq_red */
	uint8_t		unused01;
	uint8_t		unused02;
	uint8_t		unused03;
	/* XXX implicit 4 bytes padding on x86_64 */
};

/* pkthdr_pf.flags */
#define	PF_TAG_GENERATED		0x01
#define	PF_TAG_FRAGCACHE		0x02
#define	PF_TAG_TRANSLATE_LOCALHOST	0x04
#define	PF_TAG_DIVERTED			0x08
#define	PF_TAG_DIVERTED_PACKET		0x10
#define	PF_TAG_REROUTE			0x20

/*
 * Packet tag structure (see below for details).
 */
struct m_tag {
	SLIST_ENTRY(m_tag)	m_tag_link;	/* List of packet tags */
	uint16_t		m_tag_id;	/* Tag ID */
	uint16_t		m_tag_len;	/* Length of data */
	uint32_t		m_tag_cookie;	/* ABI/Module ID */
};

SLIST_HEAD(packet_tags, m_tag);

/*
 * Record/packet header in first mbuf of chain; valid only if M_PKTHDR is set.
 *
 * Be careful: The fields have been carefully ordered to avoid hidden padding.
 *             Keep this in mind, when adding or removing fields!
 */
struct pkthdr {
	struct	ifnet *rcvif;		/* rcv interface */
	struct packet_tags tags;	/* list of packet tags */

	/* variables for ip and tcp reassembly */
	void	*header;		/* pointer to packet header */
	int	len;			/* total packet length */

	/* variables for hardware checksum */
	int	csum_flags;		/* flags regarding checksum */
	int	csum_data;		/* data field used by csum routines */
	uint16_t csum_iphlen;		/* IP header length */
					/* valid if CSUM IP|UDP|TCP|TSO */
	uint8_t	csum_thlen;		/* TCP/UDP header length */
					/* valid if CSUM UDP|TCP|TSO */
	uint8_t	csum_lhlen;		/* link header length */

	uint16_t tso_segsz;		/* TSO segment size */
	uint16_t ether_vlantag;		/* ethernet 802.1p+q vlan tag */

	uint16_t hash;			/* packet hash */
	/*
	 * Valid if BRIDGE_MBUF_TAGGED is set in fw_flags, records
	 * the original ether source address (if compatible).
	 */
	uint8_t ether_br_shost[ETHER_ADDR_LEN];

	/* firewall flags */
	uint32_t fw_flags;		/* flags for FW */

	/* variables for PF processing */
	struct pkthdr_pf pf;		/* structure for PF */
};

/*
 * Description of external storage mapped into mbuf; valid only if M_EXT is set.
 */
struct m_ext {
	caddr_t	ext_buf;		/* start of buffer */
	void	(*ext_free)(void *);
	u_int	ext_size;		/* size of buffer, for ext_free */
	void	(*ext_ref)(void *);
	void	*ext_arg;
};

/*
 * The core of the mbuf object along with some shortcut defines for
 * practical purposes.
 */
struct mbuf {
	struct	m_hdr m_hdr;
	union {
		struct {
			struct	pkthdr MH_pkthdr;	/* M_PKTHDR set */
			union {
				struct	m_ext MH_ext;	/* M_EXT set */
				char	MH_databuf[MHLEN];
			} MH_dat;
		} MH;
		char	M_databuf[MLEN];		/* !M_PKTHDR, !M_EXT */
	} M_dat;
};
#define	m_next		m_hdr.mh_next
#define	m_len		m_hdr.mh_len
#define	m_data		m_hdr.mh_data
#define	m_type		m_hdr.mh_type
#define	m_flags		m_hdr.mh_flags
#define	m_nextpkt	m_hdr.mh_nextpkt
#define	m_pkthdr	M_dat.MH.MH_pkthdr
#define	m_ext		M_dat.MH.MH_dat.MH_ext
#define	m_pktdat	M_dat.MH.MH_dat.MH_databuf
#define	m_dat		M_dat.M_databuf

/*
 * Code that uses m_act should be converted to use m_nextpkt
 * instead; m_act is historical and deprecated.
 */
#define m_act   	m_nextpkt

/*
 * mbuf flags.
 */
#define	M_EXT		0x0001	/* has associated external storage */
#define	M_PKTHDR	0x0002	/* start of record */
#define	M_EOR		0x0004	/* end of record */
#define	M_PROTO1	0x0008	/* protocol-specific */
#define	M_PROTO2	0x0010	/* protocol-specific */
#define	M_PROTO3	0x0020	/* protocol-specific */
#define	M_PROTO4	0x0040	/* protocol-specific */
#define	M_PROTO5	0x0080	/* protocol-specific */

/*
 * mbuf pkthdr flags (also stored in m_flags).
 */
#define	M_BCAST		0x0100	/* send/received as link-level broadcast */
#define	M_MCAST		0x0200	/* send/received as link-level multicast */
#define	M_FRAG		0x0400	/* packet is a fragment of a larger packet */
#define	M_FIRSTFRAG	0x0800	/* packet is first fragment */
#define	M_LASTFRAG	0x1000	/* packet is last fragment */
#define	M_CLCACHE	0x2000	/* mbuf allocated from the cluster cache */
#define M_EXT_CLUSTER	0x4000	/* standard cluster else special */
#define	M_PHCACHE	0x8000	/* mbuf allocated from the pkt header cache */
#define M_NOTIFICATION	0x10000	/* notification event */
#define M_VLANTAG	0x20000	/* ether_vlantag is valid */
#define M_MPLSLABELED	0x40000	/* packet is mpls labeled */
#define M_LENCHECKED	0x80000	/* packet proto lengths are checked */
#define M_HASH		0x100000/* hash field in pkthdr is valid */
#define M_PROTO6        0x200000/* protocol-specific */
#define M_PROTO7        0x400000/* protocol-specific */
#define M_PROTO8        0x800000/* protocol-specific */
#define M_CKHASH	0x1000000/* hash needs software verification */
#define M_PRIO		0x2000000/* high priority mbuf */

/*
 * Flags copied when copying m_pkthdr.
 */
#define	M_COPYFLAGS	(M_PKTHDR|M_EOR|M_PROTO1|M_PROTO2|M_PROTO3 | \
			 M_PROTO4|M_PROTO5|M_PROTO6|M_PROTO7|M_PROTO8 | \
			 M_BCAST|M_MCAST|M_FRAG|M_FIRSTFRAG|M_LASTFRAG | \
			 M_VLANTAG|M_MPLSLABELED | \
			 M_LENCHECKED|M_HASH|M_CKHASH|M_PRIO)

/*
 * Flags indicating hw checksum support and sw checksum requirements.
 */
#define	CSUM_IP			0x0001		/* will csum IP */
#define	CSUM_TCP		0x0002		/* will csum TCP */
#define	CSUM_UDP		0x0004		/* will csum UDP */
#define	CSUM_IP_FRAGS		0x0008		/* will csum IP fragments */
#define	CSUM_FRAGMENT		0x0010		/* will do IP fragmentation */

#define	CSUM_IP_CHECKED		0x0100		/* did csum IP */
#define	CSUM_IP_VALID		0x0200		/*   ... the csum is valid */
#define	CSUM_DATA_VALID		0x0400		/* csum_data field is valid */
#define	CSUM_PSEUDO_HDR		0x0800		/* csum_data has pseudo hdr */
#define CSUM_FRAG_NOT_CHECKED	0x1000		/* did _not_ csum fragment
						 * NB: This flag is only used
						 * by IP defragmenter.
						 */
#define CSUM_TSO		0x2000		/* will do TCP segmentation */

#define	CSUM_DELAY_DATA		(CSUM_TCP | CSUM_UDP)
#define	CSUM_DELAY_IP		(CSUM_IP)	/* XXX add ipv6 here too? */

/*
 * Flags indicating PF processing status
 */
#define FW_MBUF_GENERATED	0x00000001
#define	PF_MBUF_STRUCTURE	0x00000002	/* m_pkthdr.pf valid */
#define	PF_MBUF_ROUTED		0x00000004	/* pf_routed field is valid */
#define	PF_MBUF_TAGGED		0x00000008
#define	XX_MBUF_UNUSED10	0x00000010
#define	XX_MBUF_UNUSED20	0x00000020
#define IPFORWARD_MBUF_TAGGED	0x00000040
#define DUMMYNET_MBUF_TAGGED	0x00000080
#define BRIDGE_MBUF_TAGGED	0x00000100
#define FW_MBUF_REDISPATCH	0x00000200
#define	IPFW_MBUF_GENERATED	FW_MBUF_GENERATED
/*
 * mbuf types.
 */
#define	MT_FREE		0	/* should be on free list */
#define	MT_DATA		1	/* dynamic (data) allocation */
#define	MT_HEADER	2	/* packet header */
#define	MT_SONAME	3	/* socket name */
/* 4 was MT_TAG */
#define	MT_CONTROL	5	/* extra-data protocol message */
#define	MT_OOBDATA	6	/* expedited data  */
#define	MT_NTYPES	7	/* number of mbuf types for mbtypes[] */

/*
 * General mbuf allocator statistics structure.
 *
 * NOTE: Make sure this struct's size is multiple cache line size.
 */
struct mbstat {
	u_long	m_mbufs;	/* mbufs obtained from page pool */
	u_long	m_clusters;	/* clusters obtained from page pool */
	u_long	m_spare;	/* spare field */
	u_long	m_clfree;	/* free clusters */
	u_long	m_drops;	/* times failed to find space */
	u_long	m_wait;		/* times waited for space */
	u_long	m_drain;	/* times drained protocols for space */
	u_long	m_mcfail;	/* times m_copym failed */
	u_long	m_mpfail;	/* times m_pullup failed */
	u_long	m_msize;	/* length of an mbuf */
	u_long	m_mclbytes;	/* length of an mbuf cluster */
	u_long	m_mjumpagesize;	/* length of a jumbo mbuf cluster */
	u_long	m_minclsize;	/* min length of data to allocate a cluster */
	u_long	m_mlen;		/* length of data in an mbuf */
	u_long	m_mhlen;	/* length of data in a header mbuf */
	u_long	m_pad;		/* pad to cache line size (64B) */
};

/*
 * Flags specifying how an allocation should be made.
 */

#define	MB_DONTWAIT	0x4
#define	MB_TRYWAIT	0x8
#define	MB_WAIT		MB_TRYWAIT

/*
 * Mbuf to Malloc Flag Conversion.
 */
#define	MBTOM(how)	((how) & MB_TRYWAIT ? M_WAITOK : M_NOWAIT)

/*
 * These are identifying numbers passed to the m_mballoc_wait function,
 * allowing us to determine whether the call came from an MGETHDR or
 * an MGET.
 */
#define	MGETHDR_C      1
#define	MGET_C         2

/*
 * mbuf allocation/deallocation macros (YYY deprecated, too big):
 *
 *	MGET(struct mbuf *m, int how, int type)
 * allocates an mbuf and initializes it to contain internal data.
 *
 *	MGETHDR(struct mbuf *m, int how, int type)
 * allocates an mbuf and initializes it to contain a packet header
 * and internal data.
 */
#define	MGET(m, how, type) do {						\
	(m) = m_get((how), (type));					\
} while (0)

#define	MGETHDR(m, how, type) do {					\
	(m) = m_gethdr((how), (type));					\
} while (0)

/*
 * MCLGET adds such clusters to a normal mbuf.  The flag M_EXT is set upon
 * success.
 * Deprecated.  Use m_getcl() or m_getl() instead.
 */
#define	MCLGET(m, how) do {						\
	m_mclget((m), (how));						\
} while (0)

/*
 * NB: M_COPY_PKTHDR is deprecated; use either M_MOVE_PKTHDR
 *     or m_dup_pkthdr.
 */
/*
 * Move mbuf pkthdr from "from" to "to".
 * from should have M_PKTHDR set, and to must be empty.
 * from no longer has a pkthdr after this operation.
 */
#define	M_MOVE_PKTHDR(_to, _from)	m_move_pkthdr((_to), (_from))

/*
 * Set the m_data pointer of a newly-allocated mbuf (m_get/MGET) to place
 * an object of the specified size at the end of the mbuf, longword aligned.
 */
#define	M_ALIGN(m, len) do {						\
	(m)->m_data += (MLEN - (len)) & ~(sizeof(long) - 1);		\
} while (0)

/*
 * As above, for mbufs allocated with m_gethdr/MGETHDR
 * or initialized by M_COPY_PKTHDR.
 */
#define	MH_ALIGN(m, len) do {						\
	(m)->m_data += (MHLEN - (len)) & ~(sizeof(long) - 1);		\
} while (0)

/*
 * Check if we can write to an mbuf.
 */
#define M_EXT_WRITABLE(m)	(m_sharecount(m) == 1)
#define M_WRITABLE(m)		(!((m)->m_flags & M_EXT) || M_EXT_WRITABLE(m))

/*
 * Check if the supplied mbuf has a packet header, or else panic.
 */
#define	M_ASSERTPKTHDR(m)						\
	KASSERT(m != NULL && m->m_flags & M_PKTHDR,			\
		("%s: invalid mbuf or no mbuf packet header!", __func__))

/*
 * Compute the amount of space available before the current start of data.
 * The M_EXT_WRITABLE() is a temporary, conservative safety measure: the burden
 * of checking writability of the mbuf data area rests solely with the caller.
 */
#define	M_LEADINGSPACE(m)						\
	((m)->m_flags & M_EXT ?						\
	    (M_EXT_WRITABLE(m) ? (m)->m_data - (m)->m_ext.ext_buf : 0):	\
	    (m)->m_flags & M_PKTHDR ? (m)->m_data - (m)->m_pktdat :	\
	    (m)->m_data - (m)->m_dat)

/*
 * Compute the amount of space available after the end of data in an mbuf.
 * The M_WRITABLE() is a temporary, conservative safety measure: the burden
 * of checking writability of the mbuf data area rests solely with the caller.
 */
#define	M_TRAILINGSPACE(m)						\
	((m)->m_flags & M_EXT ?						\
	    (M_WRITABLE(m) ? (m)->m_ext.ext_buf + (m)->m_ext.ext_size	\
		- ((m)->m_data + (m)->m_len) : 0) :			\
	    &(m)->m_dat[MLEN] - ((m)->m_data + (m)->m_len))

/*
 * Arrange to prepend space of size plen to mbuf m.
 * If a new mbuf must be allocated, how specifies whether to wait.
 * If how is MB_DONTWAIT and allocation fails, the original mbuf chain
 * is freed and m is set to NULL.
 */
#define	M_PREPEND(m, plen, how) do {					\
	struct mbuf **_mmp = &(m);					\
	struct mbuf *_mm = *_mmp;					\
	int _mplen = (plen);						\
	int __mhow = (how);						\
									\
	if (M_LEADINGSPACE(_mm) >= _mplen) {				\
		_mm->m_data -= _mplen;					\
		_mm->m_len += _mplen;					\
	} else								\
		_mm = m_prepend(_mm, _mplen, __mhow);			\
	if (_mm != NULL && (_mm->m_flags & M_PKTHDR))			\
		_mm->m_pkthdr.len += _mplen;				\
	*_mmp = _mm;							\
} while (0)

/* Length to m_copy to copy all. */
#define	M_COPYALL	1000000000

/* Compatibility with 4.3 */
#define	m_copy(m, o, l)	m_copym((m), (o), (l), MB_DONTWAIT)

#ifdef _KERNEL
extern	u_int		 m_clalloc_wid;	/* mbuf cluster wait count */
extern	u_int		 m_mballoc_wid;	/* mbuf wait count */
extern	int		 max_linkhdr;	/* largest link-level header */
extern	int		 max_protohdr;	/* largest protocol header */
extern	int		 max_hdr;	/* largest link+protocol header */
extern	int		 max_datalen;	/* MHLEN - max_hdr */
extern	int		 mbuf_wait;	/* mbuf sleep time */
extern	int		 nmbclusters;
extern	int		 nmbufs;

struct uio;

void		 m_adj(struct mbuf *, int);
void		 m_align(struct mbuf *, int);
int		 m_apply(struct mbuf *, int, int,
		    int (*)(void *, void *, u_int), void *);
int		m_append(struct mbuf *, int, c_caddr_t);
void		 m_cat(struct mbuf *, struct mbuf *);
u_int		 m_countm(struct mbuf *m, struct mbuf **lastm, u_int *mbcnt);
void		 m_copyback(struct mbuf *, int, int, caddr_t);
void		 m_copydata(const struct mbuf *, int, int, caddr_t);
struct	mbuf	*m_copym(const struct mbuf *, int, int, int);
struct	mbuf	*m_copypacket(struct mbuf *, int);
struct	mbuf	*m_defrag(struct mbuf *, int);
struct	mbuf	*m_defrag_nofree(struct mbuf *, int);
struct	mbuf	*m_devget(char *, int, int, struct ifnet *,
		  void (*copy)(volatile const void *, volatile void *, size_t));
struct	mbuf	*m_dup(struct mbuf *, int);
struct	mbuf	*m_dup_data(struct mbuf *, int);
int		 m_dup_pkthdr(struct mbuf *, const struct mbuf *, int);
void		 m_extadd(struct mbuf *, caddr_t, u_int, void (*)(void *),
		  void (*)(void *), void *);
#ifdef MBUF_DEBUG
struct	mbuf	*_m_free(struct mbuf *, const char *name);
void		 _m_freem(struct mbuf *, const char *name);
#else
struct	mbuf	*m_free(struct mbuf *);
void		 m_freem(struct mbuf *);
#endif
struct	mbuf	*m_get(int, int);
struct	mbuf	*m_getc(int len, int how, int type);
struct	mbuf	*m_getcl(int how, short type, int flags);
struct	mbuf	*m_getjcl(int how, short type, int flags, size_t size);
struct	mbuf	*m_getclr(int, int);
struct	mbuf	*m_gethdr(int, int);
struct	mbuf	*m_getm(struct mbuf *, int, int, int);
struct	mbuf	*m_getptr(struct mbuf *, int, int *);
struct	mbuf	*m_last(struct mbuf *m);
u_int		 m_lengthm(struct mbuf *m, struct mbuf **lastm);
void		 m_move_pkthdr(struct mbuf *, struct mbuf *);
struct	mbuf	*m_prepend(struct mbuf *, int, int);
void		 m_print(const struct mbuf *m);
struct	mbuf	*m_pulldown(struct mbuf *, int, int, int *);
struct	mbuf	*m_pullup(struct mbuf *, int);
struct	mbuf	*m_split(struct mbuf *, int, int);
struct	mbuf 	*m_uiomove(struct uio *);
struct	mbuf	*m_unshare(struct mbuf *, int);
void		m_mclget(struct mbuf *m, int how);
int		m_sharecount(struct mbuf *m);
void		m_chtype(struct mbuf *m, int type);
int		m_devpad(struct mbuf *m, int padto);

#ifdef MBUF_DEBUG

void		mbuftrackid(struct mbuf *, int);

#define m_free(m)	_m_free(m, __func__)
#define m_freem(m)	_m_freem(m, __func__)

#else

#define mbuftrackid(m, id)	/* empty */

#endif

/*
 * Allocate the right type of mbuf for the desired total length.
 * The mbuf returned does not necessarily cover the entire requested length.
 * This function follows mbuf chaining policy of allowing MINCLSIZE
 * amount of chained mbufs.
 */
static __inline struct mbuf *
m_getl(int len, int how, int type, int flags, int *psize)
{
	struct mbuf *m;
	int size;

	if (len >= MINCLSIZE) {
		m = m_getcl(how, type, flags);
		size = MCLBYTES;
	} else if (flags & M_PKTHDR) {
		m = m_gethdr(how, type);
		size = MHLEN;
	} else {
		m = m_get(how, type);
		size = MLEN;
	}
	if (psize != NULL)
		*psize = size;
	return (m);
}

/*
 * Get a single mbuf that covers the requested number of bytes.
 * This function does not create mbuf chains.  It explicitly marks
 * places in the code that abuse mbufs for contiguous data buffers.
 */
static __inline struct mbuf *
m_getb(int len, int how, int type, int flags)
{
	struct mbuf *m;
	int mbufsize = (flags & M_PKTHDR) ? MHLEN : MLEN;

	if (len > mbufsize)
		m = m_getcl(how, type, flags);
	else if (flags & M_PKTHDR)
		m = m_gethdr(how, type);
	else
		m = m_get(how, type);
	return (m);
}

/*
 * Packets may have annotations attached by affixing a list
 * of "packet tags" to the pkthdr structure.  Packet tags are
 * dynamically allocated semi-opaque data structures that have
 * a fixed header (struct m_tag) that specifies the size of the
 * memory block and a <cookie,type> pair that identifies it.
 * The cookie is a 32-bit unique unsigned value used to identify
 * a module or ABI.  By convention this value is chose as the
 * date+time that the module is created, expressed as the number of
 * seconds since the epoch (e.g. using date -u +'%s').  The type value
 * is an ABI/module-specific value that identifies a particular annotation
 * and is private to the module.  For compatibility with systems
 * like openbsd that define packet tags w/o an ABI/module cookie,
 * the value PACKET_ABI_COMPAT is used to implement m_tag_get and
 * m_tag_find compatibility shim functions and several tag types are
 * defined below.  Users that do not require compatibility should use
 * a private cookie value so that packet tag-related definitions
 * can be maintained privately.
 *
 * Note that the packet tag returned by m_tag_alloc has the default
 * memory alignment implemented by kmalloc.  To reference private data
 * one can use a construct like:
 *
 *	struct m_tag *mtag = m_tag_alloc(...);
 *	struct foo *p = m_tag_data(mtag);
 *
 * if the alignment of struct m_tag is sufficient for referencing members
 * of struct foo.  Otherwise it is necessary to embed struct m_tag within
 * the private data structure to insure proper alignment; e.g.
 *
 *	struct foo {
 *		struct m_tag	tag;
 *		...
 *	};
 *	struct foo *p = (struct foo *)m_tag_alloc(...);
 *	struct m_tag *mtag = &p->tag;
 */

#define	PACKET_TAG_NONE				0  /* Nadda */

/* Packet tag for use with PACKET_ABI_COMPAT */
#define	PACKET_TAG_IPSEC_IN_DONE		1  /* IPsec applied, in */
/* struct tdb_indent */
#define	PACKET_TAG_IPSEC_OUT_DONE		2  /* IPsec applied, out */
/* struct tdb_indent */
#define	PACKET_TAG_IPSEC_IN_CRYPTO_DONE		3  /* NIC IPsec crypto done */
/* struct tdb_indent, never added */
#define	PACKET_TAG_IPSEC_OUT_CRYPTO_NEEDED	4  /* NIC IPsec crypto req'ed */
/* struct tdb_indent, never added */
#define	PACKET_TAG_IPSEC_PENDING_TDB		5  /* Reminder to do IPsec */
/* struct tdb_indent, never added */
#define	PACKET_TAG_ENCAP			6 /* Encap.  processing */
/* struct ifnet *, the GIF interface */
#define	PACKET_TAG_IPSEC_HISTORY		7 /* IPSEC history */
/* struct ipsec_history */
#define	PACKET_TAG_IPV6_INPUT			8 /* IPV6 input processing */
/* struct ip6aux */
#define	PACKET_TAG_IPFW_DIVERT			9 /* divert info */
/* struct divert_info */
#define	PACKET_TAG_DUMMYNET			15 /* dummynet info */
/* struct dn_pkt */
#define	PACKET_TAG_IPFORWARD			18 /* ipforward info */
/* struct sockaddr_in */
#define PACKET_TAG_IPSRCRT			27 /* IP srcrt opts */
/* struct ip_srcrt_opt */
#define	PACKET_TAG_CARP                         28 /* CARP info */
/* struct pf_mtag */
#define PACKET_TAG_PF				29 /* PF info */

#define PACKET_TAG_PF_DIVERT			0x0200 /* pf(4) diverted packet */
	

/* Packet tag routines */
struct	m_tag 	*m_tag_alloc(uint32_t, int, int, int);
void		 m_tag_free(struct m_tag *);
void		 m_tag_prepend(struct mbuf *, struct m_tag *);
void		 m_tag_unlink(struct mbuf *, struct m_tag *);
void		 m_tag_delete(struct mbuf *, struct m_tag *);
void		 m_tag_delete_chain(struct mbuf *);
struct	m_tag	*m_tag_locate(struct mbuf *, uint32_t, int, struct m_tag *);
struct	m_tag	*m_tag_copy(struct m_tag *, int);
int		 m_tag_copy_chain(struct mbuf *, const struct mbuf *, int);
void		 m_tag_init(struct mbuf *);
struct	m_tag	*m_tag_first(struct mbuf *);
struct	m_tag	*m_tag_next(struct mbuf *, struct m_tag *);

/* these are for openbsd compatibility */
#define	MTAG_ABI_COMPAT		0		/* compatibility ABI */

static __inline void *
m_tag_data(struct m_tag *tag)
{
	return ((void *)(tag + 1));
}

static __inline struct m_tag *
m_tag_get(int type, int length, int wait)
{
	return m_tag_alloc(MTAG_ABI_COMPAT, type, length, wait);
}

static __inline struct m_tag *
m_tag_find(struct mbuf *m, int type, struct m_tag *start)
{
	return m_tag_locate(m, MTAG_ABI_COMPAT, type, start);
}

#endif	/* _KERNEL */

#endif	/* _KERNEL || _KERNEL_STRUCTURES */
#endif	/* !_SYS_MBUF_H_ */
