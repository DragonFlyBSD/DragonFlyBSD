/*
 * Copyright (c) 2000-2001 Boris Popov
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: src/sys/netsmb/smb_subr.c,v 1.1.2.2 2001/09/03 08:55:11 bp Exp $
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/resourcevar.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/signalvar.h>
#include <sys/wait.h>
#include <sys/unistd.h>

#include <sys/signal2.h>
#include <sys/mplock2.h>

#include <machine/stdarg.h>

#include <sys/iconv.h>

#include "smb.h"
#include "smb_conn.h"
#include "smb_rq.h"
#include "smb_subr.h"

MALLOC_DEFINE(M_SMBSTR, "SMBSTR", "netsmb string data");
MALLOC_DEFINE(M_SMBTEMP, "SMBTEMP", "Temp netsmb data");

smb_unichar smb_unieol = 0;

void
smb_makescred(struct smb_cred *scred, struct thread *td, struct ucred *cred)
{
	scred->scr_td = td;
	if (td && td->td_proc) {
		scred->scr_cred = cred ? cred : td->td_proc->p_ucred;
	} else {
		scred->scr_cred = cred ? cred : NULL;
	}
}

int
smb_proc_intr(struct thread *td)
{
	sigset_t tmpset;
	struct proc *p;
	struct lwp *lp;

	if (td == NULL || (p = td->td_proc) == NULL)
		return 0;
	lp = td->td_lwp;
	tmpset = lwp_sigpend(lp);
	SIGSETNAND(tmpset, lp->lwp_sigmask);
	SIGSETNAND(tmpset, p->p_sigignore);
	if (SIGNOTEMPTY(tmpset) && SMB_SIGMASK(tmpset))
                return EINTR;
	return 0;
}

char *
smb_strdup(const char *s)
{
	char *p;
	int len;

	len = s ? strlen(s) + 1 : 1;
	p = kmalloc(len, M_SMBSTR, M_WAITOK);
	if (s)
		bcopy(s, p, len);
	else
		*p = 0;
	return p;
}

/*
 * duplicate string from a user space.
 */
char *
smb_strdupin(char *s, int maxlen)
{
	char *p, bt;
	int len = 0;

	for (p = s; ;p++) {
		if (copyin(p, &bt, 1))
			return NULL;
		len++;
		if (maxlen && len > maxlen)
			return NULL;
		if (bt == 0)
			break;
	}
	p = kmalloc(len, M_SMBSTR, M_WAITOK);
	copyin(s, p, len);
	return p;
}

/*
 * duplicate memory block from a user space.
 */
void *
smb_memdupin(void *umem, int len)
{
	char *p;

	if (len > 8 * 1024)
		return NULL;
	p = kmalloc(len, M_SMBSTR, M_WAITOK);
	if (copyin(umem, p, len) == 0)
		return p;
	kfree(p, M_SMBSTR);
	return NULL;
}

/*
 * duplicate memory block in the kernel space.
 */
void *
smb_memdup(const void *umem, int len)
{
	char *p;

	if (len > 8 * 1024)
		return NULL;
	p = kmalloc(len, M_SMBSTR, M_WAITOK);
	bcopy(umem, p, len);
	return p;
}

void
smb_strfree(char *s)
{
	kfree(s, M_SMBSTR);
}

void
smb_memfree(void *s)
{
	kfree(s, M_SMBSTR);
}

void *
smb_zmalloc(unsigned long size, struct malloc_type *type, int flags)
{

	return kmalloc(size, type, flags | M_ZERO);
}

void
smb_strtouni(u_int16_t *dst, const char *src)
{
	while (*src) {
		*dst++ = htole16(*src++);
	}
	*dst = 0;
}

#ifdef SMB_SOCKETDATA_DEBUG
void
m_dumpm(struct mbuf *m) {
	char *p;
	int len;
	kprintf("d=");
	while(m) {
		p=mtod(m,char *);
		len=m->m_len;
		kprintf("(%d)",len);
		while(len--){
			kprintf("%02x ",((int)*(p++)) & 0xff);
		}
		m=m->m_next;
	};
	kprintf("\n");
}
#endif

