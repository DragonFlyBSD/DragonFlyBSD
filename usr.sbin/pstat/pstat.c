/*-
 * Copyright (c) 1980, 1991, 1993, 1994
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
 * @(#) Copyright (c) 1980, 1991, 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @(#)pstat.c	8.16 (Berkeley) 5/9/95
 * $FreeBSD: src/usr.sbin/pstat/pstat.c,v 1.49.2.5 2002/07/12 09:12:49 des Exp $
 */

#include <sys/user.h>
#include <sys/kinfo.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <sys/ucred.h>
#include <sys/file.h>
#include <vfs/ufs/quota.h>
#include <vfs/ufs/inode.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/namei.h>
#include <vfs/union/union.h>
#include <sys/stat.h>
#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>
#include <sys/ioctl.h>
#include <sys/ioctl_compat.h>	/* XXX NTTYDISC is too well hidden */
#include <sys/tty.h>
#include <sys/conf.h>
#include <sys/blist.h>

#include <sys/sysctl.h>

#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef USE_KCORE
#  define KCORE_KINFO_WRAPPER
#  include <kcore.h>
#else
#  include <kinfo.h>
#endif

enum {
	NL_MOUNTLIST,
	NL_NUMVNODES,
	NL_RC_TTY,
	NL_NRC_TTY,
	NL_CY_TTY,
	NL_NCY_TTY,
	NL_SI_TTY,
	NL_SI_NPORTS,
};

const size_t NL_LAST_MANDATORY = NL_NUMVNODES;

struct nlist nl[] = {
	{ "_mountlist", 0, 0, 0, 0 },	/* address of head of mount list. */
	{ "_numvnodes", 0, 0, 0, 0 },
	{ "_rc_tty", 0, 0, 0, 0 },
	{ "_nrc_tty", 0, 0, 0, 0 },
	{ "_cy_tty", 0, 0, 0, 0 },
	{ "_ncy_tty", 0, 0, 0, 0 },
	{ "_si__tty", 0, 0, 0, 0 },
	{ "_si_Nports", 0, 0, 0, 0 },
	{ "", 0, 0, 0, 0 }
};

int	usenumflag;
int	totalflag;
int	swapflag;
char	*nlistf	= NULL;
char	*memf	= NULL;
kvm_t	*kd;

const char	*usagestr;

struct {
	int m_flag;
	const char *m_name;
} mnt_flags[] = {
	{ MNT_RDONLY, "rdonly" },
	{ MNT_SYNCHRONOUS, "sync" },
	{ MNT_NOEXEC, "noexec" },
	{ MNT_NOSUID, "nosuid" },
	{ MNT_NODEV, "nodev" },
	{ MNT_UNION, "union" },
	{ MNT_ASYNC, "async" },
	{ MNT_SUIDDIR, "suiddir" },
	{ MNT_SOFTDEP, "softdep" },
	{ MNT_NOSYMFOLLOW, "nosymfollow" },
	{ MNT_NOATIME, "noatime" },
	{ MNT_NOCLUSTERR, "noclusterread" },
	{ MNT_NOCLUSTERW, "noclusterwrite" },
	{ MNT_EXRDONLY, "exrdonly" },
	{ MNT_EXPORTED, "exported" },
	{ MNT_DEFEXPORTED, "defexported" },
	{ MNT_EXPORTANON, "exportanon" },
	{ MNT_EXKERB, "exkerb" },
	{ MNT_EXPUBLIC, "public" },
	{ MNT_LOCAL, "local" },
	{ MNT_QUOTA, "quota" },
	{ MNT_ROOTFS, "rootfs" },
	{ MNT_USER, "user" },
	{ MNT_IGNORE, "ignore" },
	{ MNT_UPDATE, "update" },
	{ MNT_DELEXPORT, "delexport" },
	{ MNT_RELOAD, "reload" },
	{ MNT_FORCE, "force" },
	{ 0, NULL }
};


#define	SVAR(var) __STRING(var)	/* to force expansion */
#define	KGET(idx, var)							\
	KGET1(idx, &var, sizeof(var), SVAR(var))
#define	KGET1(idx, p, s, msg)						\
	KGET2(nl[idx].n_value, p, s, msg)
#define	KGET2(addr, p, s, msg)						\
	if (kvm_read(kd, (u_long)(addr), p, s) != s)			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd))
