/*
 * Copyright (c) 1995 Gordon Ross, Adam Glass
 * Copyright (c) 1992 Regents of the University of California.
 * All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
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
 *	California, Lawrence Berkeley Laboratory and its contributors.
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
 * nfs/krpc_subr.c
 * $NetBSD: krpc_subr.c,v 1.10 1995/08/08 20:43:43 gwr Exp $
 * $FreeBSD: src/sys/nfs/bootp_subr.c,v 1.20.2.9 2003/04/24 16:51:08 ambrisko Exp $
 */
/*
 * Procedures used by NFS_ROOT and BOOTP to do an NFS mount rpc to obtain
 * the nfs root file handle for a NFS-based root mount point.  This module 
 * is not used by normal operating code because the 'mount' command has a
 * far more sophisticated implementation.
 */
#include "opt_bootp.h"
#include "opt_nfsroot.h"

#if defined(BOOTP) || defined(NFS_ROOT)

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <net/if_types.h>
#include <net/if_dl.h>

#include "rpcv2.h"
#include "nfsproto.h"
#include "nfs.h"
#include "nfsdiskless.h"
#include "krpc.h"
#include "xdr_subs.h"
#include "nfsmountrpc.h"

/*
 * What is the longest we will wait before re-sending a request?
 * Note this is also the frequency of "RPC timeout" messages.
 * The re-send loop count sup linearly to this maximum, so the
 * first complaint will happen after (1+2+3+4+5)=15 seconds.
 */

static int getdec(char **ptr);
static char *substr(char *a,char *b);
static int xdr_opaque_decode(struct mbuf **ptr, u_char *buf, int len);
static int xdr_int_decode(struct mbuf **ptr, int *iptr);

void
nfs_mountopts(struct nfs_args *args, char *p)
{
	char *tmp;
	
	args->version = NFS_ARGSVERSION;
	args->rsize = 8192;
	args->wsize = 8192;
	args->flags = NFSMNT_RSIZE | NFSMNT_WSIZE | NFSMNT_RESVPORT;
	args->sotype = SOCK_STREAM;
	if (p == NULL)
		return;
	if ((tmp = substr(p, "rsize=")))
		args->rsize = getdec(&tmp);
	if ((tmp = substr(p, "wsize=")))
		args->wsize = getdec(&tmp);
	if ((tmp = substr(p, "intr")))
		args->flags |= NFSMNT_INT;
	if ((tmp = substr(p, "soft")))
		args->flags |= NFSMNT_SOFT;
	if ((tmp = substr(p, "noconn")))
		args->flags |= NFSMNT_NOCONN;
	if ((tmp = substr(p, "udp")))
		args->sotype = SOCK_DGRAM;
}

/*
 * RPC: mountd/mount
 * Given a server pathname, get an NFS file handle.
 * Also, sets sin->sin_port to the NFS service port.
 */
int
md_mount(struct sockaddr_in *mdsin,		/* mountd server address */
	 char *path,
	 u_char *fhp,
	 int *fhsizep,
	 struct nfs_args *args,
	 struct thread *td)
{
	struct mbuf *m;
	int error;
	int authunixok;
	int authcount;
	int authver;
	
	/* First try NFS v3 */
	/* Get port number for MOUNTD. */
	error = krpc_portmap(mdsin, RPCPROG_MNT, RPCMNT_VER3,
			     &mdsin->sin_port, td);
	if (error == 0) {
		m = xdr_string_encode(path, strlen(path));
		
		/* Do RPC to mountd. */
		error = krpc_call(mdsin, RPCPROG_MNT, RPCMNT_VER3,
				  RPCMNT_MOUNT, &m, NULL, td);
	}
	if (error == 0) {
		args->flags |= NFSMNT_NFSV3;
	} else {
		/* Fallback to NFS v2 */
		
		/* Get port number for MOUNTD. */
		error = krpc_portmap(mdsin, RPCPROG_MNT, RPCMNT_VER1,
				     &mdsin->sin_port, td);
		if (error != 0)
			return error;
		
		m = xdr_string_encode(path, strlen(path));
		
		/* Do RPC to mountd. */
		error = krpc_call(mdsin, RPCPROG_MNT, RPCMNT_VER1,
				  RPCMNT_MOUNT, &m, NULL, td);
		if (error != 0)
			return error;	/* message already freed */
		args->flags &= ~NFSMNT_NFSV3;
	}

