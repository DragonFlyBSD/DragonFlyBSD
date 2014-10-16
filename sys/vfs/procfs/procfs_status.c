/*
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
 *	@(#)procfs_status.c	8.4 (Berkeley) 6/15/94
 *
 * From:
 * $FreeBSD: src/sys/miscfs/procfs/procfs_status.c,v 1.20.2.4 2002/01/22 17:22:59 nectar Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/jail.h>
#include <sys/vnode.h>
#include <sys/tty.h>
#include <sys/resourcevar.h>
#include <vfs/procfs/procfs.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>
#include <sys/exec.h>

#define DOCHECK() do {	\
	if (ps >= psbuf+sizeof(psbuf)) {	\
		error = ENOMEM;			\
		goto bailout;			\
	}					\
    } while (0)

int
procfs_dostatus(struct proc *curp, struct lwp *lp, struct pfsnode *pfs,
		struct uio *uio)
{
	struct proc *p = lp->lwp_proc;
	struct session *sess;
	struct tty *tp;
	struct ucred *cr;
	char *ps;
	char *sep;
	int pid, ppid, pgid, sid;
	size_t xlen;
	int i;
	int error;
	char psbuf[256];	/* XXX - conservative */

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);

	pid = p->p_pid;
	ppid = p->p_pptr ? p->p_pptr->p_pid : 0;
	pgid = p->p_pgrp->pg_id;
	sess = p->p_pgrp->pg_session;
	sid = sess->s_leader ? sess->s_leader->p_pid : 0;

/* comm pid ppid pgid sid maj,min ctty,sldr start ut st wmsg 
                                euid ruid rgid,egid,groups[1 .. NGROUPS]
*/
	KASSERT(sizeof(psbuf) > MAXCOMLEN,
			("Too short buffer for new MAXCOMLEN"));

	ps = psbuf;
	bcopy(p->p_comm, ps, MAXCOMLEN);
	ps[MAXCOMLEN] = '\0';
	ps += strlen(ps);
	DOCHECK();
	ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
	    " %d %d %d %d ", pid, ppid, pgid, sid);
	DOCHECK();
	if ((p->p_flags & P_CONTROLT) && (tp = sess->s_ttyp))
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
		    "%d,%d ", major(tp->t_dev), minor(tp->t_dev));
	else
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
		    "%d,%d ", -1, -1);
	DOCHECK();

	sep = "";
	if (sess->s_ttyvp) {
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, "%sctty", sep);
		sep = ",";
		DOCHECK();
	}
	if (SESS_LEADER(p)) {
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, "%ssldr", sep);
		sep = ",";
		DOCHECK();
	}
	if (*sep != ',') {
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, "noflags");
		DOCHECK();
	}

	if (p->p_flags & P_SWAPPEDOUT) {
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
		    " -1,-1 -1,-1 -1,-1");
	} else {
		struct rusage ru;

		calcru_proc(p, &ru);
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
		    " %ld,%ld %ld,%ld %ld,%ld",
		    p->p_start.tv_sec,
		    p->p_start.tv_usec,
		    ru.ru_utime.tv_sec, ru.ru_utime.tv_usec,
		    ru.ru_stime.tv_sec, ru.ru_stime.tv_usec);
	}
	DOCHECK();

	ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, " %s",
		(lp->lwp_wchan && lp->lwp_wmesg) ? lp->lwp_wmesg : "nochan");
	DOCHECK();

	cr = p->p_ucred;

	ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, " %lu %lu %lu", 
		(u_long)cr->cr_uid,
		(u_long)p->p_ucred->cr_ruid,
		(u_long)p->p_ucred->cr_rgid);
	DOCHECK();

	/* egid (p->p_ucred->cr_svgid) is equal to cr_ngroups[0] 
	   see also getegid(2) in /sys/kern/kern_prot.c */

	for (i = 0; i < cr->cr_ngroups; i++) {
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
		    ",%lu", (u_long)cr->cr_groups[i]);
		DOCHECK();
	}

	if (p->p_ucred->cr_prison)
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps,
		    " %s", p->p_ucred->cr_prison->pr_host);
	else
		ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, " -");
	DOCHECK();
	ps += ksnprintf(ps, psbuf + sizeof(psbuf) - ps, "\n");
	DOCHECK();

	xlen = ps - psbuf;
	error = uiomove_frombuf(psbuf, xlen, uio);