#define	KGETN(idx, var)							\
	KGET1N(idx, &var, sizeof(var), SVAR(var))
#define	KGET1N(idx, p, s, msg)						\
	KGET2N(nl[idx].n_value, p, s, msg)
#define	KGET2N(addr, p, s, msg)						\
	((kvm_read(kd, (u_long)(addr), p, s) == s) ? 1 : 0)
#define	KGETRET(addr, p, s, msg)					\
	if (kvm_read(kd, (u_long)(addr), p, s) != s) {			\
		warnx("cannot read %s: %s", msg, kvm_geterr(kd));	\
		return (0);						\
	}

void	filemode(void);
int	getfiles(char **, int *);
struct mount *
	getmnt(struct mount *);
struct e_vnode *
	kinfo_vnodes(int *);
struct e_vnode *
	loadvnodes(int *);
void	mount_print(struct mount *);
void	nfs_header(void);
int	nfs_print(struct vnode *);
void	swapmode(void);
void	ttymode(void);
void	ttyprt(struct tty *, int);
void	ttytype(struct tty *, const char *, int, int, int);
void	ufs_header(void);
int	ufs_print(struct vnode *);
void	union_header(void);
int	union_print(struct vnode *);
static void usage(void);
void	vnode_header(void);
void	vnode_print(struct vnode *, struct vnode *);
void	vnodemode(void);

int
main(int argc, char **argv)
{
	int ch, ret;
	int fileflag, ttyflag, vnodeflag;
	char buf[_POSIX2_LINE_MAX];
	const char *opts;

	fileflag = swapflag = ttyflag = vnodeflag = 0;

	/* We will behave like good old swapinfo if thus invoked */
	opts = strrchr(argv[0],'/');
	if (opts)
		opts++;
	else
		opts = argv[0];
	if (!strcmp(opts,"swapinfo")) {
		swapflag = 1;
		opts = "kM:N:";
		usagestr = "swapinfo [-k] [-M core] [-N system]";
	} else {
		opts = "TM:N:fiknstv";
		usagestr = "pstat [-Tfknst] [-M core] [-N system]";
	}

	while ((ch = getopt(argc, argv, opts)) != -1)
		switch (ch) {
		case 'f':
			fileflag = 1;
			break;
		case 'k':
			if (setenv("BLOCKSIZE", "1K", 1) == -1)
				warn("setenv: cannot set BLOCKSIZE=1K");
			break;
		case 'M':
			memf = optarg;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			usenumflag = 1;
			break;
		case 's':
			++swapflag;
			break;
		case 'T':
			totalflag = 1;
			break;
		case 't':
			ttyflag = 1;
			break;
		case 'v':
		case 'i':		/* Backward compatibility. */
			errx(1, "vnode mode not supported");
#if 0
			vnodeflag = 1;
			break;
#endif
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	/*
	 * Discard setgid privileges if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	if (nlistf != NULL || memf != NULL)
		setgid(getgid());

	if ((kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, buf)) == NULL)
		errx(1, "kvm_openfiles: %s", buf);
#ifdef USE_KCORE
	if (kcore_wrapper_open(nlistf, memf, buf))
		errx(1, "kcore_open: %s", buf);
#endif
	if ((ret = kvm_nlist(kd, nl)) != 0) {
		size_t i;
		int quit = 0;

		if (ret == -1)
			errx(1, "kvm_nlist: %s", kvm_geterr(kd));
		for (i = 0; i < NL_LAST_MANDATORY; i++) {
			if (!nl[i].n_value) {
				quit = 1;
				warnx("undefined symbol: %s", nl[i].n_name);
			}
		}
		if (quit)
			exit(1);
	}
	if (!(fileflag | vnodeflag | ttyflag | swapflag | totalflag))
		usage();
	if (fileflag || totalflag)
		filemode();
	if (vnodeflag)
		vnodemode();
	if (ttyflag)
		ttymode();
	if (swapflag || totalflag)
		swapmode();
	exit (0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s\n", usagestr);
	exit (1);
}

struct e_vnode {
	struct vnode *avnode;
	struct vnode vnode;
};

void
vnodemode(void)
{
	struct e_vnode *e_vnodebase, *endvnode, *evp;
	struct vnode *vp;
	struct mount *maddr, *mp = NULL;
	int numvnodes;

	e_vnodebase = loadvnodes(&numvnodes);
	if (totalflag) {
		printf("%7d vnodes\n", numvnodes);
		return;
	}
	endvnode = e_vnodebase + numvnodes;
	printf("%d active vnodes\n", numvnodes);


#define ST	mp->mnt_stat
	maddr = NULL;
	for (evp = e_vnodebase; evp < endvnode; evp++) {
		vp = &evp->vnode;
		if (vp->v_mount != maddr) {
			/*
			 * New filesystem
			 */
			if ((mp = getmnt(vp->v_mount)) == NULL)
				continue;
			maddr = vp->v_mount;
			mount_print(mp);
			vnode_header();
			if (!strcmp(ST.f_fstypename, "ufs") ||
			    !strcmp(ST.f_fstypename, "mfs"))
				ufs_header();
			else if (!strcmp(ST.f_fstypename, "nfs"))
				nfs_header();
			else if (!strcmp(ST.f_fstypename, "union"))
				union_header();
			printf("\n");
		}
		vnode_print(evp->avnode, vp);
		if (!strcmp(ST.f_fstypename, "ufs") ||
		    !strcmp(ST.f_fstypename, "mfs"))
			ufs_print(vp);
		else if (!strcmp(ST.f_fstypename, "nfs"))
			nfs_print(vp);
		else if (!strcmp(ST.f_fstypename, "union"))
			union_print(vp);
		printf("\n");
	}
	free(e_vnodebase);
}

