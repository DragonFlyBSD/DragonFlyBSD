/*-
 * Copyright (c) 1988, 1993
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
 * @(#) Copyright (c) 1988, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)fstat.c	8.3 (Berkeley) 5/2/95
 * $FreeBSD: src/usr.bin/fstat/fstat.c,v 1.21.2.7 2001/11/21 10:49:37 dwmalone Exp $
 */

#include <sys/user.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#include <sys/sysctl.h>
#include <sys/filedesc.h>
#include <sys/queue.h>
#include <sys/pipe.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/ktrace.h>
#include <vfs/ufs/quota.h>
#include <vfs/ufs/inode.h>
#include <sys/mount.h>
#include <sys/namecache.h>
#include <nfs/nfsproto.h>
#include <nfs/rpcv2.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>
#include <sys/devfs.h>

#include <vm/vm.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include "fstat.h"

#define	TEXT	-1
#define	CDIR	-2
#define	RDIR	-3
#define	TRACE	-4
#define	MMAP	-5

DEVS *devs;

static void make_printable(char *buf, int len);

#ifdef notdef
struct nlist nl[] = {
	{ "" },
};
#endif

int 	fsflg,	/* show files on same filesystem as file(s) argument */
	pflg,	/* show files open by a particular pid */
	uflg;	/* show files open by a particular (effective) user */
int 	checkfile; /* true if restricting to particular files or filesystems */
int	nflg;	/* (numerical) display f.s. and rdev as dev_t */
int	vflg;	/* display errors in locating kernel data objects etc... */
int	mflg;	/* include memory-mapped files */
int	wflg_mnt = 16;
int	wflg_cmd = 10;
int	pid_width = 5;
int	ino_width = 6;

struct fdnode *ofiles; 	/* buffer of pointers to file structures */
int maxfiles;

#define ALLOC_OFILES(d)	\
	if ((d) > maxfiles) { \
		free(ofiles); \
		ofiles = malloc((d) * sizeof(struct fdnode)); \
		if (ofiles == NULL) { \
			err(1, NULL); \
		} \
		maxfiles = (d); \
	}

kvm_t *kd;

static void dofiles(struct kinfo_proc *, struct proc *);
static void dommap(struct proc *);
static void vtrans(struct vnode *, struct nchandle *, int, int, off_t);
static int  ufs_filestat(struct vnode *, struct filestat *);
static int  nfs_filestat(struct vnode *, struct filestat *);
static int  devfs_filestat(struct vnode *, struct filestat *);
static char *getmnton(struct mount *, struct namecache_list *, struct nchandle *);
static void pipetrans(struct pipe *, int, int);
static void socktrans(struct socket *, int);
static void getinetproto(int);
static int  getfname(const char *);
static void usage(void) __dead2;


