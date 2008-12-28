/*
 * Copyright (c) 2000 Paycounter, Inc.
 * Author: Alfred Perlstein <alfred@paycounter.com>, <alfred@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD: src/sys/netinet/accf_http.c,v 1.1.2.4 2002/05/01 08:34:37 alfred Exp $
 *	$DragonFly: src/sys/net/accf_http/accf_http.c,v 1.4 2007/04/22 01:13:13 dillon Exp $
 */

#define ACCEPT_FILTER_MOD

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/malloc.h> 
#include <sys/unistd.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/mbuf.h>
#include <sys/resource.h>
#include <sys/sysent.h>
#include <sys/resourcevar.h>

static void sohashttpget(struct socket *so, void *arg, int waitflag);
static void soparsehttpvers(struct socket *so, void *arg, int waitflag);
static void soishttpconnected(struct socket *so, void *arg, int waitflag);
static int mbufstrcmp(struct sb_reader *sr, struct mbuf *m,
			int offset, char *cmp);
static int mbufstrncmp(struct sb_reader *sr, struct mbuf *m,
			int offset, int max, char *cmp);
static int sbfull(struct signalsockbuf *sb);

static struct accept_filter accf_http_filter = {
	"httpready",
	sohashttpget,
	NULL,
	NULL
};

static moduledata_t accf_http_mod = {
	"accf_http",
	accept_filt_generic_mod_event,
	&accf_http_filter
};