void
vnode_header(void)
{
	printf("ADDR     TYP VFLAG  USE HOLD");
}

void
vnode_print(struct vnode *avnode, struct vnode *vp)
{
	const char *type;
	char flags[32];
	char *fp = flags;
	int flag;
	int refs;

	/*
	 * set type
	 */
	switch (vp->v_type) {
	case VNON:
		type = "non"; break;
	case VREG:
		type = "reg"; break;
	case VDIR:
		type = "dir"; break;
	case VBLK:
		type = "blk"; break;
	case VCHR:
		type = "chr"; break;
	case VLNK:
		type = "lnk"; break;
	case VSOCK:
		type = "soc"; break;
	case VFIFO:
		type = "fif"; break;
	case VBAD:
		type = "bad"; break;
	default:
		type = "unk"; break;
	}
	/*
	 * gather flags
	 */
	flag = vp->v_flag;
	if (flag & VROOT)
		*fp++ = 'R';
	if (flag & VTEXT)
		*fp++ = 'T';
	if (flag & VSYSTEM)
		*fp++ = 'S';
	if (flag & VISTTY)
		*fp++ = 't';
#ifdef VXLOCK
	if (flag & VXLOCK)
		*fp++ = 'L';
	if (flag & VXWANT)
		*fp++ = 'W';
#endif
	if (flag & VOBJBUF)
		*fp++ = 'V';
	if (flag & (VAGE0 | VAGE1))
		*fp++ = 'a';
	if (flag & VOLOCK)
		*fp++ = 'l';
	if (flag & VOWANT)
		*fp++ = 'w';
#ifdef VDOOMED
	if (flag & VDOOMED)
		*fp++ = 'D';
#endif
	if (flag & VONWORKLST)
		*fp++ = 'O';
#ifdef VRECLAIMED
	if (flag & VINACTIVE)
		*fp++ = 'I';
	if (flag & VRECLAIMED)
		*fp++ = 'X';
#endif

	if (flag == 0)
		*fp++ = '-';
	*fp = '\0';

	/*
	 * Convert SYSREF ref counts into something more
	 * human readable for display.
	 */
	refs = vp->v_refcnt;
	printf("%8lx %s %5s %08x %4d",
	    (u_long)(void *)avnode, type, flags, refs, vp->v_auxrefs);
}

void
ufs_header(void)
{
	printf(" FILEID IFLAG RDEV|SZ");
}