int
main(int argc, char **argv)
{
	struct passwd *passwd;
	struct kinfo_proc *p, *plast;
	struct proc proc;
	int arg, ch, what;
	char *memf, *nlistf;
	char buf[_POSIX2_LINE_MAX];
	int cnt;

	arg = 0;
	what = KERN_PROC_ALL;
	nlistf = memf = NULL;
	while ((ch = getopt(argc, argv, "fmnp:u:vwN:M:")) != -1)
		switch((char)ch) {
		case 'f':
			fsflg = 1;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'm':
			mflg = 1;
			break;
		case 'n':
			nflg = 1;
			break;
		case 'p':
			if (pflg++)
				usage();
			if (!isdigit(*optarg)) {
				warnx("-p requires a process id");
				usage();
			}
			what = KERN_PROC_PID;
			arg = atoi(optarg);
			break;
		case 'u':
			if (uflg++)
				usage();
			if (!(passwd = getpwnam(optarg)))
				errx(1, "%s: unknown uid", optarg);
			what = KERN_PROC_UID;
			arg = passwd->pw_uid;
			break;
		case 'v':
			vflg = 1;
			break;
		case 'w':
			wflg_mnt = 40;
			wflg_cmd = 16;
			break;
		case '?':
		default:
			usage();
		}

	if (*(argv += optind)) {
		for (; *argv; ++argv) {
			if (getfname(*argv))
				checkfile = 1;
		}
		if (!checkfile)	/* file(s) specified, but none accessable */
			exit(1);
	}

	ALLOC_OFILES(256);	/* reserve space for file pointers */

	if (fsflg && !checkfile) {
		/* -f with no files means use wd */
		if (getfname(".") == 0)
			exit(1);
		checkfile = 1;
	}

	/*
	 * Discard setgid privileges if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	if (nlistf != NULL || memf != NULL)
		setgid(getgid());

	if ((kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, buf)) == NULL)
		errx(1, "%s", buf);
#ifdef notdef
	if (kvm_nlist(kd, nl) != 0)
		errx(1, "no namelist: %s", kvm_geterr(kd));
#endif
	if ((p = kvm_getprocs(kd, what, arg, &cnt)) == NULL)
		errx(1, "%s", kvm_geterr(kd));
	if (nflg)
		printf("USER     %-*.*s %*.*s   FD DEV              %*.*s MODE   SZ|DV R/W", 
			wflg_cmd, wflg_cmd, "CMD",
			pid_width, pid_width, "PID",
			ino_width, ino_width, "INUM");
	else
		printf("USER     %-*.*s %*.*s   FD %-*.*s %*.*s MODE           SZ|DV R/W", 
			wflg_cmd, wflg_cmd, "CMD", 
			pid_width, pid_width, "PID",
			wflg_mnt, wflg_mnt, "PATH",
			ino_width, ino_width, "INUM");
	if (checkfile && fsflg == 0)
		printf(" NAME\n");
	else
		putchar('\n');

	for (plast = &p[cnt]; p < plast; ++p) {
		if (p->kp_stat == SZOMB)
			continue;
		if (!kread((void *)p->kp_paddr, &proc, sizeof(proc))) {
			dprintf(stderr, "can't read proc at %p for pid %d\n",
			    (void *)p->kp_paddr, Pid);
			continue;
		}
		dofiles(p, &proc);
		if (mflg)
			dommap(&proc);
	}
	exit(0);
}

const char *Uname;
char	*Comm;
int	Pid;

#define PREFIX(i) \
	printf("%-8.8s %-*s %*d", Uname, wflg_cmd, Comm, pid_width, Pid); \
	switch(i) { \
	case TEXT: \
		printf(" text"); \
		break; \
	case CDIR: \
		printf("   wd"); \
		break; \
	case RDIR: \
		printf(" root"); \
		break; \
	case TRACE: \
		printf("   tr"); \
		break; \
	case MMAP: \
		printf(" mmap"); \
		break; \
	default: \
		printf(" %4d", i); \
		break; \
	}

/*
 * print open files attributed to this process
 */
static void
dofiles(struct kinfo_proc *kp, struct proc *p)
{
	int i;
	struct file file;
	struct filedesc filed;
	struct ktrace_node ktrace_node;

	Uname = user_from_uid(kp->kp_uid, 0);
	Pid = kp->kp_pid;
	Comm = kp->kp_comm;
	make_printable(Comm, strlen(Comm));

	if (p->p_fd == NULL)
		return;
	if (!kread(p->p_fd, &filed, sizeof (filed))) {
		dprintf(stderr, "can't read filedesc at %p for pid %d\n",
		    (void *)p->p_fd, Pid);
		return;
	}
	/*
	 * root directory vnode, if one
	 */
	if (filed.fd_rdir)
		vtrans(filed.fd_rdir, &filed.fd_nrdir, RDIR, FREAD, 0);
	/*
	 * current working directory vnode
	 */
	vtrans(filed.fd_cdir, &filed.fd_ncdir, CDIR, FREAD, 0);
	/*
	 * ktrace vnode, if one
	 */
	if (p->p_tracenode) {
		if (kread(p->p_tracenode, &ktrace_node, sizeof (ktrace_node)))
			vtrans(ktrace_node.kn_vp, NULL, TRACE, FREAD|FWRITE, 0);
	}
	/*
	 * text vnode, if one
	 */
	if (p->p_textvp)
		vtrans(p->p_textvp, NULL, TEXT, FREAD, 0);
	/*
	 * open files
	 */
	ALLOC_OFILES(filed.fd_lastfile+1);
	if (!kread(filed.fd_files, ofiles,
	    (filed.fd_lastfile+1) * sizeof(struct fdnode))) {
		dprintf(stderr,
		    "can't read file structures at %p for pid %d\n",
		    (void *)filed.fd_files, Pid);
		return;
	}
	for (i = 0; i <= filed.fd_lastfile; i++) {
		if (ofiles[i].fp == NULL)
			continue;
		if (!kread(ofiles[i].fp, &file, sizeof (struct file))) {
			dprintf(stderr, "can't read file %d at %p for pid %d\n",
			    i, (void *)ofiles[i].fp, Pid);
			continue;
		}
		if (file.f_type == DTYPE_VNODE) {
			vtrans((struct vnode *)file.f_data, &file.f_nchandle,
				i, file.f_flag, file.f_offset);
		} else if (file.f_type == DTYPE_SOCKET) {
			if (checkfile == 0)
				socktrans((struct socket *)file.f_data, i);
		}
#ifdef DTYPE_PIPE
		else if (file.f_type == DTYPE_PIPE) {
			if (checkfile == 0)
				pipetrans((struct pipe *)file.f_data, i,
				    file.f_flag);
		}
#endif
#ifdef DTYPE_FIFO
		else if (file.f_type == DTYPE_FIFO) {
			if (checkfile == 0)
				vtrans((struct vnode *)file.f_data,
					&file.f_nchandle,
					i, file.f_flag, file.f_offset);
		}
#endif
		else {
			dprintf(stderr,
			    "unknown file type %d for file %d of pid %d\n",
			    file.f_type, i, Pid);
		}
	}
}

static void
dommap(struct proc *p)
{
	struct vmspace vmspace;
	vm_map_t map;
	struct vm_map_entry entry;
	vm_map_entry_t entryp;
	struct vm_object object;
	vm_object_t objp;
	int prot, fflags;

	if (!kread(p->p_vmspace, &vmspace, sizeof(vmspace))) {
		dprintf(stderr, "can't read vmspace at %p for pid %d\n",
		    (void *)p->p_vmspace, Pid);
		return;
	}

	map = &vmspace.vm_map;

	for (entryp = map->header.next; entryp != &p->p_vmspace->vm_map.header;
	    entryp = entry.next) {
		if (!kread(entryp, &entry, sizeof(entry))) {
			dprintf(stderr,
			    "can't read vm_map_entry at %p for pid %d\n",
			    (void *)entryp, Pid);
			return;
		}

		if (entry.maptype == VM_MAPTYPE_SUBMAP)
			continue;

		if ((objp = entry.object.vm_object) == NULL)
			continue;

		for (; objp; objp = object.backing_object) {
			if (!kread(objp, &object, sizeof(object))) {
				dprintf(stderr,
				    "can't read vm_object at %p for pid %d\n",
				    (void *)objp, Pid);
				return;
			}
		}

		prot = entry.protection;
		fflags = (prot & VM_PROT_READ ? FREAD : 0) |
		    (prot & VM_PROT_WRITE ? FWRITE : 0);

		switch (object.type) {
		case OBJT_VNODE:
			vtrans((struct vnode *)object.handle, NULL, 
				MMAP, fflags, 0);
			break;
		default:
			break;
		}
	}
}

static void
vtrans(struct vnode *vp, struct nchandle *ncr, int i, int flag, off_t off)
{
	struct vnode vn;
	struct filestat fst;
	char rw[3], mode[15];
	const char *badtype = NULL, *filename;
	char *name;

	fst.offset = off;
	filename = badtype = NULL;
	if (!kread(vp, &vn, sizeof (struct vnode))) {
		dprintf(stderr, "can't read vnode at %p for pid %d\n",
		    (void *)vp, Pid);
		return;
	}
	if (vn.v_type == VNON || vn.v_tag == VT_NON)
		badtype = "none";
	else if (vn.v_type == VBAD)
		badtype = "bad";
	else
		switch (vn.v_tag) {
		case VT_HAMMER:
			if (!hammer_filestat(&vn, &fst))
				badtype = "error";
			break;
		case VT_TMPFS:
			if (!tmpfs_filestat(&vn, &fst))
				badtype = "error";
			break;
		case VT_UFS:
			if (!ufs_filestat(&vn, &fst))
				badtype = "error";
			break;
		case VT_MFS:
			if (!ufs_filestat(&vn, &fst))
				badtype = "error";
			break;
		case VT_NFS:
			if (!nfs_filestat(&vn, &fst))
				badtype = "error";
			break;
		case VT_NTFS:
			if (!ntfs_filestat(&vn, &fst))
				badtype = "error";
			break;
		case VT_EXT2FS:
			if (!ext2fs_filestat(&vn, &fst))
				badtype = "error";
			break;

		case VT_MSDOSFS:
			if (!msdosfs_filestat(&vn, &fst))
				badtype = "error";
			break;

		case VT_ISOFS:
			if (!isofs_filestat(&vn, &fst))
				badtype = "error";
			break;

		case VT_DEVFS:
			if (!devfs_filestat(&vn, &fst))
				badtype = "error";
			break;

		default: {
			static char unknown[10];
			sprintf(unknown, "?(%x)", vn.v_tag);
			badtype=unknown;
			break;
		}
	}
	if (checkfile) {
		int fsmatch = 0;
		DEVS *d;

		if (badtype)
			return;
		for (d = devs; d != NULL; d = d->next)
			if (d->fsid == fst.fsid) {
				fsmatch = 1;
				if (d->ino == (ino_t)fst.fileid) {
					filename = d->name;
					break;
				}
			}
		if (fsmatch == 0 || (filename == NULL && fsflg == 0))
			return;
	}
	PREFIX(i);
	if (badtype) {
		(void)printf(" %-*s  %10s    %jd\n",
			     wflg_mnt,
			     getmnton(vn.v_mount, &vn.v_namecache, ncr),
			     badtype,
			     (intmax_t)off);
		return;
	}
	if (nflg)
		(void)printf(" %3u,%-9u   ", major(fst.fsid), minor(fst.fsid));
	else
		(void)printf(" %-*s", wflg_mnt, getmnton(vn.v_mount, &vn.v_namecache, ncr));
	if (nflg)
		(void)sprintf(mode, "%o", fst.mode);
	else
		strmode(fst.mode, mode);
	(void)printf(" %*ld %10s", ino_width, fst.fileid, mode);
	switch (vn.v_type) {
	case VBLK:
	case VCHR:
		if (nflg || ((name = devname(fst.rdev, vn.v_type == VCHR ?
		    S_IFCHR : S_IFBLK)) == NULL))
			printf(" %3u,%-4u", major(fst.rdev), minor(fst.rdev));
		else
			printf(" %8s", name);
		break;
	case VREG:
		printf(" %jd", (intmax_t)fst.offset);
		break;
	default:
		printf(" %8ju", (uintmax_t)fst.size);
	}
	rw[0] = '\0';
	if (flag & FREAD)
		strcat(rw, "r");
	if (flag & FWRITE)
		strcat(rw, "w");
	printf(" %2s", rw);
	if (filename && !fsflg)
		printf("  %s", filename);
	putchar('\n');
}

static int
ufs_filestat(struct vnode *vp, struct filestat *fsp)
{
	struct inode inode;

	if (!kread(VTOI(vp), &inode, sizeof (inode))) {
		dprintf(stderr, "can't read inode at %p for pid %d\n",
		    (void *)VTOI(vp), Pid);
		return 0;
	}
	/*
	 * The st_dev from stat(2) is a udev_t. These kernel structures
	 * contain dev_t structures. We need to convert to udev to make
	 * comparisons
	 */
	fsp->fsid = dev2udev(inode.i_dev);
	fsp->fileid = (long)inode.i_number;
	fsp->mode = (mode_t)inode.i_mode;
	fsp->size = inode.i_size;
	fsp->rdev = inode.i_rdev;

	return 1;
}

static int
nfs_filestat(struct vnode *vp, struct filestat *fsp)
{
	struct nfsnode nfsnode;

	if (!kread(VTONFS(vp), &nfsnode, sizeof (nfsnode))) {
		dprintf(stderr, "can't read nfsnode at %p for pid %d\n",
		    (void *)VTONFS(vp), Pid);
		return 0;
	}
	fsp->fsid = nfsnode.n_vattr.va_fsid;
	fsp->fileid = nfsnode.n_vattr.va_fileid;
	fsp->size = nfsnode.n_size;
	fsp->rdev = makeudev(nfsnode.n_vattr.va_rmajor,
			     nfsnode.n_vattr.va_rminor);
	fsp->mode = nfsnode.n_vattr.va_mode | mtrans(vp->v_type);

	return 1;
}

static int
devfs_filestat(struct vnode *vp, struct filestat *fsp)
{
	struct devfs_node devfs_node;

	if (!kread(vp->v_data, &devfs_node, sizeof (devfs_node))) {
		dprintf(stderr, "can't read devfs_node at %p for pid %d\n",
		    (void *)vp->v_data, Pid);
		return 0;
	}
	fsp->fsid = fsp->rdev = dev2udev(vp->v_rdev);
	fsp->fileid = devfs_node.d_dir.d_ino;
	fsp->mode = (devfs_node.mode & ~S_IFMT) | S_IFCHR;
	fsp->size = 0;

	return 1;
}

static char *
getmnton(struct mount *m, struct namecache_list *ncplist, struct nchandle *ncr)
{
	static struct mount mount_l;
	static struct mtab {
		struct mtab *next;
		struct mount *m;
		char mntonname[MNAMELEN];
	} *mhead = NULL;
	struct mtab *mt;
	struct namecache *ncp;
	struct namecache ncp_copy;
	static char path[1024];
	int i;

	/*
	 * If no ncp is passed try to find one via ncplist.  Make sure
	 * we are using the correct mount pointer or the matching code
	 * will not know how to transition mount points properly.
	 */
	if (ncr == NULL || ncr->ncp == NULL) {
		ncp = ncplist->tqh_first;
	} else {
		ncp = ncr->ncp;
		if (ncr->mount)
			m = ncr->mount;
	}

	/*
	 * If we have an ncp, traceback the path.  This is a kvm pointer.
	 */
	if (ncp) {
		if (!kread(m, &mount_l, sizeof(struct mount))) {
			warnx("can't read mount table at %p", (void *)m);
			return (NULL);
		}
		i = sizeof(path) - 1;
		path[i] = 0;
		while (ncp) {
			/*
			 * If this is the root of the mount then traverse
			 * to the parent mount.
			 */
			if (ncp == mount_l.mnt_ncmountpt.ncp) {
				ncp = mount_l.mnt_ncmounton.ncp;
				if (ncp == NULL)
					break;
				m = mount_l.mnt_ncmounton.mount;
				if (!kread(m, &mount_l, sizeof(struct mount))) {
					warnx("can't read mount table at %p", (void *)m);
					return (NULL);
				}
			}

			/*
			 * Ok, pull out the ncp and extract the name
			 */
			if (!kread(ncp, &ncp_copy, sizeof(ncp_copy))) {
				warnx("can't read ncp at %p", ncp);
				return (NULL);
			}
			if (i <= ncp_copy.nc_nlen)
				break;
			i -= ncp_copy.nc_nlen;
			if (!kread(ncp_copy.nc_name, path + i, ncp_copy.nc_nlen)) {
				warnx("can't read ncp %p path component at %p", ncp, ncp_copy.nc_name);
				return (NULL);
			}
			make_printable(path + i, ncp_copy.nc_nlen);
			path[--i] = '/';
			ncp = ncp_copy.nc_parent;
		}
		if (i == sizeof(path) - 1)
			path[--i] = '/';
		return(path + i);
	}

	/*
	 * If all else fails print out the mount point path
	 */
	for (mt = mhead; mt != NULL; mt = mt->next) {
		if (m == mt->m)
			return (mt->mntonname);
	}
	if (!kread(m, &mount_l, sizeof(struct mount))) {
		warnx("can't read mount table at %p", (void *)m);
		return (NULL);
	}
	if ((mt = malloc(sizeof (struct mtab))) == NULL)
		err(1, NULL);
	mt->m = m;
	bcopy(&mount_l.mnt_stat.f_mntonname[0], &mt->mntonname[0], MNAMELEN);
	mt->next = mhead;
	mhead = mt;
	return (mt->mntonname);
}

static void
pipetrans(struct pipe *pi, int i, int flag)
{
	struct pipe pip;
	char rw[3];

	PREFIX(i);

	/* fill in socket */
	if (!kread(pi, &pip, sizeof(struct pipe))) {
		dprintf(stderr, "can't read pipe at %p\n", (void *)pi);
		goto bad;
	}

	printf("* pipe %8lx <-> %8lx", (u_long)pi, (u_long)pip.pipe_peer);
	printf(" %6d", (int)(pip.pipe_buffer.windex - pip.pipe_buffer.rindex));
	rw[0] = '\0';
	if (flag & FREAD)
		strcat(rw, "r");
	if (flag & FWRITE)
		strcat(rw, "w");
	printf(" %2s", rw);
	putchar('\n');
	return;

bad:
	printf("* error\n");
}

static void
socktrans(struct socket *sock, int i)
{
	static const char *stypename[] = {
		"unused",	/* 0 */
		"stream", 	/* 1 */
		"dgram",	/* 2 */
		"raw",		/* 3 */
		"rdm",		/* 4 */
		"seqpak"	/* 5 */
	};
#define	STYPEMAX 5
	struct socket	so;
	struct protosw	proto;
	struct domain	dom;
	struct inpcb	inpcb;
	struct unpcb	unpcb;
	int len;
	char dname[32];

	PREFIX(i);

	/* fill in socket */
	if (!kread(sock, &so, sizeof(struct socket))) {
		dprintf(stderr, "can't read sock at %p\n", (void *)sock);
		goto bad;
	}

	/* fill in protosw entry */
	if (!kread(so.so_proto, &proto, sizeof(struct protosw))) {
		dprintf(stderr, "can't read protosw at %p",
		    (void *)so.so_proto);
		goto bad;
	}

	/* fill in domain */
	if (!kread(proto.pr_domain, &dom, sizeof(struct domain))) {
		dprintf(stderr, "can't read domain at %p\n",
		    (const void *)proto.pr_domain);
		goto bad;
	}

	if ((len = kvm_read(kd, (u_long)dom.dom_name, dname,
	    sizeof(dname) - 1)) < 0) {
		dprintf(stderr, "can't read domain name at %p\n",
		    (void *)dom.dom_name);
		dname[0] = '\0';
	}
	else
		dname[len] = '\0';

	if ((u_short)so.so_type > STYPEMAX)
		printf("* %s ?%d", dname, so.so_type);
	else
		printf("* %s %s", dname, stypename[so.so_type]);

	/*
	 * protocol specific formatting
	 *
	 * Try to find interesting things to print.  For tcp, the interesting
	 * thing is the address of the tcpcb, for udp and others, just the
	 * inpcb (socket pcb).  For unix domain, its the address of the socket
	 * pcb and the address of the connected pcb (if connected).  Otherwise
	 * just print the protocol number and address of the socket itself.
	 * The idea is not to duplicate netstat, but to make available enough
	 * information for further analysis.
	 */
	switch(dom.dom_family) {
	case AF_INET:
	case AF_INET6:
		getinetproto(proto.pr_protocol);
		if (proto.pr_protocol == IPPROTO_TCP ) {
			if (so.so_pcb) {
				if (kvm_read(kd, (u_long)so.so_pcb,
				    (char *)&inpcb, sizeof(struct inpcb))
				    != sizeof(struct inpcb)) {
					dprintf(stderr,
					    "can't read inpcb at %p\n",
					    (void *)so.so_pcb);
					goto bad;
				}
				printf(" %lx", (u_long)inpcb.inp_ppcb);
			}
		}
		else if (so.so_pcb)
			printf(" %lx", (u_long)so.so_pcb);
		break;
	case AF_UNIX:
		/* print address of pcb and connected pcb */
		if (so.so_pcb) {
			printf(" %lx", (u_long)so.so_pcb);
			if (kvm_read(kd, (u_long)so.so_pcb, (char *)&unpcb,
			    sizeof(struct unpcb)) != sizeof(struct unpcb)){
				dprintf(stderr, "can't read unpcb at %p\n",
				    (void *)so.so_pcb);
				goto bad;
			}
			if (unpcb.unp_conn) {
				char shoconn[4], *cp;

				cp = shoconn;
				if (!(so.so_state & SS_CANTRCVMORE))
					*cp++ = '<';
				*cp++ = '-';
				if (!(so.so_state & SS_CANTSENDMORE))
					*cp++ = '>';
				*cp = '\0';
				printf(" %s %lx", shoconn,
				    (u_long)unpcb.unp_conn);
			}
		}
		break;
	default:
		/* print protocol number and socket address */
		printf(" %d %lx", proto.pr_protocol, (u_long)sock);
	}
	printf("\n");
	return;
bad:
	printf("* error\n");
}


/*
 * Read the cdev structure in the kernel (as pointed to by a dev_t)
 * in order to work out the associated udev_t
 */
udev_t
dev2udev(void *dev)
{
	struct cdev si;

	if (kread(dev, &si, sizeof si)) {
		if ((si.si_umajor & 0xffffff00) ||
		    (si.si_uminor & 0x0000ff00)) {
			return NOUDEV;
		}
		return((si.si_umajor << 8) | si.si_uminor);
	} else {
		dprintf(stderr, "can't convert dev_t %p to a udev_t\n", dev);
		return NOUDEV;
	}
}

udev_t
makeudev(int x, int y)
{
        if ((x & 0xffffff00) || (y & 0x0000ff00))
		return NOUDEV;
	return ((x << 8) | y);
}

/*
 * getinetproto --
 *	print name of protocol number
 */
static void
getinetproto(int number)
{
	static int isopen;
	struct protoent *pe;

	if (!isopen)
		setprotoent(++isopen);
	if ((pe = getprotobynumber(number)) != NULL)
		printf(" %s", pe->p_name);
	else
		printf(" %d", number);
}

static int
getfname(const char *filename)
{
	struct stat statbuf;
	DEVS *cur;

	if (stat(filename, &statbuf)) {
		warn("%s", filename);
		return(0);
	}
	if ((cur = malloc(sizeof(DEVS))) == NULL)
		err(1, NULL);
	cur->next = devs;
	devs = cur;

	cur->ino = statbuf.st_ino;
	cur->fsid = statbuf.st_dev;
	cur->name = filename;
	return(1);
}

static void
usage(void)
{
	(void)fprintf(stderr,
 "usage: fstat [-fmnv] [-p pid] [-u user] [-N system] [-M core] [file ...]\n");
	exit(1);
}

static 
void
make_printable(char *buf, int len)
{
    while (len > 0) {
	if (!isprint(*buf)) 
		*buf = '?';
	++buf;
	--len;
    }
}

ssize_t
kread(const void *kaddr, void *uaddr, size_t nbytes)
{
    if (nbytes > 0x10000000)
	return(0);

    if (kvm_read(kd, (u_long)kaddr, (char *)uaddr, nbytes) == (ssize_t)nbytes)
	return(1);
    else
	return(0);
}