	if (xdr_int_decode(&m, &error) != 0 || error != 0)
		goto bad;
	
	if ((args->flags & NFSMNT_NFSV3) != 0) {
		if (xdr_int_decode(&m, fhsizep) != 0 ||
		    *fhsizep > NFSX_V3FHMAX ||
		    *fhsizep <= 0)
			goto bad;
	} else
		*fhsizep = NFSX_V2FH;

	if (xdr_opaque_decode(&m, fhp, *fhsizep) != 0)
		goto bad;

	if (args->flags & NFSMNT_NFSV3) {
		if (xdr_int_decode(&m, &authcount) != 0)
			goto bad;
		authunixok = 0;
		if (authcount < 0 || authcount > 100)
			goto bad;
		while (authcount > 0) {
			if (xdr_int_decode(&m, &authver) != 0)
				goto bad;
			if (authver == RPCAUTH_UNIX)
				authunixok = 1;
			authcount--;
		}
		if (authunixok == 0)
			goto bad;
	}
	  
	/* Set port number for NFS use. */
	error = krpc_portmap(mdsin, NFS_PROG,
			     (args->flags &
			      NFSMNT_NFSV3) ? NFS_VER3 : NFS_VER2,
			     &mdsin->sin_port, td);
	
	goto out;
	
bad:
	error = EBADRPC;
	
out:
	m_freem(m);
	return error;
}

int
md_lookup_swap(struct sockaddr_in *mdsin,	/* mountd server address */
	       char *path,
	       u_char *fhp,
	       int *fhsizep,
	       struct nfs_args *args,
	       struct thread *td)
{
	struct mbuf *m;
	int error;
	int size = -1;
	int attribs_present;
	int status;
	union {
		u_int32_t v2[17];
		u_int32_t v3[21];
	} fattribs;
	
	m = m_get(MB_WAIT,MT_DATA);
	if (m == NULL)
	  	return ENOBUFS;
	
	if ((args->flags & NFSMNT_NFSV3) != 0) {
		*mtod(m, u_int32_t *) = txdr_unsigned(*fhsizep);
		bcopy(fhp, mtod(m, u_char *) + sizeof(u_int32_t), *fhsizep);
		m->m_len = *fhsizep + sizeof(u_int32_t);
	} else {
		bcopy(fhp, mtod(m, u_char *), NFSX_V2FH);
		m->m_len = NFSX_V2FH;
	}
	
	m->m_next = xdr_string_encode(path, strlen(path));
	if (m->m_next == NULL) {
		error = ENOBUFS;
		goto out;
	}

	/* Do RPC to nfsd. */
	if ((args->flags & NFSMNT_NFSV3) != 0)
		error = krpc_call(mdsin, NFS_PROG, NFS_VER3,
				  NFSPROC_LOOKUP, &m, NULL, td);
	else
		error = krpc_call(mdsin, NFS_PROG, NFS_VER2,
				  NFSV2PROC_LOOKUP, &m, NULL, td);
	if (error != 0)
		return error;	/* message already freed */

	if (xdr_int_decode(&m, &status) != 0)
		goto bad;
	if (status != 0) {
		error = ENOENT;
		goto out;
	}
	
	if ((args->flags & NFSMNT_NFSV3) != 0) {
		if (xdr_int_decode(&m, fhsizep) != 0 ||
		    *fhsizep > NFSX_V3FHMAX ||
		    *fhsizep <= 0)
			goto bad;
	} else
		*fhsizep = NFSX_V2FH;
	