int
ufs_print(struct vnode *vp)
{
	int flag;
	struct inode inode, *ip = &inode;
	char flagbuf[16], *flags = flagbuf;
	char *name;
	mode_t type;

	KGETRET(VTOI(vp), &inode, sizeof(struct inode), "vnode's inode");
	flag = ip->i_flag;
	if (flag & IN_ACCESS)
		*flags++ = 'A';
	if (flag & IN_CHANGE)
		*flags++ = 'C';
	if (flag & IN_UPDATE)
		*flags++ = 'U';
	if (flag & IN_MODIFIED)
		*flags++ = 'M';
	if (flag & IN_RENAME)
		*flags++ = 'R';
	if (flag & IN_SHLOCK)
		*flags++ = 'S';
	if (flag & IN_EXLOCK)
		*flags++ = 'E';
	if (flag & IN_HASHED)
		*flags++ = 'H';
	if (flag & IN_LAZYMOD)
		*flags++ = 'L';
	if (flag == 0)
		*flags++ = '-';
	*flags = '\0';

	printf(" %6ju %5s", (uintmax_t)ip->i_number, flagbuf);
	type = ip->i_mode & S_IFMT;
	if (S_ISCHR(ip->i_mode) || S_ISBLK(ip->i_mode))
		if (usenumflag || ((name = devname(ip->i_rdev, type)) == NULL))
			printf("   %2d,%-2d",
			    major(ip->i_rdev), minor(ip->i_rdev));
		else
			printf(" %7s", name);
	else
		printf(" %7qd", ip->i_size);
	return (0);
}

void
nfs_header(void)
{
	printf(" FILEID NFLAG RDEV|SZ");
}

int
nfs_print(struct vnode *vp)
{
	struct nfsnode nfsnode, *np = &nfsnode;
	char flagbuf[16], *flags = flagbuf;
	int flag;
	char *name;
	mode_t type;

	KGETRET(VTONFS(vp), &nfsnode, sizeof(nfsnode), "vnode's nfsnode");
	flag = np->n_flag;
	if (flag & NFLUSHWANT)
		*flags++ = 'W';
	if (flag & NFLUSHINPROG)
		*flags++ = 'P';
	if (flag & NLMODIFIED)
		*flags++ = 'M';
	if (flag & NRMODIFIED)
		*flags++ = 'R';
	if (flag & NWRITEERR)
		*flags++ = 'E';
	if (flag & NQNFSEVICTED)
		*flags++ = 'G';
	if (flag & NACC)
		*flags++ = 'A';
	if (flag & NUPD)
		*flags++ = 'U';
	if (flag & NCHG)
		*flags++ = 'C';
	if (flag & NLOCKED)
		*flags++ = 'L';
	if (flag & NWANTED)
		*flags++ = 'w';
	if (flag == 0)
		*flags++ = '-';
	*flags = '\0';

#define VT	np->n_vattr
	printf(" %6ju %5s", (uintmax_t)VT.va_fileid, flagbuf);
	type = VT.va_mode & S_IFMT;
	if (S_ISCHR(VT.va_mode) || S_ISBLK(VT.va_mode))
		if (usenumflag || ((name = devname((VT.va_rmajor << 8) | VT.va_rminor, type)) == NULL))
			printf("   %2d,%-2d",
			    VT.va_rmajor, VT.va_rminor);
		else
			printf(" %7s", name);
	else
		printf(" %7qd", np->n_size);
	return (0);
}

void
union_header(void)
{
	printf("    UPPER    LOWER");
}

int
union_print(struct vnode *vp)
{
	struct union_node unode, *up = &unode;

	KGETRET(VTOUNION(vp), &unode, sizeof(unode), "vnode's unode");

	printf(" %8lx %8lx", (u_long)(void *)up->un_uppervp,
	    (u_long)(void *)up->un_lowervp);
	return (0);
}
	
/*
 * Given a pointer to a mount structure in kernel space,
 * read it in and return a usable pointer to it.
 */
struct mount *
getmnt(struct mount *maddr)
{
	static struct mtab {
		struct mtab *next;
		struct mount *maddr;
		struct mount mount;
	} *mhead = NULL;
	struct mtab *mt;

	for (mt = mhead; mt != NULL; mt = mt->next)
		if (maddr == mt->maddr)
			return (&mt->mount);
	if ((mt = malloc(sizeof(struct mtab))) == NULL)
		errx(1, "malloc");
	KGETRET(maddr, &mt->mount, sizeof(struct mount), "mount table");
	mt->maddr = maddr;
	mt->next = mhead;
	mhead = mt;
	return (&mt->mount);
}

void
mount_print(struct mount *mp)
{
	int flags;

#define ST	mp->mnt_stat
	printf("*** MOUNT %s %s on %s", ST.f_fstypename,
	    ST.f_mntfromname, ST.f_mntonname);
	if ((flags = mp->mnt_flag)) {
		int i;
		const char *sep = " (";

		for (i = 0; mnt_flags[i].m_flag; i++) {
			if (flags & mnt_flags[i].m_flag) {
				printf("%s%s", sep, mnt_flags[i].m_name);
				flags &= ~mnt_flags[i].m_flag;
				sep = ",";
			}
		}
		if (flags)
			printf("%sunknown_flags:%x", sep, flags);
		printf(")");
	}
	printf("\n");
#undef ST
}