bailout:
	return (error);
}

int
procfs_docmdline(struct proc *curp, struct lwp *lp, struct pfsnode *pfs,
		 struct uio *uio)
{
	struct proc *p = lp->lwp_proc;
	char *ps;
	int error;
	char *buf, *bp;
	struct ps_strings pstr;
	char **ps_argvstr;
	int i;
	size_t bytes_left, done;
	size_t buflen;

	if (uio->uio_rw != UIO_READ)
		return (EOPNOTSUPP);
	
	/*
	 * If we are using the ps/cmdline caching, use that.  Otherwise
	 * revert back to the old way which only implements full cmdline
	 * for the currept process and just p->p_comm for all other
	 * processes.
	 * Note that if the argv is no longer available, we deliberately
	 * don't fall back on p->p_comm or return an error: the authentic
	 * Linux behaviour is to return zero-length in this case.
	 */
	if (p->p_upmap != NULL && p->p_upmap->proc_title[0] &&
	    (ps_argsopen || (CHECKIO(curp, p) &&
			     (p->p_flags & P_INEXEC) == 0 &&
			     !p_trespass(curp->p_ucred, p->p_ucred))
	    )) {
		/*
		 * Args set via writable user process mmap.
		 * We must calculate the string length manually
		 * because the user data can change at any time.
		 */
		bp = p->p_upmap->proc_title;
		for (buflen = 0; buflen < UPMAP_MAXPROCTITLE - 1; ++buflen) {
			if (bp[buflen] == 0)
				break;
		}
		buf = NULL;
	} else if (p->p_args &&
		   (ps_argsopen || (CHECKIO(curp, p) &&
				    (p->p_flags & P_INEXEC) == 0 &&
				     !p_trespass(curp->p_ucred, p->p_ucred))
		   )) {
		bp = p->p_args->ar_args;
		buflen = p->p_args->ar_length;
		buf = NULL;
	} else if (p != curp) {
		bp = p->p_comm;
		buflen = MAXCOMLEN;
		buf = NULL;
	} else {
		buflen = 256;
		buf = kmalloc(buflen + 1, M_TEMP, M_WAITOK);
		bp = buf;
		ps = buf;
		error = copyin((void*)PS_STRINGS, &pstr, sizeof(pstr));

		if (error) {
			kfree(buf, M_TEMP);
			return (error);
		}
		if (pstr.ps_nargvstr < 0) {
			kfree(buf, M_TEMP);
			return (EINVAL);
		}
		if (pstr.ps_nargvstr > ARG_MAX) {
			kfree(buf, M_TEMP);
			return (E2BIG);
		}
		ps_argvstr = kmalloc(pstr.ps_nargvstr * sizeof(char *),
				     M_TEMP, M_WAITOK);
		error = copyin((void *)pstr.ps_argvstr, ps_argvstr,
			       pstr.ps_nargvstr * sizeof(char *));
		if (error) {
			kfree(ps_argvstr, M_TEMP);
			kfree(buf, M_TEMP);
			return (error);
		}
		bytes_left = buflen;
		for (i = 0; bytes_left && (i < pstr.ps_nargvstr); i++) {
			error = copyinstr(ps_argvstr[i], ps,
					  bytes_left, &done);
			/* If too long or malformed, just truncate */
			if (error) {
				error = 0;
				break;
			}
			ps += done;
			bytes_left -= done;
		}
		buflen = ps - buf;
		kfree(ps_argvstr, M_TEMP);
	}

	error = uiomove_frombuf(bp, buflen, uio);
	if (buf)
		kfree(buf, M_TEMP);
	return (error);
}
