/*
 * Copyright (c) 1983, 1988, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 * @(#)mbuf.c	8.1 (Berkeley) 6/6/93
 * $FreeBSD: src/usr.bin/netstat/mbuf.c,v 1.17.2.3 2001/08/10 09:07:09 ru Exp $
 * $DragonFly: src/usr.bin/netstat/mbuf.c,v 1.7 2006/10/09 22:30:48 hsu Exp $
 */

#define _KERNEL_STRUCTURES
#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include "netstat.h"

#define	YES	1
typedef int bool;

static struct mbtypenames {
	int	mt_type;
	const char *mt_name;
} mbtypenames[] = {
	{ MT_DATA,	"data" },
	{ MT_OOBDATA,	"oob data" },
	{ MT_CONTROL,	"ancillary data" },
	{ MT_HEADER,	"packet headers" },
	{ MT_SONAME,	"socket names and addresses" },
	{ 0, 0 }
};

/*
 * Print mbuf statistics.
 */
void
mbpr(u_long mbaddr, u_long mbtaddr, u_long nmbcaddr, u_long nmbjcaddr,
    u_long nmbufaddr, u_long ncpusaddr)
{
	u_long totmem, totpossible;
	struct mbstat *mbstat;
	struct mbtypenames *mp;
	int name[3], nmbclusters, nmbjclusters, nmbufs, nmbtypes;
	size_t nmbclen, nmbjclen, nmbuflen, mbstatlen, mbtypeslen;
	u_long *mbtypes;
	int ncpus;
	int n;
	int i;
	bool *seen;	/* "have we seen this type yet?" */

	mbtypes = NULL;
	seen = NULL;
	mbstat = NULL;

	/*
	 * XXX
	 * We can't kread() mbtypeslen from a core image so we'll
	 * bogusly assume it's the same as in the running kernel.
	 */
	if (sysctlbyname("kern.ipc.mbtypes", NULL, &mbtypeslen, NULL, 0) < 0) {
		warn("sysctl: retrieving mbtypes length");
		goto err;
	}
	nmbtypes = mbtypeslen / sizeof(*mbtypes);
	if ((seen = calloc(nmbtypes, sizeof(*seen))) == NULL) {
		warn("calloc");
		goto err;
	}

	if (mbaddr) {
		if (kread(ncpusaddr, (char *)&ncpus, sizeof ncpus))
			goto err;
		mbstat = malloc(sizeof(*mbstat) * ncpus);
		if (kread(mbaddr, (char *)mbstat, sizeof *mbstat * ncpus))
			goto err;
		mbtypes = malloc(mbtypeslen * ncpus);
		if (kread(mbtaddr, (char *)mbtypes, mbtypeslen * ncpus))
			goto err;
		if (kread(nmbcaddr, (char *)&nmbclusters, sizeof(int)))
			goto err;
		if (kread(nmbjcaddr, (char *)&nmbjclusters, sizeof(int)))
			goto err;
		if (kread(nmbufaddr, (char *)&nmbufs, sizeof(int)))
			goto err;
		for (n = 1; n < ncpus; ++n) {
			mbstat[0].m_mbufs += mbstat[n].m_mbufs;
			mbstat[0].m_clusters += mbstat[n].m_clusters;
			mbstat[0].m_drops += mbstat[n].m_drops;
			mbstat[0].m_wait += mbstat[n].m_wait;
			mbstat[0].m_drain += mbstat[n].m_drain;

			for (i = 0; i < nmbtypes; ++i) {
				mbtypes[i] += mbtypes[n * nmbtypes + i];
			}
		}
	} else {
		name[0] = CTL_KERN;
		name[1] = KERN_IPC;
		name[2] = KIPC_MBSTAT;
		mbstat = malloc(sizeof(*mbstat));
		mbstatlen = sizeof *mbstat;
		/* fake ncpus, kernel will aggregate mbstat array for us */
		if (sysctl(name, 3, mbstat, &mbstatlen, 0, 0) < 0) {
			warn("sysctl: retrieving mbstat");
			goto err;
		}

		mbtypes = malloc(mbtypeslen);
		if (sysctlbyname("kern.ipc.mbtypes", mbtypes, &mbtypeslen, NULL,
		    0) < 0) {
			warn("sysctl: retrieving mbtypes");
			goto err;
		}
		
		name[2] = KIPC_NMBCLUSTERS;
		nmbclen = sizeof(int);
		if (sysctl(name, 3, &nmbclusters, &nmbclen, 0, 0) < 0) {
			warn("sysctl: retrieving nmbclusters");
			goto err;
		}

		nmbjclen = sizeof(int);
		if (sysctlbyname("kern.ipc.nmbjclusters",
		    &nmbjclusters, &nmbjclen, 0, 0) < 0) {
			warn("sysctl: retrieving nmbjclusters");
			goto err;
		}

		nmbuflen = sizeof(int);
		if (sysctlbyname("kern.ipc.nmbufs", &nmbufs, &nmbuflen, 0, 0) < 0) {
			warn("sysctl: retrieving nmbufs");
			goto err;
		}
	}

#undef MSIZE
#define MSIZE		(mbstat->m_msize)
#undef MCLBYTES
#define	MCLBYTES	(mbstat->m_mclbytes)
#undef MJUMPAGESIZE
#define MJUMPAGESIZE	(mbstat->m_mjumpagesize)

	printf("%lu/%u mbufs in use (current/max):\n", mbstat->m_mbufs, nmbufs);
	printf("%lu/%u mbuf clusters in use (current/max)\n",
		mbstat->m_clusters, nmbclusters);
	printf("%lu/%u mbuf jumbo clusters in use (current/max)\n",
		mbstat->m_jclusters, nmbjclusters);
	for (mp = mbtypenames; mp->mt_name; mp++)
		if (mbtypes[mp->mt_type]) {
			seen[mp->mt_type] = YES;
			printf("\t%lu mbufs and mbuf clusters "
			       "allocated to %s\n",
			       mbtypes[mp->mt_type], mp->mt_name);
		}
	seen[MT_FREE] = YES;
	for (i = 0; i < nmbtypes; i++)
		if (!seen[i] && mbtypes[i]) {
			printf("\t%lu mbufs and mbuf clusters allocated to <mbuf type %d>\n",
			    mbtypes[i], i);
		}
	totmem = mbstat->m_mbufs * MSIZE + mbstat->m_clusters * MCLBYTES +
	    mbstat->m_jclusters * MJUMPAGESIZE;
	totpossible =  MSIZE * nmbufs + nmbclusters * MCLBYTES +
	    nmbjclusters * MJUMPAGESIZE;
	printf("%lu Kbytes allocated to network (%lu%% of mb_map in use)\n",
		totmem / 1024, (totmem * 100) / totpossible);
	printf("%lu requests for memory denied\n", mbstat->m_drops);
	printf("%lu requests for memory delayed\n", mbstat->m_wait);
	printf("%lu calls to protocol drain routines\n", mbstat->m_drain);

err:
	if (mbstat != NULL)
		free(mbstat);
	if (mbtypes != NULL)
		free(mbtypes);
	if (seen != NULL)
		free(seen);
}
