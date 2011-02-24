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
 *	@(#)procfs.h	8.9 (Berkeley) 5/14/95
 *
 * From:
 * $FreeBSD: src/sys/miscfs/procfs/procfs.h,v 1.32.2.3 2002/01/22 17:22:59 nectar Exp $
 * $DragonFly: src/sys/vfs/procfs/procfs.h,v 1.8 2007/02/19 01:14:24 corecode Exp $
 */

/*
 * The different types of node in a procfs filesystem
 */
typedef enum {
	Proot,		/* the filesystem root */
	Pcurproc,	/* symbolic link for curproc */
	Pproc,		/* a process-specific sub-directory */
	Pfile,		/* the executable file */
	Pmem,		/* the process's memory image */
	Pregs,		/* the process's register set */
	Pfpregs,	/* the process's FP register set */
	Pdbregs,	/* the process's debug register set */
	Pctl,		/* process control */
	Pstatus,	/* process status */
	Pnote,		/* process notifier */
	Pnotepg,	/* process group notifier */
	Pmap,		/* memory map */
	Ptype,		/* executable type */
	Pcmdline,	/* command line */
	Prlimit		/* resource limits */
} pfstype;

/*
 * control data for the proc file system.
 */
struct pfsnode {
	struct pfsnode	*pfs_next;	/* next on list */
	struct vnode	*pfs_vnode;	/* vnode associated with this pfsnode */
	pfstype		pfs_type;	/* type of procfs node */
	pid_t		pfs_pid;	/* associated process */
	u_short		pfs_mode;	/* mode bits for stat() */
	u_long		pfs_flags;	/* open flags */
	u_long		pfs_fileno;	/* unique file id */
	pid_t		pfs_lockowner;	/* pfs lock owner */
};

#define PROCFS_NOTELEN	64	/* max length of a note (/proc/$pid/note) */
#define PROCFS_CTLLEN 	8	/* max length of a ctl msg (/proc/$pid/ctl */
#define PROCFS_NAMELEN 	8	/* max length of a filename component */

/*
 * Kernel stuff follows
 */
#ifdef _KERNEL
#define CNEQ(cnp, s, len) \
	 ((cnp)->cn_namelen == (len) && \
	  (bcmp((s), (cnp)->cn_nameptr, (len)) == 0))

#define PROCFS_FILENO(pid, type) \
	(((type) < Pproc) ? \
			((type) + 2) : \
			((((pid)+1) << 4) + ((int) (type))))

/* XXX: Is PRIV_DEBUG_UNPRIV correct? */
#define CHECKIO(p1, p2) \
     ((((p1)->p_ucred->cr_uid == (p2)->p_ucred->cr_ruid) && \
       ((p1)->p_ucred->cr_ruid == (p2)->p_ucred->cr_ruid) && \
       ((p1)->p_ucred->cr_svuid == (p2)->p_ucred->cr_ruid) && \
       ((p2)->p_flag & (P_SUGID|P_INEXEC)) == 0) || \
      (priv_check_cred((p1)->p_ucred, PRIV_DEBUG_UNPRIV, 0) == 0))

/*
 * Convert between pfsnode vnode
 */
#define VTOPFS(vp)	((struct pfsnode *)(vp)->v_data)
#define PFSTOV(pfs)	((pfs)->pfs_vnode)

typedef struct vfs_namemap vfs_namemap_t;
struct vfs_namemap {
	const char *nm_name;
	int nm_val;
};

int vfs_getuserstr (struct uio *, char *, int *);
vfs_namemap_t *vfs_findname (vfs_namemap_t *, char *, int);

/* <machine/reg.h> */
struct reg;
struct fpreg;
struct dbreg;

void procfs_exit (struct thread *);
int procfs_freevp (struct vnode *);
int procfs_allocvp (struct mount *, struct vnode **, long, pfstype);
struct vnode *procfs_findtextvp (struct proc *);
int procfs_sstep (struct lwp *);
int procfs_read_regs (struct lwp *, struct reg *);
int procfs_write_regs (struct lwp *, struct reg *);
int procfs_read_fpregs (struct lwp *, struct fpreg *);
int procfs_write_fpregs (struct lwp *, struct fpreg *);
int procfs_read_dbregs (struct lwp *, struct dbreg *);
int procfs_write_dbregs (struct lwp *, struct dbreg *);
int procfs_donote (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_doregs (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_dofpregs (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_dodbregs (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_domem (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_doctl (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_dostatus (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_domap (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_dotype (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_docmdline (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);
int procfs_dorlimit (struct proc *, struct lwp *, struct pfsnode *pfsp, struct uio *uio);

/* functions to check whether or not files should be displayed */
int procfs_validfile (struct lwp *);
int procfs_validfpregs (struct lwp *);
int procfs_validregs (struct lwp *);
int procfs_validdbregs (struct lwp *);
int procfs_validmap (struct lwp *);
int procfs_validtype (struct lwp *);

struct proc *pfs_pfind(pid_t);

#define PROCFS_LOCKED	0x01
#define PROCFS_WANT	0x02

#define PFS_DEAD        0x80000000	/* or'd with pid */

int	procfs_root (struct mount *, struct vnode **);
int	procfs_rw (struct vop_read_args *);

#endif /* _KERNEL */