struct e_vnode *
loadvnodes(int *avnodes)
{
	int mib[2];
	size_t copysize;
	struct e_vnode *vnodebase;

	if (memf != NULL) {
		/*
		 * do it by hand
		 */
		return (kinfo_vnodes(avnodes));
	}
	mib[0] = CTL_KERN;
	mib[1] = KERN_VNODE;
	if (sysctl(mib, 2, NULL, &copysize, NULL, 0) == -1)
		err(1, "sysctl: KERN_VNODE");
	if ((vnodebase = malloc(copysize)) == NULL)
		errx(1, "malloc");
	if (sysctl(mib, 2, vnodebase, &copysize, NULL, 0) == -1)
		err(1, "sysctl: KERN_VNODE");
	if (copysize % sizeof(struct e_vnode))
		errx(1, "vnode size mismatch");
	*avnodes = copysize / sizeof(struct e_vnode);

	return (vnodebase);
}

/*
 * simulate what a running kernel does in in kinfo_vnode
 */
struct e_vnode *
kinfo_vnodes(int *avnodes)
{
	struct mntlist mountlist;
	struct mount *mp, mounth, *mp_next;
	struct vnode *vp, vnode, *vp_next;
	char *vbuf, *evbuf, *bp;
	int num, numvnodes;

#define VPTRSZ  sizeof(struct vnode *)
#define VNODESZ sizeof(struct vnode)

	KGET(NL_NUMVNODES, numvnodes);
	if ((vbuf = malloc((numvnodes + 20) * (VPTRSZ + VNODESZ))) == NULL)
		errx(1, "malloc");
	bp = vbuf;
	evbuf = vbuf + (numvnodes + 20) * (VPTRSZ + VNODESZ);
	KGET(NL_MOUNTLIST, mountlist);
	for (num = 0, mp = TAILQ_FIRST(&mountlist); ; mp = mp_next) {
		KGET2(mp, &mounth, sizeof(mounth), "mount entry");
		mp_next = TAILQ_NEXT(&mounth, mnt_list);
		for (vp = TAILQ_FIRST(&mounth.mnt_nvnodelist);
		    vp != NULL; vp = vp_next) {
			KGET2(vp, &vnode, sizeof(vnode), "vnode");
			vp_next = TAILQ_NEXT(&vnode, v_nmntvnodes);
			if ((bp + VPTRSZ + VNODESZ) > evbuf)
				/* XXX - should realloc */
				errx(1, "no more room for vnodes");
			memmove(bp, &vp, VPTRSZ);
			bp += VPTRSZ;
			memmove(bp, &vnode, VNODESZ);
			bp += VNODESZ;
			num++;
		}
		if (mp == TAILQ_LAST(&mountlist, mntlist))
			break;
	}
	*avnodes = num;
	return ((struct e_vnode *)vbuf);
}

char hdr[] =
"  LINE RAW CAN OUT IHIWT ILOWT OHWT LWT     COL STATE  SESS      PGID DISC\n";
int ttyspace = 128;

void
ttymode(void)
{
	struct tty *tty;
	struct tty ttyb[1000];
	int error;
	size_t len, i;

	printf(hdr);
	len = sizeof(ttyb);
	error = sysctlbyname("kern.ttys", &ttyb, &len, 0, 0);
	if (!error) {
		len /= sizeof(ttyb[0]);
		for (i = 0; i < len; i++) {
			ttyprt(&ttyb[i], 0);
		}
	}
	if ((tty = malloc(ttyspace * sizeof(*tty))) == NULL)
		errx(1, "malloc");
	if (nl[NL_NRC_TTY].n_type != 0)
		ttytype(tty, "rc", NL_RC_TTY, NL_NRC_TTY, 0);
	if (nl[NL_NCY_TTY].n_type != 0)
		ttytype(tty, "cy", NL_CY_TTY, NL_NCY_TTY, 0);
	if (nl[NL_SI_NPORTS].n_type != 0)
		ttytype(tty, "si", NL_SI_TTY, NL_SI_NPORTS, 1);
	free(tty);
}

