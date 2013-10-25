/*
 * Copyright (c) 1980, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 * @(#) Copyright (c) 1980, 1990, 1993, 1994 The Regents of the University of California.  All rights reserved.
 * @(#)df.c	8.9 (Berkeley) 5/8/95
 * $FreeBSD: src/bin/df/df.c,v 1.23.2.9 2002/07/01 00:14:24 iedowse Exp $
 */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/statvfs.h>

#include <vfs/ufs/dinode.h>
#include <vfs/ufs/fs.h>
#include <vfs/ufs/ufsmount.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fstab.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define UNITS_SI 1
#define UNITS_2 2

/* Maximum widths of various fields. */
struct maxwidths {
	int mntfrom;
	int total;
	int used;
	int avail;
	int iused;
	int ifree;
};

/* vfslist.c */
char	**makevfslist(char *);
int	  checkvfsname(const char *, char **);

static int	 bread(off_t, void *, int);
static char	*getmntpt(char *);
static int	 quadwidth(int64_t);
static char	*makenetvfslist(void);
static void	 prthuman(struct statvfs *, int64_t);
static void	 prthumanval(int64_t);
static void	 prtstat(struct statfs *, struct statvfs *, struct maxwidths *);
static long	 regetmntinfo(struct statfs **, struct statvfs **, long, char **);
static int	 ufs_df(char *, struct maxwidths *);
static void	 update_maxwidths(struct maxwidths *, struct statfs *, struct statvfs *);
static void	 usage(void);

int	aflag = 0, hflag, iflag, nflag;
struct	ufs_args mdev;

static __inline int
imax(int a, int b)
{
	return (a > b ? a : b);
}

static __inline int64_t
qmax(int64_t a, int64_t b)
{
	return (a > b ? a : b);
}

int
main(int argc, char **argv)
{
	struct stat stbuf;
	struct statfs statfsbuf, *mntbuf;
	struct statvfs statvfsbuf, *mntvbuf;
	struct maxwidths maxwidths;
	const char *fstype;
	char *mntpath, *mntpt, **vfslist;
	long mntsize;
	int ch, i, rv;

	fstype = "ufs";

	vfslist = NULL;
	while ((ch = getopt(argc, argv, "abgHhiklmnPt:")) != -1)
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'b':
				/* FALLTHROUGH */
		case 'P':
			if (setenv("BLOCKSIZE", "512", 1) != 0)
				warn("setenv: cannot set BLOCKSIZE=512");
			hflag = 0;
			break;
		case 'g':
			if (setenv("BLOCKSIZE", "1g", 1) != 0)
				warn("setenv: cannot set BLOCKSIZE=1g");
			hflag = 0;
			break;
		case 'H':
			hflag = UNITS_SI;
			break;
		case 'h':
			hflag = UNITS_2;
			break;
		case 'i':
			iflag = 1;
			break;
		case 'k':
			if (setenv("BLOCKSIZE", "1k", 1) != 0)
				warn("setenv: cannot set BLOCKSIZE=1k");
			hflag = 0;
			break;
		case 'l':
			if (vfslist != NULL)
				errx(1, "-l and -t are mutually exclusive.");
			vfslist = makevfslist(makenetvfslist());
			break;
		case 'm':
			if (setenv("BLOCKSIZE", "1m", 1) != 0)
				warn("setenv: cannot set BLOCKSIZE=1m");
			hflag = 0;
			break;
		case 'n':
			nflag = 1;
			break;
		case 't':
			if (vfslist != NULL)
				errx(1, "only one -t option may be specified");
			fstype = optarg;
			vfslist = makevfslist(optarg);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	mntsize = getmntvinfo(&mntbuf, &mntvbuf, MNT_NOWAIT);
	bzero(&maxwidths, sizeof(maxwidths));
	for (i = 0; i < mntsize; i++)
		update_maxwidths(&maxwidths, &mntbuf[i], &mntvbuf[i]);

	rv = 0;
	if (!*argv) {
		mntsize = regetmntinfo(&mntbuf, &mntvbuf, mntsize, vfslist);
		bzero(&maxwidths, sizeof(maxwidths));
		for (i = 0; i < mntsize; i++)
			update_maxwidths(&maxwidths, &mntbuf[i], &mntvbuf[i]);
		for (i = 0; i < mntsize; i++) {
			if (aflag || (mntbuf[i].f_flags & MNT_IGNORE) == 0)
				prtstat(&mntbuf[i], &mntvbuf[i], &maxwidths);
		}
		exit(rv);
	}

	for (; *argv; argv++) {
		if (stat(*argv, &stbuf) < 0) {
			if ((mntpt = getmntpt(*argv)) == NULL) {
				warn("%s", *argv);
				rv = 1;
				continue;
			}
		} else if (S_ISCHR(stbuf.st_mode)) {
			if ((mntpt = getmntpt(*argv)) == NULL) {
				mdev.fspec = *argv;
				mntpath = strdup("/tmp/df.XXXXXX");
				if (mntpath == NULL) {
					warn("strdup failed");
					rv = 1;
					continue;
				}
				mntpt = mkdtemp(mntpath);
				if (mntpt == NULL) {
					warn("mkdtemp(\"%s\") failed", mntpath);
					rv = 1;
					free(mntpath);
					continue;
				}
				if (mount(fstype, mntpt, MNT_RDONLY,
				    &mdev) != 0) {
					rv = ufs_df(*argv, &maxwidths) || rv;
					rmdir(mntpt);
					free(mntpath);
					continue;
				} else if (statfs(mntpt, &statfsbuf) == 0 &&
					   statvfs(mntpt, &statvfsbuf) == 0) {
					statfsbuf.f_mntonname[0] = '\0';
					prtstat(&statfsbuf, &statvfsbuf, &maxwidths);
				} else {
					warn("%s", *argv);
					rv = 1;
				}
				unmount(mntpt, 0);
				rmdir(mntpt);
				free(mntpath);
				continue;
			}
		} else
			mntpt = *argv;
		/*
		 * Statfs does not take a `wait' flag, so we cannot
		 * implement nflag here.
		 */
		if (statfs(mntpt, &statfsbuf) < 0) {
			warn("%s", mntpt);
			rv = 1;
			continue;
		}
		if (statvfs(mntpt, &statvfsbuf) < 0) {
			warn("%s", mntpt);
			rv = 1;
			continue;
		}
		/*
		 * Check to make sure the arguments we've been given are
		 * satisfied. Return an error if we have been asked to
		 * list a mount point that does not match the other args
		 * we've been given (-l, -t, etc.).
		 */
		if (checkvfsname(statfsbuf.f_fstypename, vfslist)) {
			rv = 1;
			continue;
		}

		if (argc == 1) {
			bzero(&maxwidths, sizeof(maxwidths));
			update_maxwidths(&maxwidths, &statfsbuf, &statvfsbuf);
		}
		prtstat(&statfsbuf, &statvfsbuf, &maxwidths);
	}
	return (rv);
}

