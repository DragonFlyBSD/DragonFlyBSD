/*
 * Copyright (c) 2000 Dag-Erling Coïdan Smørgrav
 * Copyright (c) 1999 Pierre Beyssac
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
 * $FreeBSD: src/sys/i386/linux/linprocfs/linprocfs.h,v 1.2.2.4 2001/06/25 19:46:47 pirzyk Exp $
 * $DragonFly: src/sys/emulation/linux/i386/linprocfs/linprocfs.h,v 1.10 2007/02/03 09:50:49 y0netan1 Exp $
 */

/*
 * The different types of node in a procfs filesystem
 */
typedef enum {
	Proot,		/* the filesystem root */
	Pself,		/* symbolic link for curproc */
	Pproc,		/* a process-specific sub-directory */
	Pexe,		/* the executable file */
	Pmem,		/* the process's memory image */
	Pprocstat,	/* the process's status */
	Pprocstatus,	/* the process's status (again) */
	Pmeminfo,	/* memory system statistics */
	Pcpuinfo,	/* CPU model, speed and features */
	Pstat,	        /* kernel/system statistics */
	Puptime,	/* system uptime */
	Pversion,	/* system version */
	Ploadavg,	/* system load average */
	Pnet,		/* the net sub-directory */
	Pnetdev,	/* net devices */
	Psys,		/* the sys sub-directory */
	Psyskernel,	/* the sys/kernel sub-directory */
	Pdevices,	/* devices */
	Posrelease,	/* osrelease */
	Postype,	/* ostype */
	Ppidmax,	/* pid_max */
	Pcwd,
	Pprocroot,
	Pfd,
	Pcmdline,
	Penviron,
	Pmaps,
	Pstatm,
	Pmounts,
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
	struct thread	*pfs_lockowner;	/* pfs lock owner */
};

#define PROCFS_NAMELEN 	8	/* max length of a filename component */

/*
 * Kernel stuff follows
 */
#ifdef _KERNEL
#define CNEQ(cnp, s, len) \
	 ((cnp)->cn_namelen == (len) && \
	  (bcmp((s), (cnp)->cn_nameptr, (len)) == 0))

#define KMEM_GROUP 2

#define PROCFS_FILENO(pid, type) \
	(((type) < Pproc) ? \
			((type) + 2) : \
			((((pid)+1) << 4) + ((int) (type))))

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

/* <sys/reg.h> */
struct reg;
struct fpreg;
struct dbreg;

#define PFIND(pid) ((pid) ? pfindn(pid) : &proc0) /* pfindn() not MPSAFE XXX */

void linprocfs_exit (struct thread *);
int linprocfs_freevp (struct vnode *);
int linprocfs_allocvp (struct mount *, struct vnode **, long, pfstype);
int linprocfs_sstep (struct proc *);
void linprocfs_fix_sstep (struct proc *);
struct proc *linprocfs_pfind(pid_t pfs_pid);
#if 0
int linprocfs_read_regs (struct proc *, struct reg *);
int linprocfs_write_regs (struct proc *, struct reg *);
int linprocfs_read_fpregs (struct proc *, struct fpreg *);
int linprocfs_write_fpregs (struct proc *, struct fpreg *);
int linprocfs_read_dbregs (struct proc *, struct dbreg *);
int linprocfs_write_dbregs (struct proc *, struct dbreg *);
#endif
int linprocfs_domeminfo (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_docpuinfo (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_domounts (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_dostat (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_douptime (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_doversion (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_doprocstat (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_doprocstatus (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_doloadavg (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_donetdev (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_dodevices (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_doosrelease (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_doostype (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_dopidmax (struct proc *, struct proc *, struct pfsnode *pfsp, struct uio *uio);
int linprocfs_domaps(struct proc *curp, struct proc *p, struct pfsnode *pfs, struct uio *uio);
int linprocfs_dostatm(struct proc *curp, struct proc *p, struct pfsnode *pfs, struct uio *uio);
/* functions to check whether or not files should be displayed */
int linprocfs_validfile (struct proc *);

#define PROCFS_LOCKED	0x01
#define PROCFS_WANT	0x02

#define PFS_DEAD        0x80000000	/* or'd with pid */

int	linprocfs_root (struct mount *, struct vnode **);
int	linprocfs_rw (struct vop_read_args *);
#endif /* _KERNEL */