void
ttytype(struct tty *tty, const char *name, int type, int number, int indir)
{
	struct tty *tp;
	int ntty;
	struct tty **ttyaddr;

	if (tty == NULL)
		return;
	KGET(number, ntty);
	printf("%d %s %s\n", ntty, name, (ntty == 1) ? "line" : "lines");
	if (ntty > ttyspace) {
		ttyspace = ntty;
		if ((tty = realloc(tty, ttyspace * sizeof(*tty))) == NULL)
			errx(1, "realloc");
	}
	if (indir) {
		KGET(type, ttyaddr);
		KGET2(ttyaddr, tty, (ssize_t)(ntty * sizeof(struct tty)),
		      "tty structs");
	} else {
		KGET1(type, tty, (ssize_t)(ntty * sizeof(struct tty)),
		      "tty structs");
	}
	printf(hdr);
	for (tp = tty; tp < &tty[ntty]; tp++)
		ttyprt(tp, tp - tty);
}

struct {
	int flag;
	char val;
} ttystates[] = {
#ifdef TS_WOPEN
	{ TS_WOPEN,	'W'},
#endif
	{ TS_ISOPEN,	'O'},
	{ TS_CARR_ON,	'C'},
#ifdef TS_CONNECTED
	{ TS_CONNECTED,	'c'},
#endif
	{ TS_TIMEOUT,	'T'},
	{ TS_FLUSH,	'F'},
	{ TS_BUSY,	'B'},
#ifdef TS_ASLEEP
	{ TS_ASLEEP,	'A'},
#endif
#ifdef TS_SO_OLOWAT
	{ TS_SO_OLOWAT,	'A'},
#endif
#ifdef TS_SO_OCOMPLETE
	{ TS_SO_OCOMPLETE, 'a'},
#endif
	{ TS_XCLUDE,	'X'},
	{ TS_TTSTOP,	'S'},
#ifdef TS_CAR_OFLOW
	{ TS_CAR_OFLOW,	'm'},
#endif
#ifdef TS_CTS_OFLOW
	{ TS_CTS_OFLOW,	'o'},
#endif
#ifdef TS_DSR_OFLOW
	{ TS_DSR_OFLOW,	'd'},
#endif
	{ TS_TBLOCK,	'K'},
	{ TS_ASYNC,	'Y'},
	{ TS_BKSL,	'D'},
	{ TS_ERASE,	'E'},
	{ TS_LNCH,	'L'},
	{ TS_TYPEN,	'P'},
	{ TS_CNTTB,	'N'},
#ifdef TS_CAN_BYPASS_L_RINT
	{ TS_CAN_BYPASS_L_RINT, 'l'},
#endif
#ifdef TS_SNOOP
	{ TS_SNOOP,     's'},
#endif
#ifdef TS_ZOMBIE
	{ TS_ZOMBIE,	'Z'},
#endif
	{ 0,	       '\0'},
};

void
ttyprt(struct tty *tp, int line)
{
	int i, j;
	pid_t pgid;
	char *name, state[20];

	if (usenumflag || tp->t_dev == 0 ||
	   (name = devname(tp->t_dev, S_IFCHR)) == NULL)
		printf("%7d ", line);
	else
		printf("%7s ", name);
	printf("%2d %3d ", tp->t_rawq.c_cc, tp->t_canq.c_cc);
	printf("%3d %5d %5d %4d %3d %7d ", tp->t_outq.c_cc,
		tp->t_ihiwat, tp->t_ilowat, tp->t_ohiwat, tp->t_olowat,
		tp->t_column);
	for (i = j = 0; ttystates[i].flag; i++)
		if (tp->t_state&ttystates[i].flag)
			state[j++] = ttystates[i].val;
	if (j == 0)
		state[j++] = '-';
	state[j] = '\0';
	printf("%-6s %8lx", state, (u_long)(void *)tp->t_session);
	pgid = 0;
	if (tp->t_pgrp != NULL)
		KGET2(&tp->t_pgrp->pg_id, &pgid, sizeof(pid_t), "pgid");
	printf("%6d ", pgid);
	switch (tp->t_line) {
	case TTYDISC:
		printf("term\n");
		break;
	case NTTYDISC:
		printf("ntty\n");
		break;
	case SLIPDISC:
		printf("slip\n");
		break;
	case PPPDISC:
		printf("ppp\n");
		break;
	default:
		printf("%d\n", tp->t_line);
		break;
	}
}