int
smb_maperror(int eclass, int eno)
{
	if (eclass == 0 && eno == 0)
		return 0;
	switch (eclass) {
	    case ERRDOS:
		switch (eno) {
		    case ERRbadfunc:
		    case ERRbadmcb:
		    case ERRbadenv:
		    case ERRbadformat:
		    case ERRrmuns:
			return EINVAL;
		    case ERRbadfile:
		    case ERRbadpath:
		    case ERRremcd:
		    case 66:		/* nt returns it when share not available */
		    case 67:		/* observed from nt4sp6 when sharename wrong */
			return ENOENT;
		    case ERRnofids:
			return EMFILE;
		    case ERRnoaccess:
		    case ERRbadshare:
			return EACCES;
		    case ERRbadfid:
			return EBADF;
		    case ERRnomem:
			return ENOMEM;	/* actually remote no mem... */
		    case ERRbadmem:
			return EFAULT;
		    case ERRbadaccess:
			return EACCES;
		    case ERRbaddata:
			return E2BIG;
		    case ERRbaddrive:
		    case ERRnotready:	/* nt */
			return ENXIO;
		    case ERRdiffdevice:
			return EXDEV;
		    case ERRnofiles:
			return 0;	/* eeof ? */
			return ETXTBSY;
		    case ERRlock:
			return EDEADLK;
		    case ERRfilexists:
			return EEXIST;
		    case 123:		/* dunno what is it, but samba maps as noent */
			return ENOENT;
		    case 145:		/* samba */
			return ENOTEMPTY;
		    case 183:
			return EEXIST;
		}
		break;
	    case ERRSRV:
		switch (eno) {
		    case ERRerror:
			return EINVAL;
		    case ERRbadpw:
			return EAUTH;
		    case ERRaccess:
			return EACCES;
		    case ERRinvnid:
			return ENETRESET;
		    case ERRinvnetname:
			SMBERROR("NetBIOS name is invalid\n");
			return EAUTH;
		    case 3:		/* reserved and returned */
			return EIO;
		    case 2239:		/* NT: account exists but disabled */
			return EPERM;
		}
		break;
	    case ERRHRD:
		switch (eno) {
		    case ERRnowrite:
			return EROFS;
		    case ERRbadunit:
			return ENODEV;
		    case ERRnotready:
		    case ERRbadcmd:
		    case ERRdata:
			return EIO;
		    case ERRbadreq:
			return EBADRPC;
		    case ERRbadshare:
			return ETXTBSY;
		    case ERRlock:
			return EDEADLK;
		}
		break;
	}
	SMBERROR("Unmapped error %d:%d\n", eclass, eno);
	return EBADRPC;
}

static int
smb_copy_iconv(struct mbchain *mbp, c_caddr_t src, caddr_t dst,
    size_t *srclen, size_t *dstlen)
{
	int error;
	size_t inlen = *srclen, outlen = *dstlen;

	error = iconv_conv((struct iconv_drv*)mbp->mb_udata, &src, &inlen,
	    &dst, &outlen);
	if (inlen != *srclen || outlen != *dstlen) {
		*srclen -= inlen;
		*dstlen -= outlen;
		return 0;
	} else {
		return error;
	}
}

int
smb_put_dmem(struct mbchain *mbp, struct smb_vc *vcp, const char *src,
	int size, int caseopt)
{
	struct iconv_drv *dp = vcp->vc_toserver;

	if (size == 0)
		return 0;
	if (dp == NULL) {
		return mb_put_mem(mbp, src, size, MB_MSYSTEM);
	}
	mbp->mb_copy = smb_copy_iconv;
	mbp->mb_udata = dp;
	return mb_put_mem(mbp, src, size, MB_MCUSTOM);
}

int
smb_put_dstring(struct mbchain *mbp, struct smb_vc *vcp, const char *src,
	int caseopt)
{
	int error;

	error = smb_put_dmem(mbp, vcp, src, strlen(src), caseopt);
	if (error)
		return error;
	return mb_put_uint8(mbp, 0);
}

int
smb_put_asunistring(struct smb_rq *rqp, const char *src)
{
	struct mbchain *mbp = &rqp->sr_rq;
	struct iconv_drv *dp = rqp->sr_vc->vc_toserver;
	u_char c;
	int error;

	while (*src) {
		iconv_convmem(dp, &c, src++, 1);
		error = mb_put_uint16le(mbp, c);
		if (error)
			return error;
	}
	return mb_put_uint16le(mbp, 0);
}

/*
 * Create a kernel process/thread/whatever.  It shares it's address space
 * with proc0 - ie: kernel only.
 *
 * XXX only the SMB protocol uses this, we should convert this mess to a
 * pure thread when possible.
 */
int
smb_kthread_create(void (*func)(void *), void *arg,
		   struct proc **newpp, int flags, const char *fmt, ...)
{
	int error;
	__va_list ap;
	struct proc *p2;
	struct lwp *lp2;

	error = fork1(&lwp0, RFMEM | RFFDG | RFPROC | flags, &p2);
	if (error)
		return error;

	/* save a global descriptor, if desired */
	if (newpp != NULL)
		*newpp = p2;

	/* this is a non-swapped system process */
	p2->p_flags |= P_SYSTEM;
	p2->p_sigacts->ps_flag |= PS_NOCLDWAIT;

	lp2 = ONLY_LWP_IN_PROC(p2);

	/* set up arg0 for 'ps', et al */
	__va_start(ap, fmt);
	kvsnprintf(p2->p_comm, sizeof(p2->p_comm), fmt, ap);
	__va_end(ap);

	lp2->lwp_thread->td_ucred = crhold(proc0.p_ucred);

	/* call the processes' main()... */
	cpu_set_fork_handler(lp2,
			     (void (*)(void *, struct trapframe *))func, arg);
	start_forked_proc(&lwp0, p2);

	return 0;
}

void
smb_kthread_exit(void)
{
	exit1(0);
}

/*
 * smb_sleep() icky compat routine.  Leave the token held through the tsleep
 * to interlock against the sleep.  Remember that the token could be lost
 * since we blocked, so reget or release as appropriate.
 */
int
smb_sleep(void *chan, struct smb_slock *sl, int slpflags, const char *wmesg, int timo)
{
	int error;

	if (sl) {
		tsleep_interlock(chan, slpflags);
		smb_sl_unlock(sl);
		error = tsleep(chan, slpflags | PINTERLOCKED, wmesg, timo);
		if ((slpflags & PDROP) == 0)
			smb_sl_lock(sl);
	} else {
		error = tsleep(chan, slpflags, wmesg, timo);
	}
	return error;
}