	if (xdr_opaque_decode(&m, fhp, *fhsizep) != 0)
		goto bad;
	
	if ((args->flags & NFSMNT_NFSV3) != 0) {
		if (xdr_int_decode(&m, &attribs_present) != 0)
			goto bad;
		if (attribs_present != 0) {
			if (xdr_opaque_decode(&m, (u_char *) &fattribs.v3,
					      sizeof(u_int32_t) * 21) != 0)
				goto bad;
			size = fxdr_unsigned(u_int32_t, fattribs.v3[6]);
		}
	} else {
		if (xdr_opaque_decode(&m,(u_char *) &fattribs.v2,
				      sizeof(u_int32_t) * 17) != 0)
			goto bad;
		size = fxdr_unsigned(u_int32_t, fattribs.v2[5]);
	}
	  
	if (nfsv3_diskless.swap_nblks == 0 && size != -1) {
		nfsv3_diskless.swap_nblks = size / 1024;
		kprintf("md_lookup_swap: Swap size is %d KB\n",
		       nfsv3_diskless.swap_nblks);
	}
	
	goto out;
	
bad:
	error = EBADRPC;
	
out:
	m_freem(m);
	return error;
}

int
setfs(struct sockaddr_in *addr, char *path, char *p)
{
	unsigned int ip;
	int val;
	
	ip = 0;
	if (((val = getdec(&p)) < 0) || (val > 255))
		return 0;
	ip = val << 24;
	if (*p != '.')
		return 0;
	p++;
	if (((val = getdec(&p)) < 0) || (val > 255))
		return 0;
	ip |= (val << 16);
	if (*p != '.')
		return 0;
	p++;
	if (((val = getdec(&p)) < 0) || (val > 255))
		return 0;
	ip |= (val << 8);
	if (*p != '.')
		return 0;
	p++;
	if (((val = getdec(&p)) < 0) || (val > 255))
		return 0;
	ip |= val;
	if (*p != ':')
		return 0;
	p++;
	
	addr->sin_addr.s_addr = htonl(ip);
	addr->sin_len = sizeof(struct sockaddr_in);
	addr->sin_family = AF_INET;
	
	strncpy(path, p, MNAMELEN - 1);
	return 1;
}

static int
getdec(char **ptr)
{
	char *p;
	int ret;

	p = *ptr;
	ret = 0;
	if ((*p < '0') || (*p > '9'))
		return -1;
	while ((*p >= '0') && (*p <= '9')) {
		ret = ret * 10 + (*p - '0');
		p++;
	}
	*ptr = p;
	return ret;
}

static char *
substr(char *a, char *b)
{
	char *loc1;
	char *loc2;
	
        while (*a != '\0') {
                loc1 = a;
                loc2 = b;
                while (*loc1 == *loc2++) {
                        if (*loc1 == '\0')
				return 0;
                        loc1++;
                        if (*loc2 == '\0')
				return loc1;
                }
		a++;
        }
        return 0;
}

static int
xdr_opaque_decode(struct mbuf **mptr, u_char *buf, int len)
{
	struct mbuf *m;
	int alignedlen;
	
	m = *mptr;
	alignedlen = ( len + 3 ) & ~3;
	
	if (m->m_len < alignedlen) {
		m = m_pullup(m, alignedlen);
		if (m == NULL) {
			*mptr = NULL;
			return EBADRPC;
		}
	}
	bcopy(mtod(m, u_char *), buf, len);
	m_adj(m, alignedlen);
	*mptr = m;
	return 0;
}

static int
xdr_int_decode(struct mbuf **mptr, int *iptr)
{
	u_int32_t i;
	if (xdr_opaque_decode(mptr, (u_char *) &i, sizeof(u_int32_t)) != 0)
		return EBADRPC;
	*iptr = fxdr_unsigned(u_int32_t, i);
	return 0;
}

#endif	/* BOOTP && NFS_ROOT */