DECLARE_MODULE(accf_http, accf_http_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

static int parse_http_version = 1;

SYSCTL_NODE(_net_inet_accf, OID_AUTO, http, CTLFLAG_RW, 0,
"HTTP accept filter");
SYSCTL_INT(_net_inet_accf_http, OID_AUTO, parsehttpversion, CTLFLAG_RW,
&parse_http_version, 1,
"Parse http version so that non 1.x requests work");

#ifdef ACCF_HTTP_DEBUG
#define DPRINT(fmt, args...) \
	do {	\
		kprintf("%s:%d: " fmt "\n", __func__, __LINE__ , ##args);	\
	} while (0)
#else
#define DPRINT(fmt, args...)
#endif

static int
sbfull(struct signalsockbuf *ssb)
{

	/*
	 * TBD: properly -- agg
	 */
	return (sb_cc_est(&ssb->sb) >= ssb->ssb_hiwat ||
		sb_mbcnt_est(&ssb->sb) >= ssb->ssb_mbmax);
}

/*
 * starting at offset in sr, compare characters for 'cmp'
 */
static int
mbufstrcmp(struct sb_reader *sr, struct mbuf *m, int offset, char *cmp)
{
	struct sb_reader srcopy;
	int r = 0;

	sb_reader_copy(sr, &srcopy);
	while (m) {
		for (; offset < m->m_len; offset++, cmp++) {
			if (*cmp == '\0') {
				r = 1;
				break;
			}
			if (*cmp != *(mtod(m, char *) + offset))
				break;
		}
		if (*cmp == '\0') {
			r = 1;
			break;
		}
		m = sb_reader_next(&srcopy, NULL);
		offset = 0;
	}
	sb_reader_done(&srcopy);
	return (r);
}

/*
 * starting at offset in sr, compare characters in mbuf for 'cmp';
 * stop at 'max' characters
 */
static int
mbufstrncmp(struct sb_reader *sr, struct mbuf *m,
	    int offset, int max, char *cmp)
{
	struct sb_reader srcopy;
	int r = 0;

	while (m) {
		for (; offset < m->m_len; offset++, cmp++, max--) {
			if (max == 0 || *cmp == '\0') {
				r = 1;
				break;
			}
			if (*cmp != *(mtod(m, char *) + offset))
				break;
		}
		if (max == 0 || *cmp == '\0') {
			r = 1;
			break;
		}
		m = sb_reader_next(&srcopy, NULL);
		offset = 0;
	}
	sb_reader_done(&srcopy);
	return (r);
}

#define STRSETUP(sptr, slen, str) \
	do {	\
		sptr = str;	\
		slen = sizeof(str) - 1;	\
	} while(0)

/*
 * check for GET/HEAD
 */
static void
sohashttpget(struct socket *so, void *arg, int waitflag)
{
	struct sb_reader sr;

	if ((so->so_state & SS_CANTRCVMORE) == 0 && !sbfull(&so->so_rcv)) {
		struct mbuf *m;
		char *cmp;
		int	cmplen, cc;

		cc = sb_cc_est(&so->so_rcv.sb) - 1;
		sb_reader_init(&sr, &so->so_rcv.sb);
		m = sb_reader_next(&sr, NULL);
		if (cc < 1) {
			sb_reader_done(&sr);
			return;
		}
		switch (*mtod(m, char *)) {
		case 'G':
			STRSETUP(cmp, cmplen, "ET ");
			break;
		case 'H':
			STRSETUP(cmp, cmplen, "EAD ");
			break;
		default:
			sb_reader_done(&sr);
			goto fallout;
		}
		if (cc < cmplen) {
			if (mbufstrncmp(&sr, m, 1, cc, cmp) == 1) {
				DPRINT("short cc (%d) but mbufstrncmp ok", cc);
				sb_reader_done(&sr);
				return;
			} else {
				DPRINT("short cc (%d) mbufstrncmp failed", cc);
				sb_reader_done(&sr);
				goto fallout;
			}
		}
		if (mbufstrcmp(&sr, m, 1, cmp) == 1) {
			DPRINT("mbufstrcmp ok");
			sb_reader_done(&sr);
			if (parse_http_version == 0)
				soishttpconnected(so, arg, waitflag);
			else
				soparsehttpvers(so, arg, waitflag);
			return;
		}
		DPRINT("mbufstrcmp bad");
	}

fallout:
	DPRINT("fallout");
	so->so_upcall = NULL;
	so->so_rcv.ssb_flags &= ~SSB_UPCALL;
	soisconnected(so);
	return;
}

/*
 * check for HTTP/1.0 or HTTP/1.1
 */
static void
soparsehttpvers(struct socket *so, void *arg, int waitflag)
{
	struct sb_reader sr;
	struct mbuf *m;
	int	i, cc, spaces, inspaces;

	if ((so->so_state & SS_CANTRCVMORE) != 0 || sbfull(&so->so_rcv))
		goto fallout;

	sb_reader_init(&sr, &so->so_rcv.sb);
	m = sb_reader_next(&sr, NULL);
	cc = sb_cc_est(&so->so_rcv.sb);
	inspaces = spaces = 0;

	while (m) {
		for (i = 0; i < m->m_len; i++, cc--) {
			switch (*(mtod(m, char *) + i)) {
			case ' ':
				if (!inspaces) {
					spaces++;
					inspaces = 1;
				}
				break;
			case '\r':
			case '\n':
				DPRINT("newline");
				goto fallout;
			default:
				if (spaces == 2) {
					/* make sure we have enough data left */
					if (cc < sizeof("HTTP/1.0") - 1) {
						if (mbufstrncmp(&sr, m, i, cc, "HTTP/1.") == 1) {
							DPRINT("mbufstrncmp ok");
							goto readmore;
						} else {
							DPRINT("mbufstrncmp bad");
							goto fallout;
						}
					} else if (mbufstrcmp(&sr, m, i, "HTTP/1.0") == 1 ||
						   mbufstrcmp(&sr, m, i, "HTTP/1.1") == 1) {
							DPRINT("mbufstrcmp ok");
							soishttpconnected(so, arg, waitflag);
							return;
					} else {
						DPRINT("mbufstrcmp bad");
						goto fallout;
					}
				}
				inspaces = 0;
				break;
			}
		}
		m = sb_reader_next(&sr, NULL);
	}
readmore:
	DPRINT("readmore");
	/*
	 * if we hit here we haven't hit something
	 * we don't understand or a newline, so try again
	 */
	sb_reader_done(&sr);
	so->so_upcall = soparsehttpvers;
	so->so_rcv.ssb_flags |= SSB_UPCALL;
	return;

fallout:
	DPRINT("fallout");
	sb_reader_done(&sr);
	so->so_upcall = NULL;
	so->so_rcv.ssb_flags &= ~SSB_UPCALL;
	soisconnected(so);
	return;
}


#define NCHRS 3

/*
 * check for end of HTTP/1.x request
 */
static void
soishttpconnected(struct socket *so, void *arg, int waitflag)
{
	char a, b, c;
	struct mbuf *m;
	int ccleft, copied;
	struct sb_reader sr;

	DPRINT("start");
	if ((so->so_state & SS_CANTRCVMORE) != 0 || sbfull(&so->so_rcv))
		goto gotit;

	/*
	 * Walk the socketbuffer and copy the last NCHRS (3) into a, b, and c
	 * copied - how much we've copied so far
	 * ccleft - how many bytes remaining in the socketbuffer
	 * just loop over the mbufs subtracting from 'ccleft' until we only
	 * have NCHRS left
	 */
	copied = 0;
	ccleft = sb_cc_est(&so->so_rcv.sb);
	if (ccleft < NCHRS)
		goto readmore;
	a = b = c = '\0';

	sb_reader_init(&sr, &so->so_rcv.sb);
	m = sb_reader_next(&sr, NULL);
	while (m) {
		ccleft -= m->m_len;
		if (ccleft <= NCHRS) {
			char *src;
			int tocopy;

			tocopy = (NCHRS - ccleft) - copied;
			src = mtod(m, char *) + (m->m_len - tocopy);

			while (tocopy--) {
				switch (copied++) {
				case 0:
					a = *src++;
					break;
				case 1:
					b = *src++;
					break;
				case 2:
					c = *src++;
					break;
				}
			}
		}
		m = sb_reader_next(&sr, NULL);
	}
	if (c == '\n' && (b == '\n' || (b == '\r' && a == '\n'))) {
		/* we have all request headers */
		goto gotit;
	}

readmore:
	sb_reader_done(&sr);
	so->so_upcall = soishttpconnected;
	so->so_rcv.ssb_flags |= SSB_UPCALL;
	return;

gotit:
	sb_reader_done(&sr);
	so->so_upcall = NULL;
	so->so_rcv.ssb_flags &= ~SSB_UPCALL;
	soisconnected(so);
	return;
}