static char *
getmntpt(char *name)
{
	long mntsize, i;
	struct statfs *mntbuf;
	struct statvfs *mntvbuf;

	mntsize = getmntvinfo(&mntbuf, &mntvbuf, MNT_NOWAIT);
	for (i = 0; i < mntsize; i++) {
		if (!strcmp(mntbuf[i].f_mntfromname, name))
			return (mntbuf[i].f_mntonname);
	}
	return (0);
}

/*
 * Make a pass over the filesystem info in ``mntbuf'' filtering out
 * filesystem types not in vfslist and possibly re-stating to get
 * current (not cached) info.  Returns the new count of valid statfs bufs.
 */
static long
regetmntinfo(struct statfs **mntbufp, struct statvfs **mntvbufp, long mntsize, char **vfslist)
{
	int i, j;
	struct statfs *mntbuf;
	struct statvfs *mntvbuf;

	if (vfslist == NULL)
		return (nflag ? mntsize : getmntvinfo(mntbufp, mntvbufp, MNT_WAIT));

	mntbuf = *mntbufp;
	mntvbuf = *mntvbufp;
	for (j = 0, i = 0; i < mntsize; i++) {
		if (checkvfsname(mntbuf[i].f_fstypename, vfslist))
			continue;
		if (!nflag) {
			statfs(mntbuf[i].f_mntonname,&mntbuf[j]);
			statvfs(mntbuf[i].f_mntonname,&mntvbuf[j]);
		} else if (i != j) {
			mntbuf[j] = mntbuf[i];
			mntvbuf[j] = mntvbuf[i];
		}
		j++;
	}
	return (j);
}

static void
prthuman(struct statvfs *vsfsp, int64_t used)
{
	prthumanval(vsfsp->f_blocks * vsfsp->f_bsize);
	prthumanval(used * vsfsp->f_bsize);
	prthumanval(vsfsp->f_bavail * vsfsp->f_bsize);
}

static void
prthumanval(int64_t bytes)
{
	char buf[6];
	int flags;

	flags = HN_B | HN_NOSPACE | HN_DECIMAL;
	if (hflag == UNITS_SI)
		flags |= HN_DIVISOR_1000;

	humanize_number(buf, sizeof(buf) - (bytes < 0 ? 0 : 1),
	    bytes, "", HN_AUTOSCALE, flags);

	printf(" %6s", buf);
}