void
filemode(void)
{
	struct kinfo_file *fp, *ofp;
	size_t len;
	char flagbuf[16], *fbp;
	int maxfile, nfile;
	static const char *dtypes[] = { "???", "inode", "socket" };

	if (kinfo_get_maxfiles(&maxfile))
		err(1, "kinfo_get_maxfiles");
	if (totalflag) {
		if (kinfo_get_openfiles(&nfile))
			err(1, "kinfo_get_openfiles");
		printf("%3d/%3d files\n", nfile, maxfile);
		return;
	}
	if (kinfo_get_files(&fp, &len))
		err(1, "kinfo_get_files");
	ofp = fp;
	
	printf("%d/%d open files\n", len, maxfile);
	printf("   LOC   TYPE    FLG     CNT  MSG    DATA    OFFSET\n");
	for (; len-- > 0; fp++) {
		if ((unsigned)fp->f_type > DTYPE_SOCKET)
			continue;
		printf("%p ", fp->f_file);
		printf("%-8.8s", dtypes[fp->f_type]);
		fbp = flagbuf;
		if (fp->f_flag & FREAD)
			*fbp++ = 'R';
		if (fp->f_flag & FWRITE)
			*fbp++ = 'W';
		if (fp->f_flag & FAPPEND)
			*fbp++ = 'A';
#ifdef FSHLOCK	/* currently gone */
		if (fp->f_flag & FSHLOCK)
			*fbp++ = 'S';
		if (fp->f_flag & FEXLOCK)
			*fbp++ = 'X';
#endif
		if (fp->f_flag & FASYNC)
			*fbp++ = 'I';
		*fbp = '\0';
		printf("%6s  %3d", flagbuf, fp->f_count);
		printf("  %3d", fp->f_msgcount);
		printf("  %8lx", (u_long)(void *)fp->f_data);
		if (fp->f_offset < 0)
			printf("  %qx\n", fp->f_offset);
		else
			printf("  %qd\n", fp->f_offset);
	}
	free(ofp);
}

/*
 * swapmode is based on a program called swapinfo written
 * by Kevin Lahey <kml@rokkaku.atl.ga.us>.
 */
void
swapmode(void)
{
	struct kvm_swap kswap[16];
	int i;
	int n;
	int pagesize = getpagesize();
	const char *header;
	int hlen;
	long blocksize;

	n = kvm_getswapinfo(
	    kd, 
	    kswap,
	    sizeof(kswap)/sizeof(kswap[0]),
	    ((swapflag > 1) ? SWIF_DUMP_TREE : 0) | SWIF_DEV_PREFIX
	);

#define CONVERT(v)	((int)((quad_t)(v) * pagesize / blocksize))

	header = getbsize(&hlen, &blocksize);
	if (totalflag == 0) {
		printf("%-15s %*s %8s %8s %8s  %s\n",
		    "Device", hlen, header,
		    "Used", "Avail", "Capacity", "Type");

		for (i = 0; i < n; ++i) {
			printf(
			    "%-15s %*d ",
			    kswap[i].ksw_devname,
			    hlen,
			    CONVERT(kswap[i].ksw_total)
			);
			printf(
			    "%8d %8d %5.0f%%    %s\n",
			    CONVERT(kswap[i].ksw_used),
			    CONVERT(kswap[i].ksw_total - kswap[i].ksw_used),
			    (double)kswap[i].ksw_used * 100.0 /
				(double)kswap[i].ksw_total,
			    (kswap[i].ksw_flags & SW_SEQUENTIAL) ?
				"Sequential" : "Interleaved"
			);
		}
	}

	if (totalflag) {
		blocksize = 1024 * 1024;

		printf(
		    "%dM/%dM swap space\n", 
		    CONVERT(kswap[n].ksw_used),
		    CONVERT(kswap[n].ksw_total)
		);
	} else if (n > 1) {
		printf(
		    "%-15s %*d %8d %8d %5.0f%%\n",
		    "Total",
		    hlen, 
		    CONVERT(kswap[n].ksw_total),
		    CONVERT(kswap[n].ksw_used),
		    CONVERT(kswap[n].ksw_total - kswap[n].ksw_used),
		    (double)kswap[n].ksw_used * 100.0 /
			(double)kswap[n].ksw_total
		);
	}
}