/*
 * Convert statfs returned filesystem size into BLOCKSIZE units.
 * Attempts to avoid overflow for large filesystems.
 */
static intmax_t
fsbtoblk(int64_t num, uint64_t bsize, u_long reqbsize)
{
	if (bsize != 0 && bsize < reqbsize)
		return (num / (intmax_t)(reqbsize / bsize));
	else
		return (num * (intmax_t)(bsize / reqbsize));
}

/*
 * Print out status about a filesystem.
 */
static void
prtstat(struct statfs *sfsp, struct statvfs *vsfsp, struct maxwidths *mwp)
{
	static long blocksize;
	static int headerlen, timesthrough;
	static const char *header;
	int64_t used, availblks, inodes;

	if (++timesthrough == 1) {
		mwp->mntfrom = imax(mwp->mntfrom, strlen("Filesystem"));
		if (hflag) {
			header = "  Size";
			mwp->total = mwp->used = mwp->avail = strlen(header);
		} else {
			header = getbsize(&headerlen, &blocksize);
			mwp->total = imax(mwp->total, headerlen);
		}
		mwp->used = imax(mwp->used, strlen("Used"));
		mwp->avail = imax(mwp->avail, strlen("Avail"));

		printf("%-*s %-*s %*s %*s Capacity", mwp->mntfrom,
		    "Filesystem", mwp->total, header, mwp->used, "Used",
		    mwp->avail, "Avail");
		if (iflag) {
			mwp->iused = imax(mwp->iused, strlen("  iused"));
			mwp->ifree = imax(mwp->ifree, strlen("ifree"));
			printf(" %*s %*s %%iused", mwp->iused - 2,
			    "iused", mwp->ifree, "ifree");
		}
		printf("  Mounted on\n");
	}
	printf("%-*s", mwp->mntfrom, sfsp->f_mntfromname);
	used = vsfsp->f_blocks - vsfsp->f_bfree;
	availblks = vsfsp->f_bavail + used;
	if (hflag) {
		prthuman(vsfsp, used);
	} else {
		printf(" %*jd %*jd %*jd", mwp->total,
	            fsbtoblk(vsfsp->f_blocks, vsfsp->f_bsize, blocksize),
		    mwp->used, fsbtoblk(used, vsfsp->f_bsize, blocksize),
	            mwp->avail, fsbtoblk(vsfsp->f_bavail, vsfsp->f_bsize,
		    blocksize));
	}
	printf(" %5.0f%%",
	    availblks == 0 ? 100.0 : (double)used / (double)availblks * 100.0);
	if (iflag) {
		inodes = vsfsp->f_files;
		used = inodes - vsfsp->f_ffree;
		printf(" %*jd %*jd %4.0f%% ", mwp->iused, (intmax_t)used,
		    mwp->ifree, (intmax_t)vsfsp->f_ffree, inodes == 0 ? 100.0 :
		    (double)used / (double)inodes * 100.0);
	} else
		printf("  ");
	printf("  %s\n", sfsp->f_mntonname);
}

/*
 * Update the maximum field-width information in `mwp' based on
 * the filesystem specified by `sfsp'.
 */
static void
update_maxwidths(struct maxwidths *mwp, struct statfs *sfsp, struct statvfs *vsfsp)
{
	static long blocksize;
	int dummy;

	if (blocksize == 0)
		getbsize(&dummy, &blocksize);

	mwp->mntfrom = imax(mwp->mntfrom, strlen(sfsp->f_mntfromname));
	mwp->total = imax(mwp->total, quadwidth(fsbtoblk(vsfsp->f_blocks,
	    vsfsp->f_bsize, blocksize)));
	mwp->used = imax(mwp->used, quadwidth(fsbtoblk(vsfsp->f_blocks -
	    vsfsp->f_bfree, vsfsp->f_bsize, blocksize)));
	mwp->avail = imax(mwp->avail, quadwidth(fsbtoblk(vsfsp->f_bavail,
	    vsfsp->f_bsize, blocksize)));
	mwp->iused = imax(mwp->iused, quadwidth(vsfsp->f_files -
	    vsfsp->f_ffree));
	mwp->ifree = imax(mwp->ifree, quadwidth(vsfsp->f_ffree));
}

/* Return the width in characters of the specified long. */
static int
quadwidth(int64_t val)
{
	int len;

	len = 0;
	/* Negative or zero values require one extra digit. */
	if (val <= 0) {
		val = -val;
		len++;
	}
	while (val > 0) {
		len++;
		val /= 10;
	}
	return (len);
}

/*
 * This code constitutes the pre-system call Berkeley df code for extracting
 * information from filesystem superblocks.
 */

union {
	struct fs iu_fs;
	char dummy[SBSIZE];
} sb;
#define sblock sb.iu_fs

int	rfd;

static int
ufs_df(char *file, struct maxwidths *mwp)
{
	struct statfs statfsbuf;
	struct statvfs statvfsbuf;
	struct statfs *sfsp;
	struct statvfs *vsfsp;
	const char *mntpt;
	static int synced;

	if (synced++ == 0)
		sync();

	if ((rfd = open(file, O_RDONLY)) < 0) {
		warn("%s", file);
		return (1);
	}
	if (bread((off_t)SBOFF, &sblock, SBSIZE) == 0) {
		close(rfd);
		return (1);
	}
	sfsp = &statfsbuf;
	vsfsp = &statvfsbuf;
	sfsp->f_type = 1;
	strcpy(sfsp->f_fstypename, "ufs");
	sfsp->f_flags = 0;
	sfsp->f_bsize = vsfsp->f_bsize = sblock.fs_fsize;
	sfsp->f_iosize = vsfsp->f_frsize = sblock.fs_bsize;
	sfsp->f_blocks = vsfsp->f_blocks = sblock.fs_dsize;
	sfsp->f_bfree = vsfsp->f_bfree =
		sblock.fs_cstotal.cs_nbfree * sblock.fs_frag +
		sblock.fs_cstotal.cs_nffree;
	sfsp->f_bavail = vsfsp->f_bavail = freespace(&sblock, sblock.fs_minfree);
	sfsp->f_files = vsfsp->f_files = sblock.fs_ncg * sblock.fs_ipg;
	sfsp->f_ffree = vsfsp->f_ffree = sblock.fs_cstotal.cs_nifree;
	sfsp->f_fsid.val[0] = 0;
	sfsp->f_fsid.val[1] = 0;
	if ((mntpt = getmntpt(file)) == NULL)
		mntpt = "";
	memmove(&sfsp->f_mntonname[0], mntpt, (size_t)MNAMELEN);
	memmove(&sfsp->f_mntfromname[0], file, (size_t)MNAMELEN);
	prtstat(sfsp, vsfsp, mwp);
	close(rfd);
	return (0);
}

static int
bread(off_t off, void *buf, int cnt)
{
	ssize_t nr;

	lseek(rfd, off, SEEK_SET);
	if ((nr = read(rfd, buf, (size_t)cnt)) != (ssize_t)cnt) {
		/* Probably a dismounted disk if errno == EIO. */
		if (errno != EIO)
			fprintf(stderr, "\ndf: %lld: %s\n",
			    (long long)off, strerror(nr > 0 ? EIO : errno));
		return (0);
	}
	return (1);
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: df [-b | -H | -h | -k | -m | -P] [-ailn] [-t type] [file | filesystem ...]\n");
	exit(EX_USAGE);
}

static char *
makenetvfslist(void)
{
	char *str, *strptr, **listptr;
	int mib[3], maxvfsconf, cnt=0, i;
	size_t miblen;
	struct ovfsconf *ptr;

	mib[0] = CTL_VFS; mib[1] = VFS_GENERIC; mib[2] = VFS_MAXTYPENUM;
	miblen=sizeof(maxvfsconf);
	if (sysctl(mib, (unsigned int)(sizeof(mib) / sizeof(mib[0])),
	    &maxvfsconf, &miblen, NULL, 0)) {
		warnx("sysctl failed");
		return (NULL);
	}

	if ((listptr = malloc(sizeof(char*) * maxvfsconf)) == NULL) {
		warnx("malloc failed");
		return (NULL);
	}

	for (ptr = getvfsent(); ptr; ptr = getvfsent())
		if (ptr->vfc_flags & VFCF_NETWORK) {
			listptr[cnt++] = strdup(ptr->vfc_name);
			if (listptr[cnt-1] == NULL) {
				warnx("malloc failed");
				return (NULL);
			}
		}

	if (cnt == 0 ||
	    (str = malloc(sizeof(char) * (32 * cnt + cnt + 2))) == NULL) {
		if (cnt > 0)
			warnx("malloc failed");
		free(listptr);
		return (NULL);
	}

	*str = 'n'; *(str + 1) = 'o';
	for (i = 0, strptr = str + 2; i < cnt; i++, strptr++) {
		strncpy(strptr, listptr[i], 32);
		strptr += strlen(listptr[i]);
		*strptr = ',';
		free(listptr[i]);
	}
	*(--strptr) = '\0';

	free(listptr);
	return (str);
}
